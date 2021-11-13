#include "texture.hh"
#include "helpers.hh"
#include "stb_image.h"

texture::texture(context& ctx, const std::string& path, VkImageLayout layout)
: ctx(&ctx), layout(layout)
{
    load_from_file(path);
}

texture::texture(
    context& ctx,
    uvec2 size,
    VkFormat format,
    size_t data_size,
    void* data,
    VkImageTiling tiling,
    VkImageUsageFlags usage,
    VkImageLayout layout,
    VkSampleCountFlagBits samples
):  ctx(&ctx), size(size), format(format), tiling(tiling), usage(usage),
    layout(layout), samples(samples), opaque(false)
{
    load_from_data(data_size, data);
}

VkImageView texture::get_image_view(uint32_t image_index) const
{
    return *views[min((size_t)image_index, views.size()-1)];
}

VkImage texture::get_image(uint32_t image_index) const
{
    return *images[min((size_t)image_index, views.size()-1)];
}

render_target texture::get_render_target() const
{
    assert((usage&(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)) != 0);

    std::vector<render_target::frame> frames(images.size());

    for(size_t i = 0; i < images.size(); ++i)
        frames[i] = {*images[i], *views[i], layout};

    return render_target(
        frames,
        size,
        samples,
        format
    );
}

VkFormat texture::get_format() const { return format; }
VkSampleCountFlagBits texture::get_samples() const { return samples; }

void texture::set_opaque(bool opaque) { this->opaque = opaque; }
bool texture::potentially_transparent() const
{
    switch(format)
    {
    case VK_FORMAT_R8G8B8A8_UNORM:
    case VK_FORMAT_B8G8R8A8_UNORM:
    case VK_FORMAT_R16G16B16A16_UNORM:
    case VK_FORMAT_R16G16B16A16_SFLOAT:
    case VK_FORMAT_R32G32B32A32_SFLOAT:
    case VK_FORMAT_R64G64B64A64_SFLOAT:
        return !opaque;
    default:
        return false;
    }
}

uvec2 texture::get_size() const { return size; }

void texture::load_from_file(const std::string& path)
{
    bool hdr = stbi_is_hdr(path.c_str());
    void* data = nullptr;
    size_t data_size = 0;
    int channels = 0;
    if(hdr)
    {
        data = stbi_loadf(
            path.c_str(), (int*)&size.x, (int*)&size.y, &channels, 0
        );
        data_size = size.x * size.y * channels * sizeof(float);
    }
    else
    {
        data = stbi_load(
            path.c_str(), (int*)&size.x, (int*)&size.y, &channels, 0
        );
        data_size = size.x * size.y * channels * sizeof(uint8_t);
    }
    assert(data && "Failed to load image");
    opaque = channels < 4;

    // Vulkan implementations don't really support 3-channel textures...
    if(channels == 3)
    {
        if(hdr)
        {
            size_t new_data_size = size.x * size.y * 4 * sizeof(float);
            float* new_data = (float*)malloc(new_data_size);
            float fill = 1.0;
            interlace(new_data, data, &fill, 3 * sizeof(float), 4 * sizeof(float), size.x*size.y);
            free(data);
            data = new_data;
            data_size = new_data_size;
        }
        else
        {
            size_t new_data_size = size.x * size.y * 4 * sizeof(uint8_t);
            uint8_t* new_data = (uint8_t*)malloc(new_data_size);
            uint8_t fill = 255;
            interlace(new_data, data, &fill, 3 * sizeof(uint8_t), 4 * sizeof(uint8_t), size.x*size.y);
            free(data);
            data = new_data;
            data_size = new_data_size;
        }
        channels = 4;
    }

    switch(channels)
    {
    default:
    case 1:
        format = hdr ? VK_FORMAT_R32_SFLOAT : VK_FORMAT_R8_UNORM;
        break;
    case 2:
        format = hdr ? VK_FORMAT_R32G32_SFLOAT : VK_FORMAT_R8G8_UNORM;
        break;
    case 3:
        format = hdr ? VK_FORMAT_R32G32B32_SFLOAT : VK_FORMAT_R8G8B8_UNORM;
        break;
    case 4:
        format = hdr ? VK_FORMAT_R32G32B32A32_SFLOAT : VK_FORMAT_R8G8B8A8_UNORM;
        break;
    }

    tiling = VK_IMAGE_TILING_OPTIMAL;
    usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    if(layout == VK_IMAGE_LAYOUT_GENERAL)
        usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    samples = VK_SAMPLE_COUNT_1_BIT;

    images.emplace_back(create_gpu_image(
        *ctx, size, format, layout, samples,
        tiling, usage, data_size, data, true
    ));

    stbi_image_free(data);
    views.emplace_back(create_image_view(*ctx, images[0], format, VK_IMAGE_ASPECT_COLOR_BIT));
}

void texture::load_from_data(size_t data_size, void* data)
{
    size_t count = 1;
    if((usage&(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)) != 0)
    {
        count = ctx->get_image_count();
    }

    for(size_t i = 0; i < count; ++i)
    {
        images.emplace_back(create_gpu_image(
            *ctx, size, format, layout, samples, tiling, usage, data_size, data,
            (layout&VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        ));
        views.emplace_back(create_image_view(*ctx, images[i], format, deduce_image_aspect_flags(format)));
    }
}
