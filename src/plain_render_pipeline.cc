#include "plain_render_pipeline.hh"


plain_render_pipeline::plain_render_pipeline(context& ctx)
: render_pipeline(ctx)
{
    reset();
}

void plain_render_pipeline::reset()
{
    render_target target = ctx->get_render_target();
    xor_stage.reset(new xor_render_stage(*ctx, target));
}

VkSemaphore plain_render_pipeline::render_stages(VkSemaphore semaphore, uint32_t image_index)
{
    semaphore = xor_stage->run(image_index, semaphore);
    return semaphore;
}
