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
    uint disable_rt_reflection; // unused here
} pc;

layout(constant_id = 2) const int SHADOW_RAY_COUNT = 0;
layout(constant_id = 3) const int REFLECTION_RAY_COUNT = 0;

#include "rt.glsl"

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 uv;
layout(location = 3) in vec3 tangent;
layout(location = 4) in vec3 bitangent;

layout(location = 0) out vec4 color;

void main()
{
    instance i = instances.array[pc.instance_id];
    camera cam = cameras.array[pc.camera_id];

    vec3 view_dir = normalize(cam.origin.xyz - position);

    material mat = sample_material(i.material, gl_FrontFacing, uv, normal, tangent, bitangent);

    vec3 indirect_diffuse;
    vec3 indirect_specular;

    vec3 lighting = get_indirect_light_rt(
        position,
        i.environment_mesh.xyz,
        mat.normal,
        view_dir,
        mat,
        vec2(0) // TODO: Lightmaps
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
        float cos_d = clamp(dot(view_dir, normal), 0.0f, 1.0f);
        float fresnel = fresnel_schlick(cos_d, mat.f0);
        alpha = mix(1.0f, fresnel, mat.transmittance.r);
    }

    color = vec4(lighting, alpha);
}
