#include "plain_render_pipeline.hh"

plain_render_pipeline::plain_render_pipeline(
    context& ctx,
    ecs& entities,
    const options& opt
): render_pipeline(ctx), entities(&entities), opt(opt)
{
    reset();
}

void plain_render_pipeline::set_options(const options& opt)
{
    this->opt = opt;
}

void plain_render_pipeline::reset()
{
    // Initialize buffers
    ivec2 render_resolution = ivec2(vec2(ctx->get_size()) * opt.resolution_scaling);

    render_target screen_target = ctx->get_render_target();
    color_buffer.reset(new texture(
        *ctx,
        render_resolution,
        VK_FORMAT_R16G16B16A16_SFLOAT,
        0, nullptr,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_STORAGE_BIT,
        VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR,
        opt.samples
    ));
    render_target color_target = color_buffer->get_render_target();
    depth_buffer.reset(new texture(
        *ctx,
        render_resolution,
        VK_FORMAT_D32_SFLOAT,
        0, nullptr,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR,
        opt.samples
    ));
    render_target depth_target = depth_buffer->get_render_target();

    render_target resolve_target = screen_target;
    if(render_resolution != ctx->get_size())
    {
        resolve_buffer.reset(new texture(
            *ctx,
            render_resolution,
            screen_target.get_format(),
            0, nullptr,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_SAMPLE_COUNT_1_BIT
        ));
        resolve_target = resolve_buffer->get_render_target();
    }
    else resolve_buffer.reset();

    // Remove old stages before calling new constructors.
    scene_update_stage.reset();
    forward_stage.reset();
    tonemap_stage.reset();
    blit_stage.reset();
    gui_stage.reset();

    // Initialize rendering stages
    scene_update_stage.reset(new scene_update_render_stage(*ctx, *entities));
    forward_stage.reset(new forward_render_stage(
        *ctx,
        &color_target,
        &depth_target,
        scene_update_stage->get_scene(),
        0
    ));
    tonemap_stage.reset(new tonemap_render_stage(
        *ctx,
        color_target,
        resolve_target
    ));
    if(render_resolution != ctx->get_size())
    {
        blit_stage.reset(new blit_render_stage(
            *ctx, resolve_target, screen_target
        ));
        gui_stage.reset(new gui_render_stage(*ctx, screen_target));
    }
    else
    {
        gui_stage.reset(new gui_render_stage(*ctx, resolve_target));
    }
}

VkSemaphore plain_render_pipeline::render_stages(VkSemaphore semaphore, uint32_t image_index)
{
    semaphore = scene_update_stage->run(image_index, semaphore);
    semaphore = forward_stage->run(image_index, semaphore);
    semaphore = tonemap_stage->run(image_index, semaphore);
    if(blit_stage)
        semaphore = blit_stage->run(image_index, semaphore);
    semaphore = gui_stage->run(image_index, semaphore);
    return semaphore;
}
