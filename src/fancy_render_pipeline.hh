#ifndef RAYBOY_FANCY_RENDER_PIPELINE_HH
#define RAYBOY_FANCY_RENDER_PIPELINE_HH

#include "render_pipeline.hh"
#include "blit_render_stage.hh"
#include "scene_update_render_stage.hh"
#include "forward_render_stage.hh"
#include "tonemap_render_stage.hh"
#include "gui_render_stage.hh"

class fancy_render_pipeline: public render_pipeline
{
public:
    struct options
    {
        float resolution_scaling = 1.0f;
        VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    };

    fancy_render_pipeline(context& ctx, ecs& entities, const options& opt);

    void set_options(const options& opt);

    void reset() override final;

protected:
    VkSemaphore render_stages(VkSemaphore semaphore, uint32_t image_index) override final;

private:
    ecs* entities;
    options opt;
    std::unique_ptr<texture> color_buffer;
    std::unique_ptr<texture> depth_buffer;
    std::unique_ptr<texture> resolve_buffer;
    std::unique_ptr<scene_update_render_stage> scene_update_stage;
    std::unique_ptr<forward_render_stage> forward_stage;
    std::unique_ptr<tonemap_render_stage> tonemap_stage;
    std::unique_ptr<gui_render_stage> gui_stage;
    std::unique_ptr<blit_render_stage> blit_stage;
};

#endif

