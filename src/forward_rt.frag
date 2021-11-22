#version 460
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_ray_query : enable
#extension GL_EXT_control_flow_attributes : enable

layout(push_constant) uniform push_constant_buffer
{
    uint instance_id;
    uint camera_id;
    uint disable_rt_reflection;
} pc;

layout(constant_id = 2) const int SHADOW_RAY_COUNT = 0;
layout(constant_id = 3) const int REFLECTION_RAY_COUNT = 0;

layout(binding = 11) uniform sampler2D normal_texture;
layout(binding = 12) uniform sampler2D depth_texture;
layout(binding = 13) uniform sampler2D reflection_texture;

#include "rt.glsl"

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec4 uv;
layout(location = 3) in vec3 tangent;
layout(location = 4) in vec3 bitangent;

layout(location = 0) out vec4 color;

float sample_weight(ivec2 off, vec3 view_pos, vec3 view_normal, in camera cam)
{
    ivec2 sample_coord = ivec2(gl_FragCoord.xy)+off;
    sample_coord = clamp(sample_coord, ivec2(0), textureSize(depth_texture, 0)-1);

    vec2 buffer_normal = texelFetch(normal_texture, sample_coord, 0).xy;
    float buffer_depth = texelFetch(depth_texture, sample_coord, 0).x;

    vec3 sample_view_normal = unproject_lambert_azimuthal_equal_area(buffer_normal);
    vec2 sample_uv = vec2(sample_coord)/vec2(textureSize(depth_texture, 0)-1);
    vec3 sample_view_pos = unproject_depth(buffer_depth, sample_uv, cam);

    vec3 sample_view_tangent = normalize(view_pos - sample_view_pos);

    return
        clamp(1.0f-abs(dot(sample_view_tangent, view_normal))*3.0f, 0.0, 1.0) *
        clamp(dot(sample_view_normal, view_normal)*2.0f-1.0f, 0.0f, 1.0f);
}

vec3 get_indirect_light_rt(
    vec3 view_pos,
    vec3 view_normal,
    ivec3 environment_indices,
    vec3 view,
    in material mat,
    in camera cam,
    vec2 lightmap_uv
){
    vec3 diffuse_attenuation;
    vec3 specular_attenuation;
    brdf_indirect(
        view, mat, diffuse_attenuation, specular_attenuation
    );
    vec3 indirect_diffuse = vec3(0);
    vec3 indirect_specular = vec3(0);

    // Instead of reflect(), we use a clamped version of the same calculation to
    // reduce shimmering edge artefacts.
    vec3 ref_dir = 2.0f * max(
        dot(mat.normal, view), 0.0f
    ) * mat.normal - view;
    vec3 specular_multiplier = mix(vec3(1), mat.color.rgb, mat.metallic);

    // Gather reflection data
    if(REFLECTION_RAY_COUNT >= 2 && pc.disable_rt_reflection != 1 && mat.roughness > 0.05)
    { // Denoise!
        vec4 sum = vec4(0);

        for(int x = -3; x <= 3; ++x)
        for(int y = -3; y <= 3; ++y)
        {
            ivec2 off = ivec2(x, y);
            ivec2 sample_coord = ivec2(gl_FragCoord.xy)+off;
            vec3 color = texelFetch(reflection_texture, sample_coord, 0).rgb;
            float weight = sample_weight(off, view_pos, view_normal, cam);
            weight = max(weight, 1e-5);
            sum += vec4(color * weight, weight);
        }

        indirect_specular = sum.rgb / sum.a;
    }
    else
    { // Pick best sample of 5 close positions
        ivec2 best_off = ivec2(0);
        float best_weight = sample_weight(ivec2(0), view_pos, view_normal, cam);

        float weight = sample_weight(ivec2(1,0), view_pos, view_normal, cam);
        if(weight > best_weight) { best_weight = weight; best_off = ivec2(1,0); }
        weight = sample_weight(ivec2(-1,0), view_pos, view_normal, cam);
        if(weight > best_weight) { best_weight = weight; best_off = ivec2(-1,0); }
        weight = sample_weight(ivec2(0,1), view_pos, view_normal, cam);
        if(weight > best_weight) { best_weight = weight; best_off = ivec2(0,1); }
        weight = sample_weight(ivec2(0,-1), view_pos, view_normal, cam);
        if(weight > best_weight) { best_weight = weight; best_off = ivec2(0,-1); }

        indirect_specular = texelFetch(reflection_texture, ivec2(gl_FragCoord.xy)+best_off, 0).rgb;
    }

    if(environment_indices.y != -1)
    {
        indirect_diffuse = diffuse_attenuation * sample_cubemap(environment_indices.y, mat.normal, 0);
    }

    if(environment_indices.z != -1)
        indirect_diffuse = diffuse_attenuation * texture(
            textures[nonuniformEXT(environment_indices.z)],
            lightmap_uv
        ).rgb;

    return indirect_diffuse + specular_multiplier * indirect_specular;
}

void main()
{
    instance i = instances.array[pc.instance_id];
    camera cam = cameras.array[pc.camera_id];

    vec3 view_dir = normalize(cam.origin.xyz - position);

    material mat = sample_material(i.material, gl_FrontFacing, uv.xy, normal, tangent, bitangent);

    vec3 indirect_diffuse;
    vec3 indirect_specular;

    vec3 lighting = get_indirect_light_rt(
        mat3(cam.view) * (position - cam.origin.xyz),
        mat3(cam.view) * normalize(normal),
        i.environment_mesh.xyz,
        view_dir,
        mat,
        cam,
        uv.zw
    ) + mat.emission;

    [[unroll]] for(uint i = 0; i < POINT_LIGHT_COUNT; ++i)
    {
        vec3 light_dir;
        vec3 color;
        const point_light pl = point_lights.array[i];
        get_point_light_info(pl, position, light_dir, color);
        // Hack to prevent normal map weirdness at grazing angles
        float terminator = smoothstep(-0.05, 0.0, dot(normal, light_dir));
        vec3 shadow = point_light_shadow(position, pl);
        lighting += terminator * shadow * brdf(color, color, light_dir, view_dir, mat);
    }

    [[unroll]] for(uint i = 0; i < DIRECTIONAL_LIGHT_COUNT; ++i)
    {
        vec3 light_dir;
        vec3 color;
        get_directional_light_info(directional_lights.array[i], light_dir, color);
        float terminator = smoothstep(-0.05, 0.0, dot(normal, light_dir));
        vec3 shadow = shadow_ray(position, position + light_dir*1e4);
        lighting += terminator * brdf(color, color, light_dir, view_dir, mat);
    }

    float alpha = 1.0f;

    if(mat.transmittance.r > 0.0f)
    {
        float cos_d = abs(dot(view_dir, normalize(normal)));
        float fresnel = fresnel_schlick(cos_d, mat.f0);
        alpha = mix(1.0f, fresnel, mat.transmittance.r);
    }

    color = vec4(lighting, alpha);
}
