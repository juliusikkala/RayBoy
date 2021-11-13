#include "scene.hh"
#include "transformable.hh"
#include "model.hh"
#include "light.hh"
#include "camera.hh"
#include "helpers.hh"
#include "gltf.hh"
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
    uint32_t mesh;
    uint32_t pad[3];
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

struct gpu_scene_params
{
    uint32_t point_light_count;
    uint32_t directional_light_count;
};


}

scene::scene(context& ctx, ecs& e, size_t max_entries, size_t max_textures)
:   e(&e), max_entries(max_entries), max_textures(max_textures),
    instances(ctx, max_entries*sizeof(gpu_instance), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT),
    point_lights(ctx, max_entries*sizeof(gpu_point_light), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT),
    directional_lights(ctx, max_entries*sizeof(gpu_directional_light), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT),
    cameras(ctx, max_entries*sizeof(gpu_camera), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT),
    scene_params(ctx, sizeof(gpu_scene_params), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT),
    filler_texture(
        ctx, uvec2(1), VK_FORMAT_R8G8B8A8_UNORM, 0, nullptr,
        VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    ),
    filler_sampler(ctx),
    filler_buffer(create_gpu_buffer(ctx, 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT))
{
    for(uint32_t i = 0; i < ctx.get_image_count(); ++i)
        update(i);
}

void scene::update(uint32_t image_index)
{
    size_t instance_count = 0;
    e->foreach([&](entity id, model& m) { instance_count += m.group_count(); });
    assert(
        instance_count + e->count<point_light>() +
        e->count<spotlight>() + e->count<directional_light>() <= max_entries
    );

    mesh_indices.clear();
    st_pairs.clear();
    textures.clear();
    samplers.clear();
    vertex_buffers.clear();
    index_buffers.clear();
    instance_meshes.clear();
    instance_visible.clear();
 
    // Rayboy hack: inner parts of the console are marked as such, and won't
    // be rasterized! They're only visible with ray tracing!

    instances.update<gpu_instance>(image_index, [&](gpu_instance* data) {
        size_t i = 0;
        e->foreach([&](entity id, transformable& t, model& m, inner_node* inner) {
            mat4 mat = t.get_global_transform();
            mat4 inv = inverseTranspose(mat);
            for(const model::vertex_group& group: m)
            {
                gpu_instance& inst = data[i++];
                inst.model_to_world = mat;
                inst.normal_to_world = inv;
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
                    get_st_index(group.mat.color_texture, image_index),
                    get_st_index(group.mat.metallic_roughness_texture, image_index),
                    get_st_index(group.mat.normal_texture, image_index),
                    get_st_index(group.mat.emission_texture, image_index),
                };

                auto mesh_it = mesh_indices.find(group.mesh);
                if(mesh_it != mesh_indices.end())
                    inst.mesh = mesh_it->second;
                else
                {
                    inst.mesh = mesh_indices.size();
                    mesh_indices[group.mesh] = inst.mesh;
                    vertex_buffers.push_back(group.mesh->get_vertex_buffer());
                    index_buffers.push_back(group.mesh->get_index_buffer());
                }
                instance_meshes.push_back(group.mesh);
                instance_visible.push_back(inner == nullptr);
            }
        });
    });

    textures.resize(max_textures, filler_texture.get_image_view(image_index));
    samplers.resize(max_textures, filler_sampler.get());
    vertex_buffers.resize(max_entries, filler_buffer);
    index_buffers.resize(max_entries, filler_buffer);

    gpu_scene_params params = {0, 0};
    point_lights.update<gpu_point_light>(image_index, [&](gpu_point_light* data){
        size_t i = 0;
        e->foreach([&](entity id, transformable& t, point_light& l) {
            data[i++] = {
                vec4(l.get_color(), l.get_radius()),
                vec4(t.get_global_position(), 0),
                vec4(t.get_global_direction(), 0)
            };
            params.point_light_count++;
        });
        e->foreach([&](entity id, transformable& t, spotlight& l) {
            data[i++] = {
                vec4(l.get_color(), l.get_radius()),
                vec4(t.get_global_position(), l.get_falloff_exponent()),
                vec4(t.get_global_direction(), cos(radians(l.get_cutoff_angle())))
            };
            params.point_light_count++;
        });
    });

    directional_lights.update<gpu_directional_light>(image_index, [&](gpu_directional_light* data){
        size_t i = 0;
        e->foreach([&](entity id, transformable& t, directional_light& l) {
            data[i++] = {
                vec4(l.get_color(), 1),
                vec4(t.get_global_direction(), cos(radians(l.get_radius())))
            };
            params.directional_light_count++;
        });
    });

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
    scene_params.update(image_index, params);
}

void scene::upload(VkCommandBuffer cmd, uint32_t image_index)
{
    instances.upload(cmd, image_index);
    point_lights.upload(cmd, image_index);
    directional_lights.upload(cmd, image_index);
    cameras.upload(cmd, image_index);
    scene_params.upload(cmd, image_index);
}

ecs& scene::get_ecs() const
{
    return *e;
}

std::vector<VkDescriptorSetLayoutBinding> scene::get_bindings() const
{
    return {
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
        // Scene params
        {5, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_ALL, nullptr},
        // vertex buffers
        {6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, (uint32_t)max_entries, VK_SHADER_STAGE_ALL, nullptr},
        // index buffers
        {7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, (uint32_t)max_entries, VK_SHADER_STAGE_ALL, nullptr}
    };
}

void scene::set_descriptors(gpu_pipeline& pipeline, uint32_t image_index) const
{
    pipeline.set_descriptor(image_index, 0, {instances[image_index]});
    pipeline.set_descriptor(image_index, 1, {point_lights[image_index]});
    pipeline.set_descriptor(image_index, 2, {directional_lights[image_index]});
    pipeline.set_descriptor(image_index, 3, {cameras[image_index]});

    pipeline.set_descriptor(image_index, 4, textures, samplers);
    pipeline.set_descriptor(image_index, 5, {scene_params[image_index]});
    pipeline.set_descriptor(image_index, 6, vertex_buffers);
    pipeline.set_descriptor(image_index, 7, index_buffers);
}

size_t scene::get_instance_count() const
{
    return instance_meshes.size();
}

bool scene::is_instance_visible(size_t instance_id) const
{
    return instance_visible[instance_id];
}

void scene::draw_instance(VkCommandBuffer buf, size_t instance_id) const
{
    instance_meshes[instance_id]->draw(buf);
}

int32_t scene::get_st_index(material::sampler_tex st, uint32_t image_index)
{
    if(st.first == nullptr || st.second == nullptr)
        return -1;
    auto it = st_pairs.find(st);
    if(it == st_pairs.end())
    {
        int32_t index = st_pairs.size();
        st_pairs[st] = index;
        textures.push_back(st.second->get_image_view(image_index));
        samplers.push_back(st.first->get());
        return index;
    }
    else return it->second;
}
