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

    vec3 indirect_diffuse;
    vec3 indirect_specular;

    get_indirect_light(
        position,
        i.environment_mesh.xyz,
        mat.normal,
        view_dir,
        mat.roughness,
        vec2(0), // TODO: Lightmaps
        indirect_diffuse,
        indirect_specular
    );

    vec3 lighting = brdf_indirect(
        indirect_diffuse,
        indirect_specular,
        view_dir,
        mat
    ) + mat.emission;

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
