#version 460
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_control_flow_attributes : enable

#include "scene.glsl"

layout(push_constant) uniform push_constant_buffer
{
    uint instance_id;
    uint camera_id;
} pc;

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec4 uv;
layout(location = 3) in vec3 tangent;
layout(location = 4) in vec3 bitangent;

layout(location = 0) out vec2 out_normal;

void main()
{
    instance i = instances.array[pc.instance_id];
    camera cam = cameras.array[pc.camera_id];

    out_normal = project_lambert_azimuthal_equal_area(mat3(cam.view) * normal);
}

