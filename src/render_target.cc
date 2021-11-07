#include "render_target.hh"
#include "helpers.hh"


render_target::render_target(
    const std::vector<frame>& init_frames,
    uvec2 size,
    VkSampleCountFlagBits samples,
    VkFormat format
): size(size), samples(samples), format(format), frames(init_frames)
{
}

render_target::frame render_target::operator[](size_t index)
{
    return frames[index];
}

VkImageLayout render_target::set_layout(VkImageLayout layout)
{
    VkImageLayout old = frames[0].layout;
    for(frame f: frames)
        f.layout = layout;
    return old;
}

VkImageLayout render_target::get_layout() const
{
    return frames[0].layout;
}

void render_target::transition_layout(VkCommandBuffer buf, size_t index, VkImageLayout layout)
{
    if(frames[index].layout == layout)
        return;

    image_barrier(buf, frames[index].image, format, frames[index].layout, layout);
    frames[index].layout = layout;
}

uvec2 render_target::get_size() const
{
    return size;
}

VkSampleCountFlagBits render_target::get_samples() const
{
    return samples;
}

VkFormat render_target::get_format() const
{
    return format;
}

bool render_target::is_depth_stencil() const
{
    switch(format)
    {
    case VK_FORMAT_D16_UNORM:
    case VK_FORMAT_D16_UNORM_S8_UINT:
    case VK_FORMAT_D24_UNORM_S8_UINT:
    case VK_FORMAT_D32_SFLOAT:
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        return true;
    default:
        return false;
    }
}
