#ifndef RAYBOY_RENDER_PIPELINE_HH
#define RAYBOY_RENDER_PIPELINE_HH

#include "context.hh"

class render_pipeline
{
public:
    render_pipeline(context& ctx);
    virtual ~render_pipeline() = default;

    virtual void reset() = 0;
    void render();

protected:
    virtual VkSemaphore render_stages(VkSemaphore semaphore, uint32_t image_index) = 0;

    context* ctx;
};

#endif
