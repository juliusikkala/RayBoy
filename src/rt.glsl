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

    vec3 visibility = vec3(1);

    if(REFRACTION_RAY_COUNT == 0)
    { // Without any refractions, we can do shadows the easy and fast way.
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

        rayQueryProceedEXT(rq);

        if(rayQueryGetIntersectionTypeEXT(rq, true) != gl_RayQueryCommittedIntersectionNoneEXT)
        {
            visibility = vec3(0);
        }
    }
    else
    { // Well shit, this one's a doozy.
        rayQueryEXT rq;
        rayQueryInitializeEXT(
            rq, tlas, gl_RayFlagsTerminateOnFirstHitEXT|gl_RayFlagsNoneEXT|gl_RayFlagsSkipClosestHitShaderEXT, 1, start, 1e-4, dir, len
        );

        float transmit_dist = 0;
        vec3 transmit_color = vec3(1);
        while(rayQueryProceedEXT(rq))
        {
            uint type = rayQueryGetIntersectionTypeEXT(rq, false);
            bool front = rayQueryGetIntersectionFrontFaceEXT(rq, false);
            if(type == gl_RayQueryCandidateIntersectionTriangleEXT)
            {
                uint instance_id = rayQueryGetIntersectionInstanceCustomIndexEXT(rq, false);
                uint primitive_id = rayQueryGetIntersectionPrimitiveIndexEXT(rq, false);
                vec2 barycentrics = rayQueryGetIntersectionBarycentricsEXT(rq, false);
                instance i = instances.array[nonuniformEXT(instance_id)];
                vertex_data vd = get_vertex_data(instance_id, primitive_id, barycentrics);
                material mat = sample_material(
                    i.material,
                    front,
                    vd.uv.xy,
                    vd.normal,
                    vd.tangent,
                    vd.bitangent
                );

                // Can't apply fresnel and expect it to work cause we aren't
                // refracting correctly... So this is the next best thing.
                float cos_d = abs(dot(-dir, mat.normal));
                visibility *= mat.color.rgb * mat.transmittance;
                transmit_color = mat.color.rgb;

                float t = rayQueryGetIntersectionTEXT(rq, false);
                transmit_dist += front ? -t : t;
            }
        }

        if(transmit_dist < 0) transmit_dist += len;
        // RAYBOY HACK: Hervantavakio
        visibility *= pow(transmit_color, vec3(transmit_dist*2000));

        // If there's a hit, that means we hit something opaque since everything
        // transparent would've been evaluated in the loop and left uncommitted.
        if(rayQueryGetIntersectionTypeEXT(rq, true) != gl_RayQueryCommittedIntersectionNoneEXT)
            visibility = vec3(0);
    }

    return visibility;
}

