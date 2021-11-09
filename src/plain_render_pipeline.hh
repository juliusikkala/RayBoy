#ifndef RAYBOY_PLAIN_RENDER_PIPELINE_HH
#define RAYBOY_PLAIN_RENDER_PIPELINE_HH

#include "render_pipeline.hh"
#include "emulator_render_stage.hh"
#include "gui_render_stage.hh"
#include "blit_render_stage.hh"
#include "emulator.hh"
#include "texture.hh"

class plain_render_pipeline: public render_pipeline
{
public:
    struct options
    {
        bool faded = false;
        bool color_mapped = false;
        bool integer_scaling = true;
    };

    plain_render_pipeline(context& ctx, emulator& emu, const options& opt);

    void set_options(const options& opt);

    void reset() override final;

protected:
    VkSemaphore render_stages(VkSemaphore semaphore, uint32_t image_index) override final;

private:
    options opt;
    emulator* emu;
    std::unique_ptr<texture> color_buffer;
    std::unique_ptr<emulator_render_stage> emulator_stage;
    std::unique_ptr<blit_render_stage> blit_stage;
    std::unique_ptr<gui_render_stage> gui_stage;
};

#endif
