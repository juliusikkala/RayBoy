#include "blit_render_stage.hh"

blit_render_stage::blit_render_stage(
    context& ctx,
    render_target& src,
    render_target& dst,
    bool stretch,
    bool integer_scaling
): render_stage(ctx), stage_timer(ctx, "blit_render_stage")
{
    for(size_t i = 0; i < ctx.get_image_count(); ++i)
    {
        // Record command buffers
        VkCommandBuffer cmd = graphics_commands();
        stage_timer.start(cmd, i);

        src.transition_layout(cmd, i, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        dst.transition_layout(cmd, i, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        ivec2 output_pos = ivec2(0);
        ivec2 input_size = src.get_size();
        ivec2 output_size = dst.get_size();

        if(!stretch)
        {
            ivec2 center = ivec2(dst.get_size())/2;
            VkClearColorValue color = {0.0f, 0.0f, 0.0f, 1.0f};
            VkImageSubresourceRange range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            vkCmdClearColorImage(cmd, dst[i].image, dst[i].layout, &color, 1, &range);

            vec2 scales = vec2(output_size)/vec2(input_size);
            float scale = min(scales.x, scales.y);
            if(integer_scaling && scale > 1.0f)
                scale = int(scale);
            output_size = ivec2(vec2(input_size) * scale);

            output_pos = center - output_size/2;
        }

        VkImageBlit blit = {
            {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
            {{0,0,0}, {(int32_t)src.get_size().x, (int32_t)src.get_size().y, 1}},
            {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
            {{output_pos.x,output_pos.y,0}, {output_size.x+output_pos.x, output_size.y+output_pos.y, 1}}
        };
        vkCmdBlitImage(
            cmd,
            src[i].image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            dst[i].image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &blit,
            !stretch && integer_scaling ? VK_FILTER_NEAREST : VK_FILTER_LINEAR
        );

        dst.transition_layout(cmd, i, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

        stage_timer.stop(cmd, i);
        use_graphics_commands(cmd, i);
    }
}
