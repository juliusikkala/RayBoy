#include "scene.hh"
#include "transformable.hh"
#include "model.hh"
#include "light.hh"
#include "camera.hh"
#include "helpers.hh"
#include "environment_map.hh"
#include "gltf.hh"
#include "error.hh"
#include <initializer_list>

namespace
{

struct gpu_material
{
    // xyz = color, w = alpha
    vec4 color_factor;
    // x = metallic, y = roughness, z = normal, w = ior
    vec4 metallic_roughness_normal_ior_factors;
    // xyz = emission intensity, w = transmittance
    vec4 emission_transmittance_factors;
    // x = color + alpha, y = metallic+roughness, z = normal, w = emission
    ivec4 textures;
};

struct gpu_instance
{
    mat4 model_to_world;
    mat4 normal_to_world;
    mat4 prev_mvp;
    gpu_material material;
    // x = radiance index, y = irradiance index, z = lightmap index, w = mesh index
    ivec4 environment_mesh;
};

struct gpu_camera
{
    mat4 view_proj;
    mat4 view;
    vec4 projection_info;
    vec4 clip_info;
    vec4 origin;
    vec4 noise;
};

struct gpu_point_light
{
    // xyz = color, w = radius (soft shadows)
    vec4 color_radius;
    // xyz = position in world space, w = falloff exponent
    vec4 pos_falloff;
    // xyz = direction in world space, w = cutoff angle in radians
    vec4 direction_cutoff;
};

struct gpu_directional_light
{
    // xyz = color, w = unused.
    vec4 color;
    // xyz = In world space, w = cos(solid_angle).
    vec4 direction;
};

}

scene::scene(context& ctx, ecs& e, bool ray_tracing, size_t max_entries, size_t max_textures)
:   ctx(&ctx), e(&e), max_entries(max_entries), max_textures(max_textures),
    ray_tracing(ray_tracing),
    instances(ctx, max_entries*sizeof(gpu_instance), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT),
    point_lights(ctx, max_entries*sizeof(gpu_point_light), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT),
    directional_lights(ctx, max_entries*sizeof(gpu_directional_light), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT),
    cameras(ctx, max_entries*sizeof(gpu_camera), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT),
    tlas(ctx), tlas_buffer(ctx), tlas_scratch(ctx),
    rt_instances(
        ctx,
        0,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT|VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT|
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
    ), tlas_first_build(true),
    filler_texture(
        ctx, uvec2(1), VK_FORMAT_R8G8B8A8_UNORM, 0, nullptr,
        VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    ),
    filler_cubemap(
        ctx, uvec2(1), VK_FORMAT_R8G8B8A8_UNORM, 0, nullptr,
        VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_SAMPLE_COUNT_1_BIT,
        VK_IMAGE_VIEW_TYPE_CUBE
    ),
    filler_sampler(ctx),
    radiance_sampler(ctx, VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT, 1),
    irradiance_sampler(ctx, VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT, 1, 0.0f, 0.0f),
    filler_buffer(create_gpu_buffer(ctx, 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT))
{
    if(ray_tracing)
        init_rt();
    ds_info.resize(ctx.get_image_count());
    for(uint32_t i = 0; i < ctx.get_image_count(); ++i)
        refresh_descriptors(i);
}

