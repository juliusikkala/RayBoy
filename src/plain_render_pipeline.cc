#include "plain_render_pipeline.hh"

plain_render_pipeline::plain_render_pipeline(
    context& ctx,
    emulator& emu,
    const options& opt
): render_pipeline(ctx), opt(opt), emu(&emu)
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
    uvec2 emu_texture_size = emu->get_screen_size();
    if(opt.subpixels)
    {
        vec2 scales = vec2(ctx->get_size())/vec2(emu_texture_size);
        float scale = min(scales.x, scales.y);
        if(opt.integer_scaling && scale > 1.0f)
            scale = int(scale);
        emu_texture_size = ivec2(vec2(emu_texture_size) * scale);
    }

    render_target screen_target = ctx->get_render_target();
    color_buffer.reset(new texture(
        *ctx,
        emu_texture_size,
        VK_FORMAT_R8G8B8A8_UNORM,
        0, nullptr,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_STORAGE_BIT,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_SAMPLE_COUNT_1_BIT,
        VK_IMAGE_VIEW_TYPE_2D,
        true
    ));
    render_target color_target = color_buffer->get_render_target();

    // Remove old stages before calling new constructors.
    emulator_stage.reset();
    blit_stage.reset();
    gui_stage.reset();

    // Initialize rendering stages
    emulator_stage.reset(new emulator_render_stage(
        *ctx,
        *emu,
        color_target,
        false,
        opt.color_mapped,
        true
    ));
    blit_stage.reset(new blit_render_stage(
        *ctx, color_target, screen_target, false, opt.integer_scaling
    ));
    gui_stage.reset(new gui_render_stage(*ctx, screen_target));
}

VkSemaphore plain_render_pipeline::render_stages(VkSemaphore semaphore, uint32_t image_index)
{
    semaphore = emulator_stage->run(image_index, semaphore);
    semaphore = blit_stage->run(image_index, semaphore);
    semaphore = gui_stage->run(image_index, semaphore);
    return semaphore;
}
