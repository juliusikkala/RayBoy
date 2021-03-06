#include "fancy_render_pipeline.hh"
#define PIXEL_SCALE 16u

fancy_render_pipeline::fancy_render_pipeline(
    context& ctx,
    ecs& entities,
    material* screen_material,
    emulator& emu,
    const options& opt
):  render_pipeline(ctx), entities(&entities), emu(&emu), opt(opt),
    screen_material(screen_material),
    gb_pixels(
        ctx,
        emu.get_screen_size()*PIXEL_SCALE,
        VK_FORMAT_R8G8B8A8_UNORM,
        0, nullptr,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_STORAGE_BIT|
        VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_SAMPLE_COUNT_1_BIT,
        VK_IMAGE_VIEW_TYPE_2D,
        true
    ),
    gb_pixel_sampler(ctx, VK_FILTER_LINEAR, VK_FILTER_LINEAR)
{
    screen_material->color_texture = {&gb_pixel_sampler, &gb_pixels};
    reset();
}

fancy_render_pipeline::~fancy_render_pipeline()
{
}

void fancy_render_pipeline::set_options(const options& opt)
{
    this->opt = opt;
}

void fancy_render_pipeline::reset()
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
    render_target gb_pixels_target = gb_pixels.get_render_target();
    emulator_stage.reset(new emulator_render_stage(
        *ctx, *emu, gb_pixels_target, true, true, false
    ));
    scene_update_stage.reset(new scene_update_render_stage(*ctx, *entities, opt.ray_tracing));
    forward_render_stage::options frs_opt = {
        opt.ray_tracing,
        opt.shadow_rays,
        opt.reflection_rays,
        opt.refraction_rays,
        opt.accumulation_ratio,
        opt.secondary_shadows
    };
    forward_stage.reset(new forward_render_stage(
        *ctx,
        &color_target,
        &depth_target,
        scene_update_stage->get_scene(),
        0,
        frs_opt
    ));
    tonemap_stage.reset(new tonemap_render_stage(
        *ctx,
        color_target,
        resolve_target,
        {1.0f, 0}
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

VkSemaphore fancy_render_pipeline::render_stages(VkSemaphore semaphore, uint32_t image_index)
{
    semaphore = emulator_stage->run(image_index, semaphore);
    semaphore = scene_update_stage->run(image_index, semaphore);
    semaphore = forward_stage->run(image_index, semaphore);
    semaphore = tonemap_stage->run(image_index, semaphore);
    if(blit_stage)
        semaphore = blit_stage->run(image_index, semaphore);
    semaphore = gui_stage->run(image_index, semaphore);
    return semaphore;
}

