#include "scene_update_render_stage.hh"
#include "scene.hh"

scene_update_render_stage::scene_update_render_stage(
    context& ctx,
    ecs& e,
    bool ray_tracing,
    size_t max_entries
): render_stage(ctx), e(&e), s(ctx, e, ray_tracing, max_entries), stage_timer(ctx, "scene_update_render_stage")
{
    for(size_t i = 0; i < ctx.get_image_count(); ++i)
    {
        // Record command buffers
        VkCommandBuffer cmd = graphics_commands();
        stage_timer.start(cmd, i);

        s.upload(cmd, i);

        stage_timer.stop(cmd, i);
        use_graphics_commands(cmd, i);
    }
}

scene_update_render_stage::~scene_update_render_stage()
{
}

const scene& scene_update_render_stage::get_scene() const
{
    return s;
}

void scene_update_render_stage::update_buffers(uint32_t image_index)
{
    s.update(image_index);
}
