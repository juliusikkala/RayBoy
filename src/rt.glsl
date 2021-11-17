#ifndef RT_GLSL
#define RT_GLSL
#include "scene.glsl"
#include "poisson_samples_2d.glsl"

struct vertex_attribs
{
    vec3 pos;
    vec3 normal;
    vec2 uv;
    vec4 tangent;
};

layout(binding = 6) uniform accelerationStructureEXT tlas;

layout(binding = 7, scalar) buffer vertex_buffer
{
    vertex_attribs array[];
} vertices[];

layout(binding = 8) buffer index_buffer
{
    uint array[];
} indices[];

layout(binding = 9) uniform sampler2D blue_noise;

struct vertex_data
{
    vec3 pos;
    vec3 normal;
    vec2 uv;
    vec3 tangent;
    vec3 bitangent;
};

vertex_data get_vertex_data(uint instance_index, uint primitive, vec2 barycentric)
{
    instance i = instances.array[nonuniformEXT(instance_index)];

    int mesh = i.environment_mesh.w;

    uint index0 = indices[nonuniformEXT(mesh)].array[3*primitive+0];
    uint index1 = indices[nonuniformEXT(mesh)].array[3*primitive+1];
    uint index2 = indices[nonuniformEXT(mesh)].array[3*primitive+1];

    vertex_attribs vertex0 = vertices[nonuniformEXT(mesh)].array[index0];
    vertex_attribs vertex1 = vertices[nonuniformEXT(mesh)].array[index1];
    vertex_attribs vertex2 = vertices[nonuniformEXT(mesh)].array[index2];

    vec3 weights = vec3(1.0f - barycentric.x - barycentric.y, barycentric);

    vec3 model_pos = vertex0.pos * weights.x + vertex1.pos * weights.y + vertex2.pos * weights.z;
    vec3 model_normal = vertex0.normal * weights.x + vertex1.normal * weights.y + vertex2.normal * weights.z;
    vec2 model_uv = vertex0.uv * weights.x + vertex1.uv * weights.y + vertex2.uv * weights.z;
    vec4 model_tangent = vertex0.tangent * weights.x + vertex1.tangent * weights.y + vertex2.tangent * weights.z;

    vertex_data vd;
    vd.pos = vec3(i.model_to_world * vec4(model_pos, 1));
    vd.normal = normalize(mat3(i.model_to_world) * model_normal);
    vd.uv = model_uv;
    vd.tangent = normalize(mat3(i.model_to_world) * model_tangent.xyz);
    vd.bitangent = normalize(cross(vd.normal, vd.tangent) * model_tangent.w);

    return vd;
}

vec3 shadow_ray(vec3 start, vec3 end)
{
    vec3 dir = end - start;
    float len = length(dir);
    dir /= len;
    rayQueryEXT rq;
    rayQueryInitializeEXT(
        rq,
        tlas,
        gl_RayFlagsTerminateOnFirstHitEXT|gl_RayFlagsOpaqueEXT|gl_RayFlagsSkipClosestHitShaderEXT,
        1,
        start,
        1e-4,
        dir,
        len
    );

    vec3 visibility = vec3(1);

    rayQueryProceedEXT(rq);

    if(rayQueryGetIntersectionTypeEXT(rq, true) != gl_RayQueryCommittedIntersectionNoneEXT)
    {
        visibility *= vec3(0);
    }

    return visibility;
}

// Mostly for debugging purposes.
float distance_ray(vec3 start, vec3 dir)
{
    rayQueryEXT rq;
    rayQueryInitializeEXT(rq, tlas, gl_RayFlagsNoneEXT, 0xFF, start, 1e-4, dir, 1e4);

    while(rayQueryProceedEXT(rq))
    {
        rayQueryConfirmIntersectionEXT(rq);
    }

    if(rayQueryGetIntersectionTypeEXT(rq, true) != gl_RayQueryCommittedIntersectionNoneEXT)
    {
        return rayQueryGetIntersectionTEXT(rq, true);
    }

    return 1e3;
}

mat3 create_tangent_space(vec3 normal)
{
    vec3 major;
    if(abs(normal.x) < 0.57735026918962576451) major = vec3(1,0,0);
    else if(abs(normal.y) < 0.57735026918962576451) major = vec3(0,1,0);
    else major = vec3(0,0,1);

    vec3 tangent = normalize(cross(normal, major));
    vec3 bitangent = cross(normal, tangent);

    return mat3(tangent, bitangent, normal);
}

vec3 point_light_shadow(vec3 position, point_light pl)
{
    vec3 pos = pl.pos_falloff.xyz;
    float radius = pl.color_radius.w;
    vec3 shadow = vec3(0);
    if(SHADOW_RAY_COUNT == 1)
    { // Hard shadows
        shadow += shadow_ray(position, pos);
    }
    else
    { // Soft shadows
        vec3 dir = pos - position;
        float dist = length(dir);
        dir /= dist;
        mat3 tbn = create_tangent_space(dir);
        ivec2 noise_pos = ivec2(mod(gl_FragCoord.xy, vec2(textureSize(blue_noise, 0))));
        float angle = texelFetch(blue_noise, noise_pos, 0).x;
        vec2 cs = vec2(cos(angle*2*3.141592), sin(angle*2*3.141592));
        vec3 tangent = tbn[0];
        vec3 bitangent = tbn[1];
        tbn[0] = tangent*cs.x-bitangent*cs.y;
        tbn[1] = tangent*cs.y+bitangent*cs.x;

        [[unroll]] for(uint i = 0; i < SHADOW_RAY_COUNT; ++i)
        {
            vec2 off2d = poisson_disk_samples[i];
            // This is probably not actually correct but idgaf
            vec3 off = tbn * vec3(off2d * radius, dist);
            shadow += shadow_ray(position, position + off);
        }
    }
    return shadow/SHADOW_RAY_COUNT;
}

#endif
