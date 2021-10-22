#include "texture.hh"
#include "helpers.hh"
#include "stb_image.h"

texture::texture(context& ctx, const std::string& path)
: ctx(&ctx)
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

VkImageView texture::get_image_view() const { return *view; }

VkImage texture::get_image() const { return *image; }
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
            float* fdata = (float*)data;
            size_t new_data_size = size.x * size.y * 4 * sizeof(float);
            float* new_data = (float*)malloc(new_data_size);
            for(size_t i = 0; i < size.x * size.y; ++i)
            {
                new_data[i*4+0] = fdata[i*3+0];
                new_data[i*4+1] = fdata[i*3+1];
                new_data[i*4+2] = fdata[i*3+2];
                new_data[i*4+3] = 1.0;
            }
            free(data);
            data = new_data;
            data_size = new_data_size;
        }
        else
        {
            uint8_t* udata = (uint8_t*)data;
            size_t new_data_size = size.x * size.y * 4 * sizeof(uint8_t);
            uint8_t* new_data = (uint8_t*)malloc(new_data_size);
            for(size_t i = 0; i < size.x * size.y; ++i)
            {
                new_data[i*4+0] = udata[i*3+0];
                new_data[i*4+1] = udata[i*3+1];
                new_data[i*4+2] = udata[i*3+2];
                new_data[i*4+3] = 255;
            }
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
    layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    samples = VK_SAMPLE_COUNT_1_BIT;

    image = create_gpu_image(
        *ctx, size, format, layout, samples,
        tiling, usage, data_size, data, true
    );

    stbi_image_free(data);
    view = create_image_view(*ctx, image, format, VK_IMAGE_ASPECT_COLOR_BIT);
}

void texture::load_from_data(size_t data_size, void* data)
{
    image = create_gpu_image(
        *ctx, size, format, layout, samples, tiling,
        usage, data_size, data, false
    );
    view = create_image_view(*ctx, image, format, VK_IMAGE_ASPECT_COLOR_BIT);
}
