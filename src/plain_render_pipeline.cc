#include "plain_render_pipeline.hh"


plain_render_pipeline::plain_render_pipeline(context& ctx, ecs& entities)
: render_pipeline(ctx), entities(&entities)
{
    reset();
}

void plain_render_pipeline::reset()
{
    render_target target = ctx->get_render_target();
    scene_update_stage.reset(new scene_update_render_stage(*ctx, *entities));
    forward_stage.reset(new forward_render_stage(*ctx, target, scene_update_stage->get_scene(), 0));
    xor_stage.reset(new xor_render_stage(*ctx, target));
}

VkSemaphore plain_render_pipeline::render_stages(VkSemaphore semaphore, uint32_t image_index)
{
    semaphore = scene_update_stage->run(image_index, semaphore);
    semaphore = forward_stage->run(image_index, semaphore);
    //semaphore = xor_stage->run(image_index, semaphore);
    return semaphore;
}
