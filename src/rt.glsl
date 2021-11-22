#ifndef RT_GLSL
#define RT_GLSL
#include "scene.glsl"
#include "ld_samples_2d.glsl"

struct vertex_attribs
{
    vec4 pos;
    vec4 normal;
    vec4 uv;
    vec4 tangent;
};

layout(binding = 6) uniform accelerationStructureEXT tlas;

layout(binding = 7) buffer vertex_buffer
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
    vec4 uv;
    vec3 tangent;
    vec3 bitangent;
};

vertex_data get_vertex_data(uint instance_index, uint primitive, vec2 barycentric)
{
    instance i = instances.array[nonuniformEXT(instance_index)];

    int mesh = i.environment_mesh.w;

    uint index0 = indices[nonuniformEXT(mesh)].array[3*primitive+0];
    uint index1 = indices[nonuniformEXT(mesh)].array[3*primitive+1];
    uint index2 = indices[nonuniformEXT(mesh)].array[3*primitive+2];

    vertex_attribs vertex0 = vertices[nonuniformEXT(mesh)].array[index0];
    vertex_attribs vertex1 = vertices[nonuniformEXT(mesh)].array[index1];
    vertex_attribs vertex2 = vertices[nonuniformEXT(mesh)].array[index2];

    vec3 weights = vec3(1.0f - barycentric.x - barycentric.y, barycentric);

    vec3 model_pos = vertex0.pos.xyz * weights.x + vertex1.pos.xyz * weights.y + vertex2.pos.xyz * weights.z;
    vec3 model_normal = vertex0.normal .xyz* weights.x + vertex1.normal.xyz * weights.y + vertex2.normal.xyz * weights.z;
    vec4 model_uv = vertex0.uv * weights.x + vertex1.uv * weights.y + vertex2.uv * weights.z;
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

vec3 reflection_ray(vec3 start, vec3 dir, float max_dist, out bool hit)
{
    rayQueryEXT rq;
    rayQueryInitializeEXT(
        rq,
        tlas,
        gl_RayFlagsOpaqueEXT,
        1,
        start,
        1e-4,
        dir,
        max_dist
    );

    vec3 color = vec3(0);

    rayQueryProceedEXT(rq);

    hit = rayQueryGetIntersectionTypeEXT(rq, true) != gl_RayQueryCommittedIntersectionNoneEXT;
    if(hit)
    {
        uint instance_id = rayQueryGetIntersectionInstanceCustomIndexEXT(rq, true);
        uint primitive_id = rayQueryGetIntersectionPrimitiveIndexEXT(rq, true);
        vec2 barycentrics = rayQueryGetIntersectionBarycentricsEXT(rq, true);
        instance i = instances.array[nonuniformEXT(instance_id)];
        vertex_data vd = get_vertex_data(instance_id, primitive_id, barycentrics);
        material mat = sample_material(
            i.material,
            true,
            vd.uv.xy,
            vd.normal,
            vd.tangent,
            vd.bitangent
        );
        color = shade_point(vd.pos, -dir, vd.normal, i.environment_mesh.xyz, vd.uv.zw, mat);
    }

    return color;
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

//https://pbr-book.org/3ed-2018/Monte_Carlo_Integration/2D_Sampling_with_Multidimensional_Transformations
vec2 concentric_mapping(vec2 u)
{
    vec2 uOffset = 2.0f * u - 1.0f;
    vec2 abs_uOffset = abs(uOffset);

    if(all(lessThan(abs_uOffset, vec2(1e-4))))
        return vec2(0);

    vec2 r_theta = abs_uOffset.x > abs_uOffset.y ?
        vec2(uOffset.x, M_PI * 0.25 * uOffset.y / uOffset.x) :
        vec2(uOffset.y, M_PI * 0.5 - M_PI * 0.25 * uOffset.x / uOffset.y);
    return r_theta.x * vec2(cos(r_theta.y), sin(r_theta.y));
}

vec3 point_light_shadow(vec3 position, point_light pl)
{
    vec3 pos = pl.pos_falloff.xyz;
    float radius = pl.color_radius.w;
    vec3 shadow = vec3(0);
    if(SHADOW_RAY_COUNT == 0)
        return vec3(1);
    else if(SHADOW_RAY_COUNT == 1)
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
        vec2 ld_off = texelFetch(blue_noise, noise_pos, 0).xy;

        [[unroll]] for(uint i = 0; i < SHADOW_RAY_COUNT; ++i)
        {
            vec2 off2d = fract(ld_samples[i] + ld_off);
            off2d = concentric_mapping(off2d);

            // This is probably not actually correct but idgaf
            vec3 off = tbn * vec3(off2d * radius, dist);
            shadow += shadow_ray(position, position + off);
        }
    }
    return shadow/SHADOW_RAY_COUNT;
}

vec3 get_indirect_light_rt(
    vec3 pos,
    ivec3 environment_indices,
    vec3 normal,
    vec3 view,
    in material mat,
    vec2 lightmap_uv
){
    vec3 diffuse_attenuation;
    vec3 specular_attenuation;
    brdf_indirect(
        view, mat, diffuse_attenuation, specular_attenuation
    );
    vec3 indirect_diffuse = vec3(0);
    vec3 indirect_specular = vec3(0);

    // Instead of reflect(), we use a clamped version of the same calculation to
    // reduce shimmering edge artefacts.
    vec3 ref_dir = 2.0f * max(
        dot(normal, view), 0.0f
    ) * normal - view;

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
                vec3 refl_color = reflection_ray(pos, ref_dir, 0.1, hit);
                if(hit)
                {
                    refl_color *= brdf_sharp_specular_attenuation(ref_dir, view, mat);
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
            mat3 tbn = create_tangent_space(normal);
            mat3 inv_tbn = transpose(tbn);
            ivec2 noise_pos = ivec2(mod(gl_FragCoord.xy, vec2(textureSize(blue_noise, 0))));
            vec2 ld_off = texelFetch(blue_noise, noise_pos, 0).xy;
            vec3 tan_view = inv_tbn * view;
            [[unroll]] for(uint i = 0; i < REFLECTION_RAY_COUNT; ++i)
            {
                vec2 u = fract(ld_samples[i] + ld_off);
                vec3 dir = tbn * sample_ggx_vndf_tangent(tan_view, mat.roughness2, u);
                bool hit = false;
                vec3 refl_color = reflection_ray(pos, dir, 0.1, hit);
                if(!hit)
                    refl_color = sample_cubemap(environment_indices.x, dir, 0);
                // Yeah, the clamping is arbitrary. It removes some fireflies.
                indirect_specular += clamp(refl_color * ggx_vndf_attenuation(view, dir, mat), vec3(0), vec3(5));
            }
            indirect_specular /= REFLECTION_RAY_COUNT;
        }
    }

    if(environment_indices.y != -1)
    {
        indirect_diffuse = diffuse_attenuation * sample_cubemap(environment_indices.y, normal, 0);
    }

    if(environment_indices.z != -1)
        indirect_diffuse = diffuse_attenuation * texture(
            textures[nonuniformEXT(environment_indices.z)],
            lightmap_uv
        ).rgb;

    return indirect_diffuse + indirect_specular;
}

#endif
