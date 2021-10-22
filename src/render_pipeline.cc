#include "render_pipeline.hh"

render_pipeline::render_pipeline(context& ctx)
: ctx(&ctx)
{
}

void render_pipeline::render()
{
    while(ctx->start_frame())
    {
        ctx->reset_swapchain();
        reset();
    }
    ctx->finish_frame(render_stages(ctx->get_start_semaphore(), ctx->get_image_index()));
}

