#ifndef RAYBOY_XOR_RENDER_STAGE_HH
#define RAYBOY_XOR_RENDER_STAGE_HH

#include "render_stage.hh"
#include "compute_pipeline.hh"
#include "gpu_buffer.hh"
#include "timer.hh"

class render_target;
class xor_render_stage: public render_stage
{
public:
    xor_render_stage(context& ctx, render_target& target);

protected:
    void update_buffers(uint32_t image_index) override;

private:
    compute_pipeline xor_pipeline;
    gpu_buffer uniforms;
    timer stage_timer;
};

#endif
