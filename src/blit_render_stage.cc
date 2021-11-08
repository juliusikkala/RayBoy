#include "blit_render_stage.hh"

blit_render_stage::blit_render_stage(context& ctx, render_target& src, render_target& dst)
: render_stage(ctx), stage_timer(ctx, "blit_render_stage")
{
    for(size_t i = 0; i < ctx.get_image_count(); ++i)
    {
        // Record command buffers
        VkCommandBuffer cmd = graphics_commands();
        stage_timer.start(cmd, i);

        src.transition_layout(cmd, i, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        dst.transition_layout(cmd, i, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        VkImageBlit blit = {
            {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
            {{0,0,0}, {(int32_t)src.get_size().x, (int32_t)src.get_size().y, 1}},
            {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
            {{0,0,0}, {(int32_t)dst.get_size().x, (int32_t)dst.get_size().y, 1}}
        };
        vkCmdBlitImage(
            cmd,
            src[i].image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            dst[i].image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &blit,
            VK_FILTER_LINEAR
        );

        dst.transition_layout(cmd, i, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

        stage_timer.stop(cmd, i);
        use_graphics_commands(cmd, i);
    }
}
