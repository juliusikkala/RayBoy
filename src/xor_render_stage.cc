#include "xor_render_stage.hh"
#include "xor.comp.h"
#include "render_target.hh"

xor_render_stage::xor_render_stage(context& ctx, render_target& target)
: render_stage(ctx)
{
    init_bindings(
        ctx.get_image_count(), 
        {{0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}}
    );

    // Create pipeline
    init_compute_pipeline(sizeof(xor_shader_binary), xor_shader_binary);

    for(size_t i = 0; i < ctx.get_image_count(); ++i)
    {
        // Assign descriptors
        set_descriptor(i, 0, {target[i].view});

        // Record command buffer
        VkCommandBuffer buf = begin_compute_commands(i);

        target.transition_layout(buf, i, VK_IMAGE_LAYOUT_GENERAL);

        ivec2 size = ctx.get_size();
        vkCmdDispatch(buf, (size.x+7)/8, (size.y+7)/8, 1);

        target.transition_layout(buf, i, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        finish_compute_commands(buf);
    }
}
