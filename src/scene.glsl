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
    material_spec material;
    uint mesh;
    uint pad[3];
};

struct camera
{
    mat4 view_proj;
    mat4 view_inv, proj_inv;
    vec4 origin;
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

struct vertex
{
    vec3 pos;
    vec3 normal;
    vec2 uv;
    vec4 tangent;
};

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

layout(binding = 5) uniform scene_params_buffer
{
    uint point_light_count;
    uint directional_light_count;
} scene_params;

material sample_material(material_spec spec, bool front_facing, vec2 uv, vec3 normal, vec3 tangent, vec3 bitangent)
{
    material mat;
    mat.color = spec.color_factor;
    mat.color.rgb = inverse_srgb_correction(mat.color.rgb);
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
    mat.roughness2 = mr.y * mr.y;

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
        vec3 ts_normal =
            texture(textures[nonuniformEXT(spec.textures.z)], uv).xyz * 2.0f - 1.0f;
        ts_normal.z = max(ts_normal.z, 0.01);
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
    mat.transmittance = mat.color.rgb * spec.emission_transmittance_factors.a;

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

#endif
