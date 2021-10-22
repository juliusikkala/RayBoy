#ifndef RAYBOY_PLAIN_RENDER_PIPELINE_HH
#define RAYBOY_PLAIN_RENDER_PIPELINE_HH

#include "render_pipeline.hh"
#include "xor_render_stage.hh"

class plain_render_pipeline: public render_pipeline
{
public:
    plain_render_pipeline(context& ctx);

    void reset() override final;

protected:
    VkSemaphore render_stages(VkSemaphore semaphore, uint32_t image_index) override final;

private:
    std::unique_ptr<xor_render_stage> xor_stage;
};

#endif
