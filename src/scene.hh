#ifndef RAYBOY_SCENE_HH
#define RAYBOY_SCENE_HH

#include "context.hh"
#include "gpu_buffer.hh"
#include "gpu_pipeline.hh"
#include "mesh.hh"
#include "material.hh"
#include "texture.hh"
#include "sampler.hh"
#include "ecs.hh"

class scene_change_handler;
class environment_map;

// Only objects with the 'ray_traced' component are included in the acceleration
// structure. Additionally, specific ray tracing effects can be enabled or
// disabled per-entity.
struct ray_traced
{
    bool shadow = true;
    bool reflection = true;
    bool refraction = true;
};

// Only entities with the 'visible' component are rendered.
struct visible {};

// This class is just a container for GPU assets concerning the entire scene;
// it's not to be used for organizing the scene itself. Just use the ECS.
class scene
{
public:
    scene(
        context& ctx,
        ecs& e,
        bool ray_tracing,
        size_t max_entries = 512,
        size_t max_textures = 256
    );

    // If update returns false, you have to re-set descriptors with
    // refresh_descriptors() and set_descriptors().
    bool update(uint32_t image_index);
    void upload(VkCommandBuffer cmd, uint32_t image_index);

    ecs& get_ecs() const;

    std::vector<VkDescriptorSetLayoutBinding> get_bindings() const;
    std::vector<VkSpecializationMapEntry> get_specialization_entries() const;
    std::vector<uint32_t> get_specialization_data() const;

    void refresh_descriptors(uint32_t image_index);
    void set_descriptors(gpu_pipeline& pipeline, uint32_t image_index) const;

    size_t get_point_light_count() const;
    size_t get_directional_light_count() const;
    int32_t get_entity_instance_id(entity id, uint32_t vg_index) const;

private:
    void init_rt();
    void upload_rt(VkCommandBuffer cmd, uint32_t image_index, bool full_refresh = false);
    int32_t get_st_index(material::sampler_tex st, bool& outdated) const;
    friend class scene_change_handler;

    context* ctx;
    ecs* e;
    size_t max_entries, max_textures;
    bool ray_tracing;
    gpu_buffer instances;
    gpu_buffer point_lights;
    gpu_buffer directional_lights;
    gpu_buffer cameras;

    vkres<VkAccelerationStructureKHR> tlas;
    vkres<VkBuffer> tlas_buffer;
    vkres<VkBuffer> tlas_scratch;
    gpu_buffer rt_instances;
    size_t instance_count;
    size_t rt_instance_count;
    VkDeviceAddress scratch_address;
    bool tlas_first_build;

    std::unordered_map<const mesh*, uint32_t> mesh_indices;
    std::unordered_map<material::sampler_tex, int32_t> st_pairs;
    std::unordered_map<const environment_map*, int32_t> envmap_indices;
    std::unordered_map<entity, std::vector<uint32_t>> entity_instances;
    std::unordered_map<entity, mat4> old_mvps;

    struct descriptor_info
    {
        std::vector<VkImageView> textures;
        std::vector<VkSampler> samplers;
        std::vector<VkImageView> cubemap_textures;
        std::vector<VkSampler> cubemap_samplers;
        std::vector<VkBuffer> vertex_buffers;
        std::vector<VkBuffer> index_buffers;
    };
    std::vector<descriptor_info> ds_info;

    texture filler_texture;
    texture filler_cubemap;
    sampler filler_sampler;
    sampler radiance_sampler;
    sampler irradiance_sampler;
    vkres<VkBuffer> filler_buffer;
};

#endif
