#version 460
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_GOOGLE_include_directive : enable
#include "scene.glsl"

layout(location = 0) in vec3 model_pos;
layout(location = 1) in vec3 model_normal;
layout(location = 2) in vec2 model_uv;
layout(location = 3) in vec4 model_tangent;

layout(push_constant) uniform push_constant_buffer
{
    uint instance_id;
    uint camera_id;
} pc;

void main()
{
    instance i = instances.array[pc.instance_id];
    camera cam = cameras.array[pc.camera_id];

    vec3 world_position = vec3(i.model_to_world * vec4(model_pos, 1.0f));
    gl_Position = cam.view_proj * vec4(world_position, 1.0f);
}