bool scene::update(uint32_t image_index)
{
    size_t instance_count = 0;
    bool outdated = false;

    e->foreach([&](entity id, model& m) { instance_count += m.group_count(); });
    check_error(
        instance_count + e->count<point_light>() +
        e->count<spotlight>() + e->count<directional_light>() > max_entries,
        "Too many entities in scene!"
    );

    mat4 vp = mat4(1);
    cameras.update<gpu_camera>(image_index, [&](gpu_camera* data) {
        size_t i = 0;
        e->foreach([&](entity id, transformable& t, camera& c) {
            mat4 view_inv = t.get_global_transform();
            mat4 view = inverse(view_inv);
            mat4 proj = c.get_projection();
            mat4 proj_inv = inverse(proj);

            if(i == 0) vp = proj * view;

            data[i++] = {
                proj * view,
                view,
                vec4(c.get_projection_info(), 0, 0),
                vec4(c.get_clip_info(), 0),
                view_inv[3],
                linearRand(vec4(0), vec4(1))
            };
        });
    });

    instances.update<gpu_instance>(image_index, [&](gpu_instance* data) {
        size_t i = 0;
        e->foreach([&](entity id, transformable& t, model& m, visible&) {
            mat4 mat = t.get_global_transform();
            mat4 inv = inverseTranspose(mat);
            std::vector<uint32_t>& instances = entity_instances[id];
            instances.clear();
            mat4 prev_mvp = mat4(NAN);
            if(old_mvps.count(id))
                prev_mvp = old_mvps[id];
            old_mvps[id] = vp * mat;

            for(const model::vertex_group& group: m)
            {
                instances.push_back(i);
                gpu_instance& inst = data[i++];
                inst.model_to_world = mat;
                inst.normal_to_world = inv;
                inst.prev_mvp = prev_mvp;
                inst.material.color_factor = group.mat.color_factor;
                inst.material.metallic_roughness_normal_ior_factors = vec4(
                    group.mat.metallic_factor,
                    group.mat.roughness_factor,
                    group.mat.normal_factor,
                    group.mat.ior
                );
                inst.material.emission_transmittance_factors = vec4(
                    group.mat.emission_factor,
                    group.mat.transmittance
                );
                inst.material.textures = {
                    get_st_index(group.mat.color_texture, outdated),
                    get_st_index(group.mat.metallic_roughness_texture, outdated),
                    get_st_index(group.mat.normal_texture, outdated),
                    get_st_index(group.mat.emission_texture, outdated),
                };
                inst.environment_mesh = ivec4(-1);
                if(group.mat.envmap != nullptr)
                {
                    auto eit = envmap_indices.find(group.mat.envmap);
                    if(eit == envmap_indices.end())
                    {
                        outdated = true;
                        return;
                    }
                    inst.environment_mesh.x = eit->second;
                    inst.environment_mesh.y = eit->second+1;
                }
                inst.environment_mesh.z = get_st_index(group.mat.lightmap, outdated);

                auto mesh_it = mesh_indices.find(group.mesh);
                if(mesh_it == mesh_indices.end())
                {
                    outdated = true;
                    return;
                }
                inst.environment_mesh.w = mesh_it->second;
            }
        });
    });

    point_lights.update<gpu_point_light>(image_index, [&](gpu_point_light* data){
        size_t i = 0;
        e->foreach([&](entity id, transformable& t, point_light& l) {
            data[i++] = {
                vec4(l.get_color(), l.get_radius()),
                vec4(t.get_global_position(), 0),
                vec4(t.get_global_direction(), 0)
            };
        });
        e->foreach([&](entity id, transformable& t, spotlight& l) {
            data[i++] = {
                vec4(l.get_color(), l.get_radius()),
                vec4(t.get_global_position(), l.get_falloff_exponent()),
                vec4(t.get_global_direction(), cos(radians(l.get_cutoff_angle())))
            };
        });
    });

    directional_lights.update<gpu_directional_light>(image_index, [&](gpu_directional_light* data){
        size_t i = 0;
        e->foreach([&](entity id, transformable& t, directional_light& l) {
            data[i++] = {
                vec4(l.get_color(), 1),
                vec4(t.get_global_direction(), cos(radians(l.get_radius())))
            };
        });
    });

    if(ray_tracing)
    {
        rt_instances.update<VkAccelerationStructureInstanceKHR>(image_index, [&](VkAccelerationStructureInstanceKHR* data) {
            rt_instance_count = 0;
            size_t i = 0;
            e->foreach([&](entity id, transformable& t, model& m, visible&, ray_traced* rt) {
                mat4 transform = transpose(t.get_global_transform());
                for(const model::vertex_group& group: m)
                {
                    if(rt)
                    {
                        data[rt_instance_count] = {
                            {}, (uint32_t)i, group.mat.potentially_transparent() ? 2u : 1u, 0,
                            VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR,
                            group.mesh->get_blas_address()
                        };
                        memcpy(
                            &data[rt_instance_count].transform, &transform,
                            sizeof(data[rt_instance_count].transform)
                        );
                        rt_instance_count++;
                    }
                    i++;
                }
            });
        });
    }
    return !outdated;
}

