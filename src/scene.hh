#ifndef RAYBOY_SCENE_HH
#define RAYBOY_SCENE_HH

#include "context.hh"
#include "gpu_buffer.hh"
#include "mesh.hh"
#include "material.hh"
#include "ecs.hh"

class scene_change_handler;

// This class is just a container for GPU assets concerning the entire scene;
// it's not to be used for organizing the scene itself. Just use the ECS.
class scene: public ptr_component
{
public:
    scene(context& ctx, ecs& e, size_t max_entries = 512);

    // Returns true if the buffers were reset.
    bool update(uint32_t image_index);
    void upload(VkCommandBuffer cmd, uint32_t image_index);

private:
    int32_t get_st_index(material::sampler_tex st);
    friend class scene_change_handler;

    ecs* e;
    size_t max_entries;
    gpu_buffer instances;
    gpu_buffer point_lights;
    gpu_buffer directional_lights;
    gpu_buffer cameras;
    std::unordered_map<const mesh*, uint32_t> mesh_indices;
    std::unordered_map<material::sampler_tex, int32_t> st_pairs;
};

#endif
