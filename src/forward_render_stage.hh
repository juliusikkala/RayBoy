#ifndef RAYBOY_FORWARD_RENDER_STAGE_HH
#define RAYBOY_FORWARD_RENDER_STAGE_HH

#include "render_stage.hh"
#include "graphics_pipeline.hh"
#include "gpu_buffer.hh"
#include "scene.hh"
#include "timer.hh"

class render_target;
class forward_render_stage: public render_stage
{
public:
    forward_render_stage(
        context& ctx,
        render_target& target,
        const scene& s,
        entity cam_id
    );

    void set_camera(entity cam_id);

protected:
    void update_buffers(uint32_t image_index) override;

private:
    graphics_pipeline gfx;
    const scene* s;
    entity cam_id;
    timer stage_timer;
};

#endif
