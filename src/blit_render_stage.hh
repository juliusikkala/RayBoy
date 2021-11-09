#ifndef RAYBOY_BLIT_RENDER_STAGE_HH
#define RAYBOY_BLIT_RENDER_STAGE_HH

#include "render_stage.hh"
#include "timer.hh"

class render_target;
class blit_render_stage: public render_stage
{
public:
    blit_render_stage(
        context& ctx,
        render_target& src,
        render_target& dst,
        bool stretch = true,
        bool integer_scaling = true
    );

protected:
private:
    timer stage_timer;
};

#endif
