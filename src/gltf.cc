#include "gltf.hh"
#include "tiny_gltf.h"
#include "helpers.hh"
#include "error.hh"
#include "stb_image.h"
#include "scene.hh"
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <algorithm>

void gltf_data::remove(ecs& e)
{
    for(const auto& pair: entities)
        e.remove(pair.second);
    textures.clear();
    samplers.clear();
    meshes.clear();
    animation_pools.clear();
    entities.clear();
}

namespace
{

void flip_lines(unsigned pitch, unsigned char* line_a, unsigned char* line_b)
{
    for(unsigned i = 0; i < pitch; ++i)
    {
        unsigned char tmp = line_a[i];
        line_a[i] = line_b[i];
        line_b[i] = tmp;
    }
}

void flip_vector_image(std::vector<unsigned char>& image, unsigned height)
{
    unsigned pitch = image.size() / height;
    for(unsigned i = 0; i < height/2; ++i)
        flip_lines(
            pitch,
            image.data()+i*pitch,
            image.data()+(height-1-i)*pitch
        );
}

bool check_opaque(tinygltf::Image& img)
{
    if(img.component != 4) return true;
    if(img.bits == 8)
    {
        // Check that every fourth (alpha) value is 255.
        for(size_t i = 3; i < img.image.size(); i += 4)
            if(img.image[i] != 255)
                return false;
        return true;
    }
    return false;
}

template<typename T>
vec4 vector_to_vec4(const std::vector<T>& v, float fill_value = 0.0f)
{
    vec4 ret(fill_value);
    for(size_t i = 0; i < std::min(4llu, v.size()); ++i)
        ret[i] = v[i];
    return ret;
}

template<typename T>
void cast_gltf_data(uint8_t* data, int componentType, int, T& out)
{
    switch(componentType)
    {
    case TINYGLTF_COMPONENT_TYPE_BYTE:
        out = *reinterpret_cast<int8_t*>(data);
        break;
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
        out = *reinterpret_cast<uint8_t*>(data);
        break;
    case TINYGLTF_COMPONENT_TYPE_SHORT:
        out = *reinterpret_cast<int16_t*>(data);
        break;
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
        out = *reinterpret_cast<uint16_t*>(data);
        break;
    case TINYGLTF_COMPONENT_TYPE_INT:
        out = *reinterpret_cast<int32_t*>(data);
        break;
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
        out = *reinterpret_cast<uint32_t*>(data);
        break;
    case TINYGLTF_COMPONENT_TYPE_FLOAT:
        out = *reinterpret_cast<float*>(data);
        break;
    case TINYGLTF_COMPONENT_TYPE_DOUBLE:
        out = *reinterpret_cast<double*>(data);
        break;
    }
}

template<int size, typename T>
void cast_gltf_data(
    uint8_t* data, int componentType, int type, vec<size, T>& out
){
    int component_size = tinygltf::GetComponentSizeInBytes(componentType);
    int components = tinygltf::GetNumComponentsInType(type);
    out = glm::vec<size, T>(0);
    for(int i = 0; i < min(components, (int)size); ++i)
        cast_gltf_data(data+i*component_size, componentType, type, out[i]);
}

template<int C, int R, typename T>
void cast_gltf_data(
    uint8_t* data, int componentType, int type, mat<C, R, T>& out
){
    int component_size = tinygltf::GetComponentSizeInBytes(componentType);
    int components = tinygltf::GetNumComponentsInType(type);
    out = glm::mat<C, R, T>(1);
    for(int x = 0; x < C; ++x)
    for(int y = 0; y < R; ++y)
    {
        int i = y+x*C;
        if(i < components)
            cast_gltf_data(
                data+i*component_size, componentType, type, out[x][y]
            );
    }
}

void cast_gltf_data(
    uint8_t* data, int componentType, int type, quat& out
){
    vec4 tmp;
    cast_gltf_data(data, componentType, type, tmp);
    out = quat(tmp.w, tmp.x, tmp.y, tmp.z);
}

template<typename T>
std::vector<T> read_accessor(tinygltf::Model& model, int index)
{
    tinygltf::Accessor& accessor = model.accessors[index];
    tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
    tinygltf::Buffer& buf = model.buffers[view.buffer];

    std::vector<T> res(accessor.count);
    int stride = accessor.ByteStride(view);

    size_t offset = view.byteOffset;
    for(T& entry: res)
    {
        uint8_t* data = buf.data.data() + offset;
        cast_gltf_data(data, accessor.componentType, accessor.type, entry);
        offset += stride;
    }
    return res;
}

template<typename T>
std::vector<animation::sample<T>> read_animation_accessors(
    tinygltf::Model& model, int input, int output
){
    std::vector<animation::sample<T>> res;

    std::vector<float> timestamps = read_accessor<float>(model, input);
    std::vector<T> data = read_accessor<T>(model, output);

    bool has_tangents = data.size() >= 3*timestamps.size();
    res.resize(timestamps.size());
    for(size_t i = 0; i < res.size(); ++i)
    {
        // Convert timestamps into microseconds
        res[i].timestamp = round(timestamps[i]*1000000);
        if(has_tangents)
        {
            res[i].in_tangent = data[i*3];
            res[i].data = data[i*3+1];
            res[i].out_tangent = data[i*3+2];
        }
        else res[i].data = data[i];
    }

    return res;
}

material::sampler_tex get_texture(tinygltf::Model& model, gltf_data& md, int index)
{
    if(index == -1) return {nullptr, nullptr};
    int sampler_index = model.textures[index].sampler;
    sampler* s = md.samplers.back().get();
    if(sampler_index >= 0)
        md.samplers[sampler_index].get();
    return {s, md.textures[model.textures[index].source].get() };
}

material create_material(
    tinygltf::Material& mat,
    tinygltf::Model& model,
    gltf_data& md
){
    material m;
    m.color_factor = vector_to_vec4(mat.pbrMetallicRoughness.baseColorFactor);

    m.color_texture = get_texture(model, md, mat.pbrMetallicRoughness.baseColorTexture.index);

    m.metallic_factor = mat.pbrMetallicRoughness.metallicFactor;
    m.roughness_factor = mat.pbrMetallicRoughness.roughnessFactor;

    m.metallic_roughness_texture = get_texture(
        model, md, mat.pbrMetallicRoughness.metallicRoughnessTexture.index
    );

    m.normal_factor = mat.normalTexture.scale;
    m.normal_texture = get_texture(model, md, mat.normalTexture.index);

    m.ior = 1.45f;

    m.emission_factor = vector_to_vec4(mat.emissiveFactor);
    m.emission_texture = get_texture(model, md, mat.emissiveTexture.index);

    if(mat.extensions.count("KHR_materials_transmission"))
    {
        m.transmittance = mat.extensions["KHR_materials_transmission"].Get("transmissionFactor").Get<double>();
    }

    return m;
}

struct node_meta_info
{
    std::unordered_map<int /*node*/, animation_pool*> animations;
    std::unordered_map<std::string /*name*/, model> models;
};

void load_gltf_node(
    ecs& entities,
    tinygltf::Model& gltf_model,
    tinygltf::Scene& scene,
    int node_index,
    gltf_data& data,
    transformable* parent,
    node_meta_info& meta
){
    entity id = entities.add(transformable());
    tinygltf::Node& node = gltf_model.nodes[node_index];
    data.entities[node.name] = id;

    transformable* tnode = entities.get<transformable>(id);

    // Set transformation for node
    if(node.matrix.size())
        tnode->set_transform(glm::make_mat4(node.matrix.data()));
    else
    {
        if(node.translation.size())
            tnode->set_position((vec3)glm::make_vec3(node.translation.data()));

        if(node.scale.size())
            tnode->set_scaling((vec3)glm::make_vec3(node.scale.data()));

        if(node.rotation.size())
            tnode->set_orientation(glm::make_quat(node.rotation.data()));
    }
    tnode->set_parent(parent);

    auto it = meta.animations.find(node_index);
    if(it != meta.animations.end())
    {
        entities.attach(id, animated(it->second));
    }

    if(node.mesh != -1)
    {
        entities.attach(
            id,
            model(meta.models.at(gltf_model.meshes[node.mesh].name))
        );
        entities.attach(id, visible{});
    }

    if(node.camera != -1)
    {
        camera cam;
        tinygltf::Camera& c = gltf_model.cameras[node.camera];

        if(c.type == "perspective")
        {
            cam.perspective(
                glm::degrees(c.perspective.yfov), c.perspective.aspectRatio,
                c.perspective.znear, c.perspective.zfar
            );
        }
        else if(c.type == "orthographic")
            cam.ortho(
                -0.5*c.orthographic.xmag, 0.5*c.orthographic.xmag,
                -0.5*c.orthographic.ymag, 0.5*c.orthographic.ymag,
                c.orthographic.znear, c.orthographic.zfar
            );

        entities.attach(id, std::move(cam));
    }

    // Add light, if present.
    if(node.extensions.count("KHR_lights_punctual"))
    {
        tinygltf::Light& l = gltf_model.lights[
            node.extensions["KHR_lights_punctual"].Get("light").Get<int>()
        ];

        // Apparently Blender's gltf exporter is broken in terms of light
        // intensity as of writing this, so the multipliers here are just magic
        // numbers. Fix this when this issue is solved:
        // https://github.com/KhronosGroup/glTF-Blender-IO/issues/564
        vec3 color(vector_to_vec4(l.color) * (float)l.intensity);
        if(l.type == "directional")
            entities.attach(id, directional_light(color));
        else if(l.type == "point")
            entities.attach(id, point_light(color*float(0.25/M_PI)));
        else if(l.type == "spot")
        {
            spotlight sl(
                color*float(0.25/M_PI),
                glm::degrees(l.spot.outerConeAngle)
            );
            sl.set_inner_angle(glm::degrees(l.spot.innerConeAngle), 4/255.0f);
            entities.attach(id, std::move(sl));
        }
    }

    bool outer = node.extras.Has("outer_layer");
    if(outer) entities.attach(id, outer_layer{});
    entities.attach(id, gltf_name{node.name});

    // Load child nodes
    for(int child_index: node.children)
        load_gltf_node(
            entities, gltf_model, scene, child_index, data, tnode, meta
        );
}

}

