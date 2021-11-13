#version 460
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_GOOGLE_include_directive : enable

#include "scene.glsl"

layout(push_constant) uniform push_constant_buffer
{
    uint instance_id;
    uint camera_id;
} pc;

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

    vec3 lighting = vec3(0);
    for(uint i = 0; i < scene_params.point_light_count; ++i)
    {
        vec3 light_dir;
        vec3 color;
        get_point_light_info(point_lights.array[i], position, light_dir, color);
        // Hack to prevent normal map weirdness at grazing angles
        float terminator = smoothstep(-0.05, 0.0, dot(normal, light_dir));
        lighting += terminator * brdf(color, color, light_dir, view_dir, mat);
    }

    for(uint i = 0; i < scene_params.directional_light_count; ++i)
    {
        vec3 light_dir;
        vec3 color;
        get_directional_light_info(directional_lights.array[i], light_dir, color);
        float terminator = smoothstep(-0.05, 0.0, dot(normal, light_dir));
        lighting += brdf(color, color, light_dir, view_dir, mat);
    }

    color = vec4(lighting, mat.color.a);
}
