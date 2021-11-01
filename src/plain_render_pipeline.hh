#ifndef RAYBOY_PLAIN_RENDER_PIPELINE_HH
#define RAYBOY_PLAIN_RENDER_PIPELINE_HH

#include "render_pipeline.hh"
#include "scene_update_render_stage.hh"
#include "forward_render_stage.hh"
#include "xor_render_stage.hh"

class plain_render_pipeline: public render_pipeline
{
public:
    plain_render_pipeline(context& ctx, ecs& entities);

    void reset() override final;

protected:
    VkSemaphore render_stages(VkSemaphore semaphore, uint32_t image_index) override final;

private:
    ecs* entities;
    std::unique_ptr<scene_update_render_stage> scene_update_stage;
    std::unique_ptr<forward_render_stage> forward_stage;
    std::unique_ptr<xor_render_stage> xor_stage;
};

#endif
