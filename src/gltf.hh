#ifndef RAYBOY_GLTF_HH
#define RAYBOY_GLTF_HH
#include "model.hh"
#include "light.hh"
#include "camera.hh"
#include "mesh.hh"
#include "texture.hh"
#include "animation.hh"
#include "sampler.hh"
#include <string>
#include <unordered_map>
#include <memory>

struct gltf_data
{
    std::vector<std::unique_ptr<texture>> textures;
    std::vector<std::unique_ptr<sampler>> samplers;
    std::vector<std::unique_ptr<mesh>> meshes;
    std::vector<std::unique_ptr<animation_pool>> animation_pools;
    std::unordered_map<std::string, entity> entities;
    entity scene;

    void remove(ecs& e);
};

// Entity tag, used to mark all objects that don't export a custom property
// called "outer_layer". Used to skip rasterizing internals of the Rayboy. 
struct inner_node {};

gltf_data load_gltf(
    context& ctx,
    const std::string& path,
    ecs& entities,
    bool mark_inner = false // rayboy-specific visibility hack
);

#endif
