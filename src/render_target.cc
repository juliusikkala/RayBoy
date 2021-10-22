#include "render_target.hh"
#include "helpers.hh"


render_target::render_target(const std::vector<frame>& init_frames)
: frames(init_frames)
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

void render_target::transition_layout(VkCommandBuffer buf, size_t index, VkImageLayout layout)
{
    if(frames[index].layout == layout)
        return;

    image_barrier(buf, frames[index].image, frames[index].layout, layout);
    frames[index].layout = layout;
}
