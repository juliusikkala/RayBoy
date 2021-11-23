#version 460
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_control_flow_attributes : enable

#include "scene.glsl"

layout(push_constant) uniform push_constant_buffer
{
    uint instance_id;
    uint camera_id;
    uint disable_rt_reflection; // unused here
    float accumulation_ratio; // unused here
} pc;

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec4 uv;
layout(location = 3) in vec3 tangent;
layout(location = 4) in vec3 bitangent;

layout(location = 0) out vec4 color;

void main()
{
    instance i = instances.array[pc.instance_id];
    camera cam = cameras.array[pc.camera_id];

    vec3 view_dir = normalize(cam.origin.xyz - position);

    material mat = sample_material(i.material, gl_FrontFacing, uv.xy, normal, tangent, bitangent);

    vec3 lighting = shade_point(position, view_dir, normal, i.environment_mesh.xyz, uv.zw, mat);

    float alpha = 1.0f;

    if(mat.transmittance.r > 0.0f)
    {
        float cos_d = abs(dot(view_dir, normal));
        float fresnel = fresnel_schlick(cos_d, mat.f0);
        alpha = mix(1.0f, fresnel, mat.transmittance.r);
    }

    color = vec4(lighting, alpha);
}
