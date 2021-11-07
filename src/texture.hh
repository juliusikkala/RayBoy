#ifndef RAYBOY_TEXTURE_HH
#define RAYBOY_TEXTURE_HH
#include "context.hh"
#include "render_target.hh"

class texture
{
public:
    texture(context& ctx, const std::string& path);
    texture(
        context& ctx,
        uvec2 size,
        VkFormat format,
        size_t data_size = 0,
        void* data = nullptr,
        VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL,
        VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
        VkImageLayout layout = VK_IMAGE_LAYOUT_GENERAL,
        VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT
    );
    texture(const context& other) = delete;
    texture(texture&& other) = default;

    VkImageView get_image_view(uint32_t image_index) const;
    VkImage get_image(uint32_t image_index) const;
    render_target get_render_target() const;

    VkFormat get_format() const;
    VkSampleCountFlagBits get_samples() const;

    void set_opaque(bool opaque);
    bool potentially_transparent() const;

    uvec2 get_size() const;

private:
    void load_from_file(const std::string& path);
    void load_from_data(size_t data_size, void* data);

    context* ctx;

    uvec2 size;
    std::vector<vkres<VkImage>> images;
    std::vector<vkres<VkImageView>> views;
    VkFormat format;
    VkImageTiling tiling;
    VkImageUsageFlags usage;
    VkImageLayout layout;
    VkSampleCountFlagBits samples;
    bool opaque;
};

#endif
