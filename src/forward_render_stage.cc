#include "forward_render_stage.hh"
#include "forward.frag.h"
#include "forward.vert.h"

namespace
{

struct push_constants
{
    uint32_t instance_id;
    uint32_t camera_id;
};

}

forward_render_stage::forward_render_stage(
    context& ctx,
    render_target& target,
    const scene& s,
    entity cam_id
): render_stage(ctx), gfx(ctx), stage_timer(ctx, "forward_render_stage")
{
    shader_data sd;

    sd.vertex_bytes = sizeof(forward_vert_shader_binary);
    sd.vertex_data = forward_vert_shader_binary;

    sd.fragment_bytes = sizeof(forward_frag_shader_binary);
    sd.fragment_data = forward_frag_shader_binary;

    gfx.init(
        graphics_pipeline::params({&target}),
        sd,
        ctx.get_image_count(), 
        s.get_bindings(),
        sizeof(push_constants)
    );

    push_constants pc = {0, 0};

    for(uint32_t i = 0; i < ctx.get_image_count(); ++i)
    {
        // Assign descriptors
        s.set_descriptors(gfx, i);

        // Record command buffer
        VkCommandBuffer buf = graphics_commands();
        stage_timer.start(buf, i);

        gfx.begin_render_pass(buf, i);
        gfx.bind(buf, i);

        for(size_t j = 0; j < s.get_instance_count(); ++j)
        {
            pc.instance_id = j;
            gfx.push_constants(buf, &pc);
            s.draw_instance(buf, j);
        }

        gfx.end_render_pass(buf);

        stage_timer.stop(buf, i);
        use_graphics_commands(buf, i);
    }
}

void forward_render_stage::set_camera(entity cam_id)
{
    this->cam_id = cam_id;
}

void forward_render_stage::update_buffers(uint32_t image_index)
{
}
