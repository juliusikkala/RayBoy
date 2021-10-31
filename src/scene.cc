#include "scene.hh"
#include "transformable.hh"
#include "model.hh"
#include "light.hh"
#include "camera.hh"
#include <cassert>

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
    gpu_material material;
    alignas(4*sizeof(float)) uint32_t mesh;
};

struct gpu_camera
{
    mat4 view_proj;
    mat4 view_inv, proj_inv;
    vec4 origin;
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

scene::scene(context& ctx, ecs& e, size_t max_entries)
:   e(&e), max_entries(max_entries),
    instances(ctx, 0, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT),
    point_lights(ctx, 0, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT),
    directional_lights(ctx, 0, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT),
    cameras(ctx, 0, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
{
}

bool scene::update(uint32_t image_index)
{
    size_t instance_count = 0;
    e->foreach([&](entity id, model& m) { instance_count += m.group_count(); });
    assert(
        instance_count + e->count<point_light>() +
        e->count<spotlight>() + e->count<directional_light>() <= max_entries
    );

    mesh_indices.clear();
    st_pairs.clear();

    bool reallocated = false;
    reallocated |= instances.resize(instance_count * sizeof(gpu_instance));
    instances.update<gpu_instance>(image_index, [&](gpu_instance* data) {
        size_t i = 0;
        e->foreach([&](entity id, transformable& t, model& m) {
            for(const model::vertex_group& group: m)
            {
                gpu_instance& inst = data[i++];
                inst.model_to_world = t.get_global_transform();
                inst.normal_to_world = inverseTranspose(inst.model_to_world);
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
                    get_st_index(group.mat.color_texture),
                    get_st_index(group.mat.metallic_roughness_texture),
                    get_st_index(group.mat.normal_texture),
                    get_st_index(group.mat.emission_texture),
                };

                auto mesh_it = mesh_indices.find(group.mesh);
                if(mesh_it != mesh_indices.end())
                    inst.mesh = mesh_it->second;
                else
                {
                    inst.mesh = mesh_indices.size();
                    mesh_indices[group.mesh] = inst.mesh;
                }
            }
        });
    });

    reallocated |= point_lights.resize(
        (e->count<point_light>() + e->count<spotlight>()) * sizeof(gpu_point_light)
    );
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

    reallocated |= directional_lights.resize(
        e->count<directional_light>() * sizeof(gpu_directional_light)
    );
    directional_lights.update<gpu_directional_light>(image_index, [&](gpu_directional_light* data){
        size_t i = 0;
        e->foreach([&](entity id, transformable& t, directional_light& l) {
            data[i++] = {
                vec4(l.get_color(), 1),
                vec4(t.get_global_direction(), cos(radians(l.get_radius())))
            };
        });
    });

    reallocated |= cameras.resize(e->count<camera>() * sizeof(gpu_camera));
    cameras.update<gpu_camera>(image_index, [&](gpu_camera* data) {
        size_t i = 0;
        e->foreach([&](entity id, transformable& t, camera& c) {
            mat4 view_inv = t.get_global_transform();
            mat4 view = inverse(view_inv);
            mat4 proj = c.get_projection();
            mat4 proj_inv = inverse(proj);

            data[i++] = {
                proj * view,
                view_inv,
                proj_inv,
                view_inv[3]
            };
        });
    });
    return reallocated;
}

void scene::upload(VkCommandBuffer cmd, uint32_t image_index)
{
    instances.upload(cmd, image_index);
    point_lights.upload(cmd, image_index);
    directional_lights.upload(cmd, image_index);
    cameras.upload(cmd, image_index);
}

int32_t scene::get_st_index(material::sampler_tex st)
{
    if(st.first == nullptr || st.second == nullptr)
        return -1;
    auto it = st_pairs.find(st);
    if(it == st_pairs.end())
    {
        int32_t index = st_pairs.size();
        st_pairs[st] = index;
        return index;
    }
    else return it->second;
}
