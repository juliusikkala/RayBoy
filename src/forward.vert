#version 460
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_GOOGLE_include_directive : enable
#include "scene.glsl"

layout(location = 0) in vec3 model_pos;
layout(location = 1) in vec3 model_normal;
layout(location = 2) in vec4 model_uv;
layout(location = 3) in vec4 model_tangent;

layout(location = 0) out vec3 world_position;
layout(location = 1) out vec3 world_normal;
layout(location = 2) out vec4 world_uv;
layout(location = 3) out vec3 world_tangent;
layout(location = 4) out vec3 world_bitangent;

layout(push_constant) uniform push_constant_buffer
{
    uint instance_id;
    uint camera_id;
    uint disable_rt_reflection; // unused here
    float accumulation_ratio; // unused here
} pc;

void main()
{
    instance i = instances.array[pc.instance_id];
    camera cam = cameras.array[pc.camera_id];

    world_position = vec3(i.model_to_world * vec4(model_pos, 1.0f));
    gl_Position = cam.view_proj * vec4(world_position, 1.0f);

    // These outputs have to be normalized because the matrix product causes
    // non-unit length, which in turn weights the interpolation between normals
    // :(
    world_normal = normalize(mat3(i.normal_to_world) * model_normal);
    world_tangent = normalize(mat3(i.normal_to_world) * model_tangent.xyz);
    world_bitangent = cross(world_normal, world_tangent) * model_tangent.w;

    world_uv = model_uv;
}
