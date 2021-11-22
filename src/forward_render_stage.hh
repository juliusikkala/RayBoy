#ifndef RAYBOY_FORWARD_RENDER_STAGE_HH
#define RAYBOY_FORWARD_RENDER_STAGE_HH

#include "render_stage.hh"
#include "graphics_pipeline.hh"
#include "gpu_buffer.hh"
#include "scene.hh"
#include "timer.hh"
#include "texture.hh"
#include "sampler.hh"

class render_target;
class forward_render_stage: public render_stage
{
public:
    struct options
    {
        bool ray_tracing = false;
        unsigned shadow_rays = 8;
        unsigned reflection_rays = 1;
        bool refract = false;
    };

    forward_render_stage(
        context& ctx,
        render_target* color_target,
        render_target* depth_target,
        const scene& s,
        entity cam_id,
        const options& opt
    );

    void set_camera(entity cam_id);

protected:
    void update_buffers(uint32_t image_index) override;

private:
    graphics_pipeline depth_pre_pass;
    graphics_pipeline gfx;
    graphics_pipeline rt_gfx;
    options opt;
    entity cam_id;
    texture brdf_integration;
    texture blue_noise;
    sampler brdf_integration_sampler;
    timer stage_timer;
};

#endif
