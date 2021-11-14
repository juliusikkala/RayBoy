#include "texture.hh"
#include "helpers.hh"
#include "stb_image.h"
#include "ktxvulkan.h"
#include "io.hh"
#include "error.hh"

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
):  ctx(&ctx), dim(size, 1), format(format), tiling(tiling),
    usage(usage), layout(layout), samples(samples), opaque(false)
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
    check_error(
        (usage&(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)) == 0,
        "Cannot get render target for this texture due to incorrect usage flags!"
    );

    std::vector<render_target::frame> frames(images.size());

    for(size_t i = 0; i < images.size(); ++i)
        frames[i] = {*images[i], *views[i], layout};

    return render_target(
        frames,
        dim,
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

uvec2 texture::get_size() const { return dim; }
uvec3 texture::get_dim() const { return dim; }

void texture::load_from_file(const std::string& path)
{
    fs::path p(path);
    if(p.extension() == ".ktx")
    {
        load_from_ktx(path);
    }
    else load_from_stb(path);
}

void texture::load_from_ktx(const std::string& path)
{
    ktxTexture* kTexture;
    KTX_error_code result;
    ktxVulkanDeviceInfo vdi;
    ktxVulkanTexture texture;

    const device& dev = ctx->get_device();

    ktxVulkanDeviceInfo_Construct(
        &vdi, dev.physical_device, dev.logical_device, dev.graphics_queue,
        dev.graphics_pool, nullptr
    );

    result = ktxTexture_CreateFromNamedFile(
        path.c_str(),
        KTX_TEXTURE_CREATE_NO_FLAGS,
        &kTexture
    );
    check_error(result != KTX_SUCCESS, "Failed to load image %s", path.c_str());

    tiling = VK_IMAGE_TILING_OPTIMAL;
    usage = VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_STORAGE_BIT;
    result = ktxTexture_VkUploadEx(
        kTexture, &vdi, &texture, tiling, usage, layout
    );
    check_error(
        result != KTX_SUCCESS,
        "Failed to load image %s to Vulkan",
        path.c_str()
    );

    ktxTexture_Destroy(kTexture);
    ktxVulkanDeviceInfo_Destruct(&vdi);

    format = texture.imageFormat;
    samples = VK_SAMPLE_COUNT_1_BIT;
    opaque = true; // TODO
    dim = uvec3(texture.width, texture.height, texture.depth);

    images.emplace_back(*ctx, texture.image, texture.deviceMemory);
    views.emplace_back(create_image_view(
        *ctx, images[0], format, VK_IMAGE_ASPECT_COLOR_BIT, texture.viewType
    ));
}

void texture::load_from_stb(const std::string& path)
{
    bool hdr = stbi_is_hdr(path.c_str());
    void* data = nullptr;
    size_t data_size = 0;
    int channels = 0;
    dim.z = 1;
    if(hdr)
    {
        data = stbi_loadf(
            path.c_str(), (int*)&dim.x, (int*)&dim.y, &channels, 0
        );
        data_size = dim.x * dim.y * channels * sizeof(float);
    }
    else
    {
        data = stbi_load(
            path.c_str(), (int*)&dim.x, (int*)&dim.y, &channels, 0
        );
        data_size = dim.x * dim.y * channels * sizeof(uint8_t);
    }
    check_error(!data, "Failed to load image %s", path.c_str());
    opaque = channels < 4;

    // Vulkan implementations don't really support 3-channel textures...
    if(channels == 3)
    {
        if(hdr)
        {
            size_t new_data_size = dim.x * dim.y * 4 * sizeof(float);
            float* new_data = (float*)malloc(new_data_size);
            float fill = 1.0;
            interlace(new_data, data, &fill, 3 * sizeof(float), 4 * sizeof(float), dim.x*dim.y);
            free(data);
            data = new_data;
            data_size = new_data_size;
        }
        else
        {
            size_t new_data_size = dim.x * dim.y * 4 * sizeof(uint8_t);
            uint8_t* new_data = (uint8_t*)malloc(new_data_size);
            uint8_t fill = 255;
            interlace(new_data, data, &fill, 3 * sizeof(uint8_t), 4 * sizeof(uint8_t), dim.x*dim.y);
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
        *ctx, uvec2(dim), format, layout, samples,
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
            *ctx, dim, format, layout, samples, tiling, usage, data_size, data,
            (layout&VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        ));
        views.emplace_back(create_image_view(*ctx, images[i], format, deduce_image_aspect_flags(format)));
    }
}
