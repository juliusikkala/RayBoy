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
        unsigned refraction_rays = 0;
        float accumulation_ratio = 0.1;
        bool secondary_shadows = false;
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
    void init_depth_pre_pass(
        graphics_pipeline& dp,
        const scene& s,
        render_target* depth_target,
        VkImageLayout initial_layout,
        VkImageLayout final_layout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR,
        bool clear = true
    );

    void init_forward_pass(
        graphics_pipeline& fp,
        const scene& s,
        render_target* color_target,
        render_target* depth_target
    );

    void init_rt_textures(uvec2 size);
    void init_generate_pass(
        graphics_pipeline& gp,
        const scene& s,
        render_target* depth,
        render_target* normal,
        render_target* accumulation,
        bool opaque
    );

    void init_gather_pass(
        graphics_pipeline& gp,
        const scene& s,
        render_target* color_target,
        render_target* depth_target,
        bool opaque
    );

    void draw_entities(
        uint32_t image_index,
        VkCommandBuffer buf,
        const scene& s,
        graphics_pipeline& gfx,
        int ray_traced, // -1: don't care, 0 don't render, 1 only render
        int transparent // -1: don't care, 0 don't render, 1 only render
    );

    // These pipelines and textures are only used when ray tracing is enabled.
    struct {
        graphics_pipeline opaque_depth_pre_pass;
        graphics_pipeline opaque_generate_pass;
        graphics_pipeline opaque_gather_pass;

        graphics_pipeline transparent_depth_pre_pass;
        graphics_pipeline transparent_generate_pass;
        graphics_pipeline transparent_gather_pass;

        std::unique_ptr<texture> opaque_depth;
        std::unique_ptr<texture> opaque_normal;
        std::unique_ptr<texture> opaque_accumulation;

        std::unique_ptr<texture> transparent_depth;
        std::unique_ptr<texture> transparent_normal;
        std::unique_ptr<texture> transparent_accumulation;
    } rt;

    graphics_pipeline depth_pre_pass;
    graphics_pipeline default_raster;

    options opt;
    entity cam_id;
    texture brdf_integration;
    texture blue_noise;
    sampler brdf_integration_sampler;
    sampler buffer_sampler;
    timer stage_timer;
    gpu_buffer accumulation_data;
    uint64_t history_frames;
};

#endif
