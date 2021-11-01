#ifndef RAYBOY_SCENE_UPDATE_RENDER_STAGE_HH
#define RAYBOY_SCENE_UPDATE_RENDER_STAGE_HH

#include "render_stage.hh"
#include "compute_pipeline.hh"
#include "gpu_buffer.hh"
#include "timer.hh"
#include "ecs.hh"
#include "scene.hh"

class scene_update_render_stage: public render_stage
{
public:
    scene_update_render_stage(context& ctx, ecs& e, size_t max_entries = 512);
    ~scene_update_render_stage();

    const scene& get_scene() const;

protected:
    void update_buffers(uint32_t image_index) override;

private:
    ecs* e;
    scene s;
    timer stage_timer;
};

#endif

