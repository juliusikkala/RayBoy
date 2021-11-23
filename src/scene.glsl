#ifndef SCENE_GLSL
#define SCENE_GLSL
#include "material.glsl"
#include "srgb.glsl"

struct material_spec
{
    // xyz = color, w = alpha
    vec4 color_factor;
    // x = metallic, y = roughness, z = normal, w = ior
    vec4 metallic_roughness_normal_ior_factors;
    // xyz = emission intensity, w = transmittance
    vec4 emission_transmittance_factors;
    // x = color + alpha, y = metallic+roughness, z = normal, w = emission
    ivec4 textures;
};

struct instance
{
    mat4 model_to_world;
    mat4 normal_to_world;
    mat4 prev_mvp;
    material_spec material;
    // x = radiance index, y = irradiance index, z = lightmap index, w = mesh_index
    ivec4 environment_mesh;
};

struct camera
{
    mat4 view_proj;
    mat4 view;
    vec4 projection_info;
    vec4 clip_info;
    vec4 origin;
    vec4 noise;
};

struct point_light
{
    // xyz = color, w = radius (soft shadows)
    vec4 color_radius;
    // xyz = position in world space, w = falloff exponent
    vec4 pos_falloff;
    // xyz = direction in world space, w = cutoff angle in radians
    vec4 direction_cutoff;
};

struct directional_light
{
    // xyz = color, w = unused.
    vec4 color;
    // xyz = In world space, w = cos(solid_angle).
    vec4 direction;
};

layout(constant_id = 0) const uint POINT_LIGHT_COUNT = 0;
layout(constant_id = 1) const uint DIRECTIONAL_LIGHT_COUNT = 0;

layout(binding = 0) buffer instance_buffer
{
    instance array[];
} instances;

layout(binding = 1) buffer point_light_buffer
{
    point_light array[];
} point_lights;

layout(binding = 2) buffer directional_light_buffer
{
    directional_light array[];
} directional_lights;

layout(binding = 3) buffer camera_buffer
{
    camera array[];
} cameras;

layout(binding = 4) uniform sampler2D textures[];

layout(binding = 5) uniform samplerCube cube_textures[];

vec3 unproject_depth(float depth, vec2 uv, in camera cam)
{
    return unproject_position(
        linearize_depth(depth*2.0f-1.0f, cam.clip_info.xyz),
        vec2(uv.x, 1-uv.y),
        cam.projection_info.xy
    );
}

material sample_material(material_spec spec, bool front_facing, vec2 uv, vec3 normal, vec3 tangent, vec3 bitangent)
{
    material mat;
    mat.color = spec.color_factor;
    if(spec.textures.x != -1)
    {
        vec4 tex_col = texture(textures[nonuniformEXT(spec.textures.x)], uv);
        tex_col.rgb = inverse_srgb_correction(tex_col.rgb);
        mat.color *= tex_col;
    }

    vec2 mr = spec.metallic_roughness_normal_ior_factors.xy;
    if(spec.textures.y != -1)
        mr *= texture(textures[nonuniformEXT(spec.textures.y)], uv).bg;
    mat.metallic = mr.x;
    mat.roughness = mr.y;
    mat.roughness2 = mat.roughness * mat.roughness;

    mat.ior_after = spec.metallic_roughness_normal_ior_factors.w;
    mat.ior_before = 1.0f;
    mat.f0 = (mat.ior_after - mat.ior_before)/(mat.ior_after + mat.ior_before);
    mat.f0 *= mat.f0;

    if(spec.textures.z != -1)
    {
        mat3 tbn = mat3(
            normalize(tangent),
            normalize(bitangent),
            normalize(normal)
        );
        vec3 ts_normal = texture(textures[nonuniformEXT(spec.textures.z)], uv).xyz * 2.0f - 1.0f;
        ts_normal.xy *= spec.metallic_roughness_normal_ior_factors.z;
        mat.normal = normalize(tbn * ts_normal);
    }
    else mat.normal = normalize(normal);

    mat.normal = front_facing ? mat.normal : -mat.normal;

    mat.emission = spec.emission_transmittance_factors.rgb;
    if(spec.textures.w != -1)
    {
        vec3 tex_col = texture(textures[nonuniformEXT(spec.textures.w)], uv).rgb;
        tex_col = inverse_srgb_correction(tex_col);
        mat.emission *= tex_col;
    }

    mat.emission *= mat.color.a;
    mat.transmittance = vec3(spec.emission_transmittance_factors.a);

    return mat;
}

