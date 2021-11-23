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
    float accumulation_ratio;
} pc;

layout(constant_id = 2) const int SHADOW_RAY_COUNT = 0;
layout(constant_id = 3) const int REFLECTION_RAY_COUNT = 0;

#include "rt.glsl"

layout(binding = 11) uniform sampler2D prev_depth;
layout(binding = 12) uniform sampler2D prev_normal;
layout(binding = 13) uniform sampler2D prev_reflection;

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec4 uv;
layout(location = 3) in vec3 tangent;
layout(location = 4) in vec3 bitangent;
layout(location = 5) in vec4 prev_proj_pos;

layout(location = 0) out vec4 out_reflection;
layout(location = 1) out vec2 out_normal;

void main()
{
    instance i = instances.array[pc.instance_id];
    ivec3 environment_indices = i.environment_mesh.xyz;

    camera cam = cameras.array[pc.camera_id];

    vec3 view_dir = normalize(cam.origin.xyz - position);

    material mat = sample_material(i.material, gl_FrontFacing, uv.xy, normal, tangent, bitangent);

    // Don't apply color here yet (mostly matters for metals). This lets us
    // preserve more detail later on, as this pass's results may be denoised.
    mat.color.rgb = vec3(1);

    vec3 diffuse_attenuation;
    vec3 specular_attenuation;
    brdf_indirect(view_dir, mat, diffuse_attenuation, specular_attenuation);
    vec3 indirect_specular = vec3(0);

    vec3 ref_dir = clamped_reflect(view_dir, mat.normal);

    if(environment_indices.x != -1)
    {
        float lod = mat.roughness * float(textureQueryLevels(cube_textures[nonuniformEXT(environment_indices.x)])-1);
        indirect_specular = specular_attenuation * sample_cubemap(environment_indices.x, ref_dir, lod);
    }

    indirect_specular = evaluate_reflection(
        position, indirect_specular, environment_indices, view_dir, mat,
        cam.noise.xy
    );

    float accumulation_ratio = pc.accumulation_ratio;

    vec3 proj_pos = prev_proj_pos.xyz/prev_proj_pos.w;
    proj_pos.xy = (proj_pos.xy*0.5+0.5) * textureSize(prev_depth, 0).xy;
    ivec2 sample_pos = ivec2(proj_pos.xy);
    vec3 new_view_normal = mat3(cam.view) * normalize(normal);

    if(
        !any(isnan(proj_pos)) &&
        all(lessThan(sample_pos.xy, textureSize(prev_depth, 0).xy)) &&
        all(greaterThanEqual(sample_pos.xy, ivec2(0)))
    ){
        vec3 old_reflection = texelFetch(prev_reflection, sample_pos, 0).rgb;
        // Don't propagate NaN or inf
        if(any(isnan(old_reflection))||any(isinf(old_reflection)))
        {
            out_reflection = vec4(indirect_specular, 1.0f);
        }
        else
        {
            vec3 old_view_normal = unproject_lambert_azimuthal_equal_area(texelFetch(prev_normal, sample_pos, 0).rg);
            float angle = 1.001f - acos(
                clamp(dot(old_view_normal, new_view_normal), 0.0f, 0.999999f)
            )/M_PI*2;
            accumulation_ratio = mix(1, accumulation_ratio, pow(angle, 2.0f/max(mat.roughness, 1e-2)));

            float old_depth = texelFetch(prev_depth, sample_pos, 0).r;
            if(abs(old_depth/proj_pos.z-1.0f) > 1e-4)
            {
                accumulation_ratio = 1.0f;
            }
            out_reflection = vec4(mix(old_reflection, indirect_specular, accumulation_ratio), 1);
        }
    }
    else
    {
        out_reflection = vec4(indirect_specular, 1.0f);
    }

    out_normal = project_lambert_azimuthal_equal_area(new_view_normal);
}
