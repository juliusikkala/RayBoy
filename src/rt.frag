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

#include "rt.glsl"

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec4 uv;
layout(location = 3) in vec3 tangent;
layout(location = 4) in vec3 bitangent;

layout(location = 0) out vec4 out_reflection;

void main()
{
    instance i = instances.array[pc.instance_id];
    ivec3 environment_indices = i.environment_mesh.xyz;
    camera cam = cameras.array[pc.camera_id];

    vec3 view_dir = normalize(cam.origin.xyz - position);

    material mat = sample_material(i.material, gl_FrontFacing, uv.xy, normal, tangent, bitangent);

    vec3 diffuse_attenuation;
    vec3 specular_attenuation;
    mat.color = vec4(1);
    brdf_indirect(view_dir, mat, diffuse_attenuation, specular_attenuation);
    vec3 indirect_specular = vec3(0);

    // Instead of reflect(), we use a clamped version of the same calculation to
    // reduce shimmering edge artefacts.
    vec3 ref_dir = 2.0f * max(
        dot(mat.normal, view_dir), 0.0f
    ) * mat.normal - view_dir;

    if(environment_indices.x != -1)
    {
        float lod = mat.roughness * float(textureQueryLevels(cube_textures[nonuniformEXT(environment_indices.x)])-1);
        indirect_specular = specular_attenuation * sample_cubemap(environment_indices.x, ref_dir, lod);
    }

    if(REFLECTION_RAY_COUNT == 1)
    {
        if(pc.disable_rt_reflection != 1)
        {
            const float REFLECTION_RAY_LIMIT_ROUGHNESS = 0.3;
            float fade = clamp((mat.roughness-REFLECTION_RAY_LIMIT_ROUGHNESS)*(1.0/REFLECTION_RAY_LIMIT_ROUGHNESS), 0, 1);
            if(fade < 0.99)
            {
                bool hit = false;
                vec3 refl_color = reflection_ray(position, ref_dir, 0.1, hit);
                if(hit)
                {
                    refl_color *= brdf_sharp_specular_attenuation(ref_dir, view_dir, mat);
                    indirect_specular = mix(refl_color, indirect_specular, fade);
                }
            }
        }
    }
    else if(REFLECTION_RAY_COUNT >= 2)
    {
        if(pc.disable_rt_reflection != 1)
        {
            indirect_specular = vec3(0);
            mat3 tbn = create_tangent_space(mat.normal);
            mat3 inv_tbn = transpose(tbn);
            ivec2 noise_pos = ivec2(mod(gl_FragCoord.xy, vec2(textureSize(blue_noise, 0))));
            vec2 ld_off = texelFetch(blue_noise, noise_pos, 0).xy;
            vec3 tan_view = inv_tbn * view_dir;
            [[unroll]] for(uint i = 0; i < REFLECTION_RAY_COUNT; ++i)
            {
                vec2 u = fract(ld_samples[i] + ld_off);
                vec3 dir = tbn * sample_ggx_vndf_tangent(tan_view, mat.roughness2, u);
                bool hit = false;
                vec3 refl_color = reflection_ray(position, dir, 0.1, hit);
                if(!hit)
                    refl_color = sample_cubemap(environment_indices.x, dir, 0);
                // Yeah, the clamping is arbitrary. It removes some fireflies.
                indirect_specular += clamp(refl_color * ggx_vndf_attenuation(view_dir, dir, mat), vec3(0), vec3(5));
            }
            indirect_specular /= REFLECTION_RAY_COUNT;
        }
    }

    out_reflection = vec4(indirect_specular, 1);
}