gltf_data load_gltf(
    context& ctx,
    const std::string& path,
    ecs& entities
){
    gltf_data md;

    std::string err, warn;
    tinygltf::Model gltf_model;
    tinygltf::TinyGLTF loader;

    // TinyGLTF uses stb_image too, and expects this value.
    stbi_set_flip_vertically_on_load(true);

    if(!loader.LoadBinaryFromFile(
        &gltf_model, &err, &warn, path
    )) throw std::runtime_error(err);

    for(tinygltf::Image& image: gltf_model.images)
    {
        if(image.bufferView != -1)
        {// Embedded image
            VkFormat format;

            switch(image.component)
            {
            case 1:
                format = image.bits == 8 ? VK_FORMAT_R8_UNORM : VK_FORMAT_R16_UNORM;
                break;
            case 2:
                format = image.bits == 8 ? VK_FORMAT_R8G8_UNORM : VK_FORMAT_R16G16_UNORM;
                break;
            default:
            case 3:
                format = image.bits == 8 ? VK_FORMAT_R8G8B8_UNORM : VK_FORMAT_R16G16B16_UNORM;
                break;
            case 4:
                format = image.bits == 8 ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R16G16B16A16_UNORM;
                break;
            }

            flip_vector_image(image.image, image.height);
            if(image.component == 3 && image.bits == 8)
            {
                check_error(
                    image.pixel_type != TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE,
                    "16-bit channel interlacing not implemented yet"
                );
                std::vector<uint8_t> new_image(image.image.size()/3*4, 0);
                uint8_t fill = 255;
                interlace(
                    new_image.data(),
                    image.image.data(),
                    &fill,
                    3, 4,
                    image.width*image.height
                );
                image.image = std::move(new_image);
                format = VK_FORMAT_R8G8B8A8_UNORM;
            }

            md.textures.emplace_back(new texture(
                ctx,
                uvec2(image.width, image.height),
                format,
                image.image.size(),
                image.image.data(),
                VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_SAMPLE_COUNT_1_BIT,
                VK_IMAGE_VIEW_TYPE_2D,
                true
            ));

            if(check_opaque(image))
                md.textures.back()->set_opaque(true);
        }
        else
        {// URI
            md.textures.emplace_back(new texture(ctx, image.uri));
        }
    }

    for(tinygltf::Sampler& gltf_sampler: gltf_model.samplers)
    {
        VkFilter min = VK_FILTER_LINEAR;
        VkFilter mag = VK_FILTER_LINEAR;
        VkSamplerMipmapMode mipmap_mode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        VkSamplerAddressMode extension = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        bool use_mipmaps = true;

        switch(gltf_sampler.minFilter)
        {
        case TINYGLTF_TEXTURE_FILTER_NEAREST:
            min = VK_FILTER_NEAREST;
            use_mipmaps = false;
            break;
        case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST:
            min = VK_FILTER_NEAREST;
            use_mipmaps = true;
            mipmap_mode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            break;
        case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR:
            min = VK_FILTER_NEAREST;
            use_mipmaps = true;
            mipmap_mode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            break;
        case TINYGLTF_TEXTURE_FILTER_LINEAR:
            min = VK_FILTER_LINEAR;
            use_mipmaps = false;
            break;
        case TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST:
            min = VK_FILTER_LINEAR;
            use_mipmaps = true;
            mipmap_mode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            break;
        case TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR:
            min = VK_FILTER_LINEAR;
            use_mipmaps = true;
            mipmap_mode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            break;
        default:
            break;
        }

        switch(gltf_sampler.magFilter)
        {
        case TINYGLTF_TEXTURE_FILTER_NEAREST:
            mag = VK_FILTER_NEAREST;
            break;
        case TINYGLTF_TEXTURE_FILTER_LINEAR:
            mag = VK_FILTER_LINEAR;
            break;
        default:
            break;
        }

        switch(gltf_sampler.wrapS)
        {
        case TINYGLTF_TEXTURE_WRAP_CLAMP_TO_EDGE:
            extension = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            break;
        case TINYGLTF_TEXTURE_WRAP_MIRRORED_REPEAT:
            extension = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
            break;
        case TINYGLTF_TEXTURE_WRAP_REPEAT:
            extension = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            break;
        default:
            break;
        }
        md.samplers.emplace_back(new sampler(
            ctx,
            min,
            mag,
            mipmap_mode,
            extension,
            16,
            use_mipmaps ? 100.0f : 0.0f
        ));
    }
    md.samplers.emplace_back(new sampler(ctx));

    node_meta_info meta;
    for(tinygltf::Mesh& gltf_mesh: gltf_model.meshes)
    {
        model m;

        for(tinygltf::Primitive& p: gltf_mesh.primitives)
        {
            std::vector<vec3> position;
            std::vector<vec3> normal;
            std::vector<vec2> texcoord;
            std::vector<vec2> lm_texcoord;
            std::vector<vec4> tangent;

            for(const auto& pair: p.attributes)
            {
                if(pair.first == "POSITION")
                    position = read_accessor<vec3>(gltf_model, pair.second);
                else if(pair.first == "NORMAL")
                    normal = read_accessor<vec3>(gltf_model, pair.second);
                else if(pair.first == "TEXCOORD_0")
                    texcoord = read_accessor<vec2>(gltf_model, pair.second);
                else if(pair.first == "TEXCOORD_1")
                    lm_texcoord = read_accessor<vec2>(gltf_model, pair.second);
                else if(pair.first == "TANGENT")
                    tangent = read_accessor<vec4>(gltf_model, pair.second);
            }

            normal.resize(position.size());
            texcoord.resize(position.size());
            lm_texcoord.resize(position.size());
            tangent.resize(position.size());

            std::vector<mesh::vertex> vertices(position.size());
            for(size_t i = 0; i < position.size(); ++i)
                vertices[i] = {
                    pvec4(position[i], 0),
                    pvec4(normal[i], 0),
                    pvec4(texcoord[i], lm_texcoord[i].x, 1.0-lm_texcoord[i].y),
                    tangent[i]
                };

            material mat;
            if(p.material >= 0)
                mat = create_material(
                    gltf_model.materials[p.material], gltf_model, md
                );

            // If missing tangent data and we have a normal texture, show a
            // warning.
            if(mat.normal_texture.second && !p.attributes.count("TANGENT"))
            {
                std::cerr
                    << path << ": " << gltf_mesh.name
                    << " has a normal map but doesn't have tangents!"
                    << std::endl;
            }

            md.meshes.emplace_back(new mesh(
                ctx,
                std::move(vertices),
                read_accessor<uint32_t>(gltf_model, p.indices),
                !mat.potentially_transparent()
            ));

            m.add_vertex_group(mat, md.meshes.back().get());
        }

        meta.models[gltf_mesh.name] = m;
    }

    // Add animations
    for(tinygltf::Animation& anim: gltf_model.animations)
    {
        for(tinygltf::AnimationChannel& chan: anim.channels)
        {
            auto it = meta.animations.find(chan.target_node);
            if(it == meta.animations.end())
                it = meta.animations.emplace(
                    chan.target_node,
                    md.animation_pools.emplace_back(new animation_pool()).get()
                ).first;

            animation& res = (*it->second)[anim.name];
            tinygltf::AnimationSampler& sampler = anim.samplers[chan.sampler];

            animation::interpolation interp = animation::LINEAR;
            if(sampler.interpolation == "LINEAR") interp = animation::LINEAR;
            else if(sampler.interpolation == "STEP") interp = animation::STEP;
            else if(sampler.interpolation == "CUBICSPLINE")
                interp = animation::CUBICSPLINE;

            if(chan.target_path == "translation")
                res.set_position(
                    interp,
                    read_animation_accessors<vec3>(
                        gltf_model, sampler.input, sampler.output
                    )
                );
            else if(chan.target_path == "rotation")
                res.set_orientation(
                    interp,
                    read_animation_accessors<quat>(
                        gltf_model, sampler.input, sampler.output
                    )
                );
            else if(chan.target_path == "scale")
                res.set_scaling(
                    interp,
                    read_animation_accessors<vec3>(
                        gltf_model, sampler.input, sampler.output
                    )
                );
            // Unknown target type (probably weights for morph targets)
            else continue;
        }
    }

    // Add objects & cameras
    for(tinygltf::Scene& scene: gltf_model.scenes)
    {
        for(int node_index: scene.nodes)
            load_gltf_node(
                entities, gltf_model, scene, node_index, md, nullptr, meta
            );
    }

    return md;
}