vec3 shade_point_rt(
    vec3 position,
    vec3 view_dir,
    vec3 surface_normal,
    ivec3 environment_indices,
    vec2 lightmap_uv,
    in material mat
){
    vec3 lighting = get_indirect_light(
        position,
        environment_indices,
        mat.normal,
        view_dir,
        mat,
        lightmap_uv
    ) + mat.emission;

    for(uint i = 0; i < POINT_LIGHT_COUNT; ++i)
    {
        vec3 light_dir;
        vec3 color;
        get_point_light_info(point_lights.array[i], position, light_dir, color);
        // Hack to prevent normal map weirdness at grazing angles
        float terminator = smoothstep(-0.05, 0.0, dot(surface_normal, light_dir));
        vec3 shadow = shadow_ray(position, point_lights.array[i].pos_falloff.xyz);
        lighting += terminator * shadow * brdf(color, color, light_dir, view_dir, mat);
    }

    for(uint i = 0; i < DIRECTIONAL_LIGHT_COUNT; ++i)
    {
        vec3 light_dir;
        vec3 color;
        get_directional_light_info(directional_lights.array[i], light_dir, color);
        float terminator = smoothstep(-0.05, 0.0, dot(surface_normal, light_dir));
        vec3 shadow = shadow_ray(position, position + light_dir*1e4);
        lighting += terminator * shadow * brdf(color, color, light_dir, view_dir, mat);
    }
    return lighting;
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

vec3 reflection_ray(vec3 start, vec3 dir, float max_dist, out bool hit, float lod_bias)
{
    rayQueryEXT rq;
    rayQueryInitializeEXT(
        rq,
        tlas,
        gl_RayFlagsOpaqueEXT,
        5,
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
        material mat = sample_material_lod(
            i.material,
            true,
            vd.uv.xy,
            vd.normal,
            vd.tangent,
            vd.bitangent,
            lod_bias
        );
        if(SECONDARY_SHADOWS == 0)
            color = shade_point(vd.pos, -dir, vd.normal, i.environment_mesh.xyz, vd.uv.zw, mat);
        else
            color = shade_point_rt(vd.pos, -dir, vd.normal, i.environment_mesh.xyz, vd.uv.zw, mat);
    }

    return color;
}

vec3 xray(
    vec3 start,
    vec3 dir,
    vec3 volume_color,
    float max_dist,
    out bool hit
) {
    vec3 color = vec3(1);
    float dist = max_dist;
    // First, we determine the distance to the next opaque surface and calculate
    // shading for it while we're at it.
    {
        rayQueryEXT rq;
        rayQueryInitializeEXT(
            rq, tlas, gl_RayFlagsOpaqueEXT|gl_RayFlagsCullNoOpaqueEXT, 1, start, 1e-5, dir, max_dist
        );

        rayQueryProceedEXT(rq);

        hit = rayQueryGetIntersectionTypeEXT(rq, true) != gl_RayQueryCommittedIntersectionNoneEXT;
        if(hit)
        {
            dist = rayQueryGetIntersectionTEXT(rq, true);
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
    }

    // Then, we abuse the any-hit mechanic to calculate the distance we travel
    // inside the object.
    float transmit_dist = 0;
    {
        rayQueryEXT rq;
        rayQueryInitializeEXT(
            rq, tlas, gl_RayFlagsCullOpaqueEXT, 1, start, 1e-5, dir, dist*1.01
        );

        while(rayQueryProceedEXT(rq))
        {
            uint type = rayQueryGetIntersectionTypeEXT(rq, false);
            bool front = rayQueryGetIntersectionFrontFaceEXT(rq, false);
            if(type == gl_RayQueryCandidateIntersectionTriangleEXT)
            {
                float t = rayQueryGetIntersectionTEXT(rq, false);
                transmit_dist += front ? -t : t;
            }
        }
        if(transmit_dist < 0) transmit_dist += dist;
        color *= pow(volume_color, vec3(transmit_dist*1e3));
    }

    return color;
}

vec3 direct_refraction_ray(
    vec3 start,
    vec3 dir,
    vec3 volume_color,
    float max_dist,
    out bool hit
) {
    vec3 color = vec3(1);
    float dist = max_dist;
    // First, we determine the distance to the next opaque surface and calculate
    // shading for it while we're at it.
    {
        rayQueryEXT rq;
        rayQueryInitializeEXT(
            rq, tlas, gl_RayFlagsOpaqueEXT|gl_RayFlagsCullNoOpaqueEXT, 1, start, 1e-5, dir, max_dist
        );

        rayQueryProceedEXT(rq);

        hit = rayQueryGetIntersectionTypeEXT(rq, true) != gl_RayQueryCommittedIntersectionNoneEXT;
        if(hit)
        {
            dist = rayQueryGetIntersectionTEXT(rq, true);
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
    }

    {
        rayQueryEXT rq;
        rayQueryInitializeEXT(
            rq, tlas, gl_RayFlagsCullOpaqueEXT, 1, start, 1e-5, dir, dist*1.01
        );

        while(rayQueryProceedEXT(rq))
        {
            uint type = rayQueryGetIntersectionTypeEXT(rq, false);
            bool front = rayQueryGetIntersectionFrontFaceEXT(rq, false);
            if(type == gl_RayQueryCandidateIntersectionTriangleEXT)
            {
                float t = rayQueryGetIntersectionTEXT(rq, false);

                uint instance_id = rayQueryGetIntersectionInstanceCustomIndexEXT(rq, false);
                uint primitive_id = rayQueryGetIntersectionPrimitiveIndexEXT(rq, false);
                vec2 barycentrics = rayQueryGetIntersectionBarycentricsEXT(rq, false);
                instance i = instances.array[nonuniformEXT(instance_id)];
                vertex_data vd = get_vertex_data(instance_id, primitive_id, barycentrics);
                material mat = sample_material(
                    i.material,
                    front,
                    vd.uv.xy,
                    vd.normal,
                    vd.tangent,
                    vd.bitangent
                );

                float cos_d = clamp(dot(-dir, mat.normal), 0.0f, 1.0f);
                color *= (1.0f - ggx_fresnel(cos_d, mat))*volume_color;
            }
        }
    }

    return color;
}

vec3 refraction_path(
    vec3 start,
    vec3 h,
    vec3 dir,
    material mat,
    ivec3 environment_indices,
    float max_dist
) {
    vec3 color = vec3(0);
    vec3 tint = (1.0f - ggx_fresnel(clamp(dot(-dir, h), 0.0f, 1.0f), mat)) * mat.color.rgb;
    vec3 light_tint = pow(mat.color.rgb, vec3(2));
    float dist = max_dist;

    // Refractions SUCK to calculate...
    [[unroll]] for(int i = 0; i < 4; ++i)
    {
        rayQueryEXT rq;
        rayQueryInitializeEXT(
            rq, tlas, gl_RayFlagsOpaqueEXT, 5, start, 1e-5, dir, max_dist
        );

        rayQueryProceedEXT(rq);

        bool hit = rayQueryGetIntersectionTypeEXT(rq, true) != gl_RayQueryCommittedIntersectionNoneEXT;
        if(hit)
        {
            uint instance_id = rayQueryGetIntersectionInstanceCustomIndexEXT(rq, true);
            uint primitive_id = rayQueryGetIntersectionPrimitiveIndexEXT(rq, true);
            vec2 barycentrics = rayQueryGetIntersectionBarycentricsEXT(rq, true);
            bool front = rayQueryGetIntersectionFrontFaceEXT(rq, true);
            float t = rayQueryGetIntersectionTEXT(rq, true);
            instance i = instances.array[nonuniformEXT(instance_id)];
            vertex_data vd = get_vertex_data(instance_id, primitive_id, barycentrics);
            mat = sample_material(
                i.material,
                front,
                vd.uv.xy,
                vd.normal,
                vd.tangent,
                vd.bitangent
            );

            // RAYBOY HACK: Color the light slightly as it passes through the material.
            if(!front) tint *= pow(mat.color.rgb, vec3(t*500));

            vec3 shade;
            if(SECONDARY_SHADOWS == 0)
                shade = shade_point(vd.pos, -dir, vd.normal, i.environment_mesh.xyz, vd.uv.zw, mat);
            else
                shade = shade_point_rt(vd.pos, -dir, vd.normal, i.environment_mesh.xyz, vd.uv.zw, mat);

            color += light_tint * tint * shade;
            tint *= (1.0f - ggx_fresnel(clamp(dot(-dir, mat.normal), 0.0f, 1.0f), mat)) * mat.color.rgb * mat.transmittance;
            if(all(lessThan(tint, vec3(1e-4))))
                break;

            start = vd.pos;
            float ior_ratio = mat.ior_before/mat.ior_after;
            dir = refract(dir, mat.normal, ior_ratio);
        }
        else
        {
            color += tint * sample_cubemap(environment_indices.x, dir, 0);
            break;
        }
    }

    return color;
}

vec3 point_light_shadow(vec3 position, point_light pl, vec2 noise)
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
        vec2 ld_off = texelFetch(blue_noise, noise_pos, 0).xy + noise;

        [[unroll]] for(uint i = 0; i < SHADOW_RAY_COUNT; ++i)
        {
            vec2 off2d = fract(ld_samples[i] + ld_off);
            off2d = concentric_mapping(off2d);

            // This is probably not actually correct but idgaf
            vec3 off = tbn * vec3(off2d * radius, dist-radius);
            shadow += shadow_ray(position, position + off);
        }
    }
    return shadow/SHADOW_RAY_COUNT;
}

vec3 clamped_reflect(vec3 I, vec3 N)
{
    return 2.0f * max(dot(N, I), 0.0f) * N - I;
}

vec3 evaluate_reflection(
    vec3 pos,
    vec3 fallback_value,
    ivec3 environment_indices,
    vec3 view,
    in material mat,
    vec2 noise
){
    vec3 indirect_specular = fallback_value;

    if(REFLECTION_RAY_COUNT >= 1 && pc.disable_rt_reflection != 1)
    {
        indirect_specular = vec3(0);
        mat3 tbn = create_tangent_space(mat.normal);
        mat3 inv_tbn = transpose(tbn);
        ivec2 noise_pos = ivec2(mod(gl_FragCoord.xy, vec2(textureSize(blue_noise, 0))));
        vec2 ld_off = texelFetch(blue_noise, noise_pos, 0).xy + noise;
        vec3 tan_view = inv_tbn * view;
        [[unroll]] for(uint i = 0; i < REFLECTION_RAY_COUNT; ++i)
        {
            vec2 u = fract(ld_samples[i] + ld_off);
            vec3 dir = tbn * reflect(-tan_view, sample_ggx_vndf_tangent(tan_view, mat.roughness2, u));
            bool hit = false;
            vec3 refl_color = reflection_ray(pos, dir, 0.1, hit, mat.roughness*5.0);
            if(!hit)
                refl_color = sample_cubemap(environment_indices.x, dir, 0);
            // Yeah, the clamping is arbitrary. It removes some fireflies.
            indirect_specular += clamp(
                refl_color * clamp(ggx_vndf_attenuation(view, dir, mat), vec3(0), vec3(2)),
                vec3(0), vec3(5)
            );
        }
        indirect_specular /= REFLECTION_RAY_COUNT;
    }

    return indirect_specular;
}

vec3 evaluate_refraction(
    vec3 pos,
    vec3 fallback_value,
    ivec3 environment_indices,
    vec3 view,
    vec3 normal,
    in material mat,
    vec2 noise
){
    vec3 light = fallback_value;

    if(REFRACTION_RAY_COUNT >= 1 && pc.disable_rt_refraction != 1)
    {
        light = vec3(0);
        mat3 tbn = create_tangent_space(mat.normal);
        mat3 inv_tbn = transpose(tbn);
        ivec2 noise_pos = ivec2(mod(gl_FragCoord.xy, vec2(textureSize(blue_noise, 0))));
        vec2 ld_off = texelFetch(blue_noise, noise_pos, 0).xy + noise;
        vec3 tan_view = inv_tbn * view;
        float ior_ratio = mat.ior_before/mat.ior_after;
        [[unroll]] for(uint i = 0; i < REFRACTION_RAY_COUNT; ++i)
        {
            vec2 u = fract(ld_samples[i] + ld_off);
            vec3 h = tbn * sample_ggx_vndf_tangent(tan_view, mat.roughness2, u);
            vec3 dir = refract(-view, h, ior_ratio);
            vec3 refr_color = refraction_path(pos, h, dir, mat, environment_indices, 0.05);
            light += clamp(refr_color * (1.0f - ggx_vndf_attenuation(view, dir, mat)), vec3(0), vec3(5));
        }
        light /= REFRACTION_RAY_COUNT;
    }

    return light;
}

vec3 get_indirect_light_rt(
    vec3 pos,
    ivec3 environment_indices,
    vec3 normal,
    vec3 view,
    in material mat,
    vec2 lightmap_uv,
    vec2 noise
){
    vec3 diffuse_attenuation;
    vec3 specular_attenuation;
    brdf_indirect(
        view, mat, diffuse_attenuation, specular_attenuation
    );
    vec3 indirect_diffuse = vec3(0);
    vec3 indirect_specular = vec3(0);

    vec3 ref_dir = clamped_reflect(view, mat.normal);

    if(environment_indices.x != -1)
    {
        float lod = mat.roughness * float(textureQueryLevels(cube_textures[nonuniformEXT(environment_indices.x)])-1);
        indirect_specular = specular_attenuation * sample_cubemap(environment_indices.x, ref_dir, lod);
    }

    indirect_specular = evaluate_reflection(
        pos, indirect_specular, environment_indices, view, mat, noise
    );

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
