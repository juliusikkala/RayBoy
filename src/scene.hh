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

// Tag component for nodes that are considered background. They can use worse
// rendering as lightmapping or other scams are expected.
struct background_entity {};
struct disable_rt_reflection {};

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

    void update(uint32_t image_index);
    void upload(VkCommandBuffer cmd, uint32_t image_index);

    ecs& get_ecs() const;

    std::vector<VkDescriptorSetLayoutBinding> get_bindings() const;
    std::vector<VkSpecializationMapEntry> get_specialization_entries() const;
    std::vector<uint32_t> get_specialization_data() const;

    void set_descriptors(gpu_pipeline& pipeline, uint32_t image_index) const;

    size_t get_instance_count() const;
    size_t get_point_light_count() const;
    size_t get_directional_light_count() const;
    bool is_instance_visible(size_t instance_id) const;
    bool is_instance_background(size_t instance_id) const;
    bool is_instance_rt_reflection_disabled(size_t instance_id) const;
    const material* get_instance_material(size_t instance_id) const;
    void draw_instance(VkCommandBuffer buf, size_t instance_id) const;

private:
    void init_rt();
    void upload_rt(VkCommandBuffer cmd, uint32_t image_index, bool full_refresh = false);
    int32_t get_st_index(material::sampler_tex st, uint32_t image_index);
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
    size_t rt_instance_count;
    VkDeviceAddress scratch_address;
    bool tlas_first_build;

    std::unordered_map<const mesh*, uint32_t> mesh_indices;
    std::unordered_map<material::sampler_tex, int32_t> st_pairs;
    std::unordered_map<const environment_map*, int32_t> envmap_indices;
    std::vector<VkImageView> textures;
    std::vector<VkSampler> samplers;
    std::vector<VkImageView> cubemap_textures;
    std::vector<VkSampler> cubemap_samplers;
    std::vector<VkBuffer> vertex_buffers;
    std::vector<VkBuffer> index_buffers;
    // TODO: This is getting silly. Maybe make a struct or something.
    std::vector<const mesh*> instance_meshes;
    std::vector<mat4> instance_transforms;
    std::vector<bool> instance_visible;
    std::vector<bool> instance_background;
    std::vector<bool> instance_rt_reflection_disabled;
    std::vector<const material*> instance_material;
    texture filler_texture;
    texture filler_cubemap;
    sampler filler_sampler;
    sampler radiance_sampler;
    sampler irradiance_sampler;
    vkres<VkBuffer> filler_buffer;
};

#endif
