#include "scene_update_render_stage.hh"
#include "scene.hh"

scene_update_render_stage::scene_update_render_stage(context& ctx, ecs& e, size_t max_entries)
: render_stage(ctx), e(&e), stage_timer(ctx, "scene_update_render_stage")
{
    scene_id = e.add(scene(ctx, e, max_entries));
}

scene_update_render_stage::~scene_update_render_stage()
{
    e->remove(scene_id);
}

void scene_update_render_stage::update_buffers(uint32_t image_index)
{
    if(e->get<scene>(scene_id)->update(image_index))
    {
        clear_commands();

        scene& s = *e->get<scene>(scene_id);
        for(size_t i = 0; i < ctx->get_image_count(); ++i)
        {
            // Record command buffers
            VkCommandBuffer cmd = graphics_commands();
            stage_timer.start(cmd, i);

            s.upload(cmd, i);

            stage_timer.stop(cmd, i);
            use_graphics_commands(cmd, i);
        }
    }
}
