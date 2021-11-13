#ifndef RAYBOY_TONEMAP_RENDER_STAGE_HH
#define RAYBOY_TONEMAP_RENDER_STAGE_HH

#include "render_stage.hh"
#include "compute_pipeline.hh"
#include "gpu_buffer.hh"
#include "timer.hh"

class render_target;
class tonemap_render_stage: public render_stage
{
public:
    struct options
    {
        float exposure = 1.0f;
        uint32_t algorithm = 0;
    };
    tonemap_render_stage(context& ctx, render_target& src, render_target& dst, const options& opt);

protected:
    void update_buffers(uint32_t image_index) override;

private:
    options opt;
    compute_pipeline tonemap_pipeline;
    gpu_buffer uniforms;
    timer stage_timer;
};

#endif
