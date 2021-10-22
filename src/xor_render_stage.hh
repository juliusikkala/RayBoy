#ifndef RAYBOY_XOR_RENDER_STAGE_HH
#define RAYBOY_XOR_RENDER_STAGE_HH

#include "render_stage.hh"

class render_target;
class xor_render_stage: public render_stage
{
public:
    xor_render_stage(context& ctx, render_target& target);
};

#endif
