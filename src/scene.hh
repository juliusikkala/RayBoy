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

// This class is just a container for GPU assets concerning the entire scene;
// it's not to be used for organizing the scene itself. Just use the ECS.
class scene
{
public:
    scene(context& ctx, ecs& e, size_t max_entries = 512, size_t max_textures = 2048);

    void update(uint32_t image_index);
    void upload(VkCommandBuffer cmd, uint32_t image_index);

    ecs& get_ecs() const;

    std::vector<VkDescriptorSetLayoutBinding> get_bindings() const;
    void set_descriptors(gpu_pipeline& pipeline, uint32_t image_index) const;

    size_t get_instance_count() const;
    void draw_instance(VkCommandBuffer buf, size_t instance_id) const;

private:
    int32_t get_st_index(material::sampler_tex st);
    friend class scene_change_handler;

    ecs* e;
    size_t max_entries, max_textures;
    gpu_buffer instances;
    gpu_buffer point_lights;
    gpu_buffer directional_lights;
    gpu_buffer cameras;
    std::unordered_map<const mesh*, uint32_t> mesh_indices;
    std::unordered_map<material::sampler_tex, int32_t> st_pairs;
    std::vector<VkImageView> textures;
    std::vector<VkSampler> samplers;
    std::vector<VkBuffer> vertex_buffers;
    std::vector<VkBuffer> index_buffers;
    std::vector<const mesh*> instance_meshes;
    texture filler_texture;
    sampler filler_sampler;
    vkres<VkBuffer> filler_buffer;
};

#endif