void scene::upload(VkCommandBuffer cmd, uint32_t image_index)
{
    instances.upload(cmd, image_index);
    point_lights.upload(cmd, image_index);
    directional_lights.upload(cmd, image_index);
    cameras.upload(cmd, image_index);

    if(ray_tracing)
    {
        if(tlas_first_build)
        {
            VkCommandBuffer cmd = begin_command_buffer(*ctx);
            upload_rt(cmd, image_index, true);
            end_command_buffer(*ctx, cmd);
            tlas_first_build = false;
        }
        upload_rt(cmd, image_index, false);
    }
}

ecs& scene::get_ecs() const
{
    return *e;
}

std::vector<VkDescriptorSetLayoutBinding> scene::get_bindings() const
{
    std::vector<VkDescriptorSetLayoutBinding> bindings = {
        // instances
        {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr},
        // point_lights
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr},
        // directional_lights
        {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr},
        // cameras
        {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr},
        // textures
        {4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, (uint32_t)max_textures, VK_SHADER_STAGE_ALL, nullptr},
        // Cubemap textures
        {5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, (uint32_t)max_textures, VK_SHADER_STAGE_ALL, nullptr},
    };

    if(ray_tracing)
    {
        // TLAS
        bindings.push_back(
            {6, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, VK_SHADER_STAGE_ALL, nullptr}
        );
        // vertex buffers
        bindings.push_back(
            {7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, (uint32_t)max_entries, VK_SHADER_STAGE_ALL, nullptr}
        );
        // index buffers
        bindings.push_back(
            {8, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, (uint32_t)max_entries, VK_SHADER_STAGE_ALL, nullptr}
        );
    }

    return bindings;
}

std::vector<VkSpecializationMapEntry> scene::get_specialization_entries() const
{
    return {
        {0, 0, sizeof(uint32_t)},
        {1, sizeof(uint32_t), sizeof(uint32_t)}
    };
}
std::vector<uint32_t> scene::get_specialization_data() const
{
    return {(uint32_t)get_point_light_count(), (uint32_t)get_directional_light_count()};
}

