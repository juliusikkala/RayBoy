#ifndef RAYBOY_RENDER_TARGET_HH
#define RAYBOY_RENDER_TARGET_HH

#include "device.hh"
#include <vector>

class render_target
{
public:
    struct frame
    {
        VkImage image;
        VkImageView view;
        VkImageLayout layout;
    };

    render_target(const std::vector<frame>& init_frames);

    frame operator[](size_t index);

    // You can use this to indicate layout change without actually recording it
    // now. You can use the image_barrier() command to do it later. The old
    // layout is returned.
    VkImageLayout set_layout(VkImageLayout layout);

    // This one changes the layout and records now, only use this in render
    // stage constructors if you never update the command buffers afterwards.
    void transition_layout(VkCommandBuffer buf, size_t index, VkImageLayout layout);


private:
    std::vector<frame> frames;
};

#endif
