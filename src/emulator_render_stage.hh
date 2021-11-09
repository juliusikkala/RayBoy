#ifndef RAYBOY_EMULATOR_RENDER_STAGE_HH
#define RAYBOY_EMULATOR_RENDER_STAGE_HH

#include "render_stage.hh"
#include "compute_pipeline.hh"
#include "gpu_buffer.hh"
#include "timer.hh"
#include "emulator.hh"

class render_target;
// Simply uploads the emulator's framebuffer to a texture with the specified
// color & subpixel transformations. Can also generate mipmaps.
class emulator_render_stage: public render_stage
{
public:
    emulator_render_stage(
        context& ctx,
        emulator& emu,
        render_target& output_target,
        bool generate_mipmaps = true,
        bool color_mapping = false,
        bool faded = false
    );

protected:
    void update_buffers(uint32_t image_index) override;

private:
    emulator* emu;
    bool faded;
    compute_pipeline transform_pipeline;
    gpu_buffer image_buffer;
    timer stage_timer;
};

#endif