void get_point_light_info(
    point_light l,
    vec3 pos,
    out vec3 dir,
    out vec3 color
){
    dir = l.pos_falloff.xyz - pos;
    float dist2 = dot(dir, dir);
    float dist = sqrt(dist2);
    dir /= dist;

    color = l.color_radius.rgb/dist2;

    if(l.pos_falloff.w > 0)
    {
        float cutoff = dot(dir, -l.direction_cutoff.xyz);
        cutoff = cutoff > l.direction_cutoff.w ?
            1.0f-pow(
                max(1.0f-cutoff, 0.0f)/(1.0f-l.direction_cutoff.w),
                l.pos_falloff.w
            ) : 0.0f;
        color *= cutoff;
    }
}

void get_directional_light_info(
    directional_light l, 
    out vec3 dir,
    out vec3 color
){
    dir = -l.direction.xyz;
    color = l.color.rgb;
}

vec3 sample_cubemap(int ind, vec3 dir, float lod)
{
    return textureLod(cube_textures[nonuniformEXT(ind)], vec3(dir.x, dir.y, -dir.z), lod).rgb;
}

vec3 get_indirect_light(
    vec3 pos,
    ivec3 environment_indices,
    vec3 normal,
    vec3 view,
    in material mat,
    vec2 lightmap_uv
){
    vec3 diffuse_attenuation;
    vec3 specular_attenuation;
    brdf_indirect(
        view, mat, diffuse_attenuation, specular_attenuation
    );
    vec3 indirect_diffuse = vec3(0);
    vec3 indirect_specular = vec3(0);

    if(environment_indices.x != -1)
    {
        // Instead of reflect(), we use a clamped version of the same calculation to
        // reduce shimmering edge artefacts.
        vec3 ref_dir = 2.0f * max(
            dot(normal, view), 0.0f
        ) * normal - view;

        float lod = mat.roughness * float(textureQueryLevels(cube_textures[nonuniformEXT(environment_indices.x)])-1);
        indirect_specular = specular_attenuation * sample_cubemap(environment_indices.x, ref_dir, lod);
    }

    if(environment_indices.y != -1)
    {
        indirect_diffuse = diffuse_attenuation * sample_cubemap(environment_indices.y, normal, 0);
    }

    if(environment_indices.z != -1)
    {
        indirect_diffuse = diffuse_attenuation * texture(
            textures[nonuniformEXT(environment_indices.z)],
            lightmap_uv
        ).rgb;
    }
    return indirect_diffuse + indirect_specular;
}

vec3 shade_point(
    vec3 position,
    vec3 view_dir,
    vec3 surface_normal,
    ivec3 environment_indices,
    vec2 lightmap_uv,
    in material mat
){
    vec3 lighting = get_indirect_light(
        position,
        environment_indices,
        mat.normal,
        view_dir,
        mat,
        lightmap_uv
    ) + mat.emission;

    for(uint i = 0; i < POINT_LIGHT_COUNT; ++i)
    {
        vec3 light_dir;
        vec3 color;
        get_point_light_info(point_lights.array[i], position, light_dir, color);
        // Hack to prevent normal map weirdness at grazing angles
        float terminator = smoothstep(-0.05, 0.0, dot(surface_normal, light_dir));
        lighting += terminator * brdf(color, color, light_dir, view_dir, mat);
    }

    for(uint i = 0; i < DIRECTIONAL_LIGHT_COUNT; ++i)
    {
        vec3 light_dir;
        vec3 color;
        get_directional_light_info(directional_lights.array[i], light_dir, color);
        float terminator = smoothstep(-0.05, 0.0, dot(surface_normal, light_dir));
        lighting += terminator * brdf(color, color, light_dir, view_dir, mat);
    }
    return lighting;
}

#endif