void scene::refresh_descriptors(uint32_t image_index)
{
    std::vector<VkImageView>& textures = ds_info[image_index].textures;
    std::vector<VkSampler>& samplers = ds_info[image_index].samplers;
    std::vector<VkImageView>& cubemap_textures = ds_info[image_index].cubemap_textures;
    std::vector<VkSampler>& cubemap_samplers = ds_info[image_index].cubemap_samplers;
    std::vector<VkBuffer>& vertex_buffers = ds_info[image_index].vertex_buffers;
    std::vector<VkBuffer>& index_buffers = ds_info[image_index].index_buffers;

    mesh_indices.clear();
    st_pairs.clear();
    envmap_indices.clear();

    textures.clear();
    samplers.clear();
    cubemap_textures.clear();
    cubemap_samplers.clear();
    vertex_buffers.clear();
    index_buffers.clear();

    // Add cubemap textures
    e->foreach([&](entity id, environment_map& e) {
        envmap_indices[&e] = cubemap_textures.size();
        cubemap_textures.push_back(e.get_radiance()->get_image_view(image_index));
        cubemap_textures.push_back(e.get_irradiance()->get_image_view(image_index));
        cubemap_samplers.push_back(radiance_sampler.get());
        cubemap_samplers.push_back(irradiance_sampler.get());
    });

    e->foreach([&](entity id, transformable& t, model& m) {
        for(const model::vertex_group& group: m)
        {
            for(const material::sampler_tex& st: {
                group.mat.color_texture,
                group.mat.metallic_roughness_texture,
                group.mat.normal_texture,
                group.mat.emission_texture,
                group.mat.lightmap
            }) {
                if(st.first == nullptr || st.second == nullptr)
                    continue;
                auto it = st_pairs.find(st);
                if(it == st_pairs.end())
                {
                    st_pairs[st] = st_pairs.size();
                    textures.push_back(st.second->get_image_view(image_index));
                    samplers.push_back(st.first->get());
                }
            }

            auto mesh_it = mesh_indices.find(group.mesh);
            if(mesh_it == mesh_indices.end())
            {
                mesh_indices[group.mesh] = vertex_buffers.size();
                vertex_buffers.push_back(group.mesh->get_vertex_buffer());
                index_buffers.push_back(group.mesh->get_index_buffer());
            }
        }
    });
    textures.resize(max_textures, filler_texture.get_image_view(image_index));
    samplers.resize(max_textures, filler_sampler.get());
    cubemap_textures.resize(max_textures, filler_cubemap.get_image_view(image_index));
    cubemap_samplers.resize(max_textures, filler_sampler.get());
    vertex_buffers.resize(max_entries, *filler_buffer);
    index_buffers.resize(max_entries, *filler_buffer);
}

void scene::set_descriptors(gpu_pipeline& pipeline, uint32_t image_index) const
{
    const std::vector<VkImageView>& textures = ds_info[image_index].textures;
    const std::vector<VkSampler>& samplers = ds_info[image_index].samplers;
    const std::vector<VkImageView>& cubemap_textures = ds_info[image_index].cubemap_textures;
    const std::vector<VkSampler>& cubemap_samplers = ds_info[image_index].cubemap_samplers;
    const std::vector<VkBuffer>& vertex_buffers = ds_info[image_index].vertex_buffers;
    const std::vector<VkBuffer>& index_buffers = ds_info[image_index].index_buffers;

    pipeline.set_descriptor(image_index, 0, {instances[image_index]});
    pipeline.set_descriptor(image_index, 1, {point_lights[image_index]});
    pipeline.set_descriptor(image_index, 2, {directional_lights[image_index]});
    pipeline.set_descriptor(image_index, 3, {cameras[image_index]});

    pipeline.set_descriptor(image_index, 4, textures, samplers);
    pipeline.set_descriptor(image_index, 5, cubemap_textures, cubemap_samplers);

    if(ray_tracing)
    {
        pipeline.set_descriptor(image_index, 6, *tlas);
        pipeline.set_descriptor(image_index, 7, vertex_buffers);
        pipeline.set_descriptor(image_index, 8, index_buffers);
    }
}

size_t scene::get_point_light_count() const
{
    return e->count<point_light>() + e->count<spotlight>();
}

size_t scene::get_directional_light_count() const
{
    return e->count<directional_light>();
}

int32_t scene::get_entity_instance_id(entity id, uint32_t vg_index) const
{
    auto it = entity_instances.find(id);
    if(it == entity_instances.end()) return -1;
    const std::vector<uint32_t>& instances = it->second;

    if(vg_index >= instances.size()) return -1;
    return instances[vg_index];
}

