#include "plain_render_pipeline.hh"


plain_render_pipeline::plain_render_pipeline(context& ctx, ecs& entities)
: render_pipeline(ctx), entities(&entities)
{
    reset();
}

void plain_render_pipeline::reset()
{
    render_target color_target = ctx->get_render_target();
    depth_buffer.reset(new texture(
        *ctx,
        ctx->get_size(),
        VK_FORMAT_D32_SFLOAT,
        0, nullptr,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR,
        VK_SAMPLE_COUNT_1_BIT
    ));
    render_target depth_target = depth_buffer->get_render_target();
    scene_update_stage.reset(new scene_update_render_stage(*ctx, *entities));
    forward_stage.reset(new forward_render_stage(
        *ctx,
        &color_target,
        &depth_target,
        scene_update_stage->get_scene(),
        0
    ));
}

VkSemaphore plain_render_pipeline::render_stages(VkSemaphore semaphore, uint32_t image_index)
{
    semaphore = scene_update_stage->run(image_index, semaphore);
    semaphore = forward_stage->run(image_index, semaphore);
    return semaphore;
}