void scene::init_rt()
{
    rt_instances.resize(max_entries * sizeof(VkAccelerationStructureInstanceKHR));

    VkAccelerationStructureGeometryKHR as_geom = {
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
        nullptr,
        VK_GEOMETRY_TYPE_INSTANCES_KHR,
        {},
        0
    };
    as_geom.geometry.instances = {
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
        nullptr,
        VK_FALSE,
        VkDeviceOrHostAddressConstKHR{rt_instances.get_device_address(0)}
    };

    VkAccelerationStructureBuildGeometryInfoKHR as_build_info = {
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
        nullptr,
        VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
        VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR|
        VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR,
        VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
        VK_NULL_HANDLE,
        VK_NULL_HANDLE,
        1,
        &as_geom,
        nullptr,
        0
    };

    VkAccelerationStructureBuildSizesInfoKHR build_size = {
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR,
        nullptr
    };
    uint32_t max_primitive_count = max_entries;
    vkGetAccelerationStructureBuildSizesKHR(
        ctx->get_device().logical_device,
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &as_build_info,
        &max_primitive_count,
        &build_size
    );

    tlas_buffer = create_gpu_buffer(
        *ctx,
        build_size.accelerationStructureSize,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR|
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
    );

    VkAccelerationStructureCreateInfoKHR as_create_info = {
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
        nullptr,
        0,
        tlas_buffer,
        0,
        build_size.accelerationStructureSize,
        VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
        0
    };

    VkAccelerationStructureKHR as_tmp;
    vkCreateAccelerationStructureKHR(ctx->get_device().logical_device, &as_create_info, nullptr, &as_tmp);
    tlas = vkres(*ctx, as_tmp);

    uint32_t alignment = ctx->get_device().as_properties.minAccelerationStructureScratchOffsetAlignment;
    vkres<VkBuffer> scratch_buffer = create_gpu_buffer(
        *ctx,
        build_size.buildScratchSize + alignment,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
    );
    VkBufferDeviceAddressInfo scratch_info = {
        VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        nullptr,
        scratch_buffer
    };
    scratch_address = vkGetBufferDeviceAddress(
        ctx->get_device().logical_device,
        &scratch_info
    ) + alignment - (build_size.buildScratchSize % alignment);
}

void scene::upload_rt(VkCommandBuffer cmd, uint32_t image_index, bool full_refresh)
{
    rt_instances.upload(cmd, image_index);

    VkMemoryBarrier2KHR barriers[] = {
        {
            VK_STRUCTURE_TYPE_MEMORY_BARRIER_2_KHR,
            nullptr,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
            VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
            VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR
        },
        {
            VK_STRUCTURE_TYPE_MEMORY_BARRIER_2_KHR,
            nullptr,
            VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
            VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR
        }
    };
    VkDependencyInfoKHR deps = {
        VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR, nullptr, 0,
        2, barriers, 0, nullptr, 0, nullptr
    };
    vkCmdPipelineBarrier2KHR(cmd, &deps);

    VkAccelerationStructureGeometryKHR as_geom = {
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
        nullptr,
        VK_GEOMETRY_TYPE_INSTANCES_KHR,
        {},
        VK_GEOMETRY_OPAQUE_BIT_KHR
    };
    as_geom.geometry.instances = {
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
        nullptr,
        VK_FALSE,
        VkDeviceOrHostAddressConstKHR{rt_instances.get_device_address(0)}
    };

    VkAccelerationStructureBuildGeometryInfoKHR as_build_info = {
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
        nullptr,
        VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
        VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR|
        VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR,
        full_refresh ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR : VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR,
        full_refresh ? VK_NULL_HANDLE : *tlas,
        tlas,
        1,
        &as_geom,
        nullptr,
        scratch_address
    };

    VkAccelerationStructureBuildRangeInfoKHR range = {
        (uint32_t)rt_instance_count, 0, 0, 0
    };
    VkAccelerationStructureBuildRangeInfoKHR* range_ptr = &range;

    vkCmdBuildAccelerationStructuresKHR(cmd, 1, &as_build_info, &range_ptr);
}

int32_t scene::get_st_index(material::sampler_tex st, bool& outdated) const
{
    if(st.first == nullptr || st.second == nullptr)
        return -1;
    auto it = st_pairs.find(st);
    if(it == st_pairs.end())
    {
        outdated = true;
        return -1;
    }
    else return it->second;
}
