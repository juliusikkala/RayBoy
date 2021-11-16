#include "forward_render_stage.hh"
#include "io.hh"
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
    render_target* color_target,
    render_target* depth_target,
    const scene& s,
    entity cam_id
):  render_stage(ctx), gfx(ctx), stage_timer(ctx, "forward_render_stage"),
    cam_id(cam_id), brdf_integration(ctx, get_readonly_path("data/brdf_integration.ktx")),
    brdf_integration_sampler(
        ctx, VK_FILTER_LINEAR, VK_FILTER_LINEAR,
        VK_SAMPLER_MIPMAP_MODE_NEAREST,
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, 1, 0, 0
    )
{
    shader_data sd;

    sd.vertex_bytes = sizeof(forward_vert_shader_binary);
    sd.vertex_data = forward_vert_shader_binary;

    sd.fragment_bytes = sizeof(forward_frag_shader_binary);
    sd.fragment_data = forward_frag_shader_binary;

    std::vector<render_target*> targets;
    if(color_target) targets.push_back(color_target);
    if(depth_target) targets.push_back(depth_target);

    graphics_pipeline::params gfx_params(targets);

    if(color_target)
    {
        gfx_params.blend_states[0] = {
            VK_TRUE,
            VK_BLEND_FACTOR_ONE,
            VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            VK_BLEND_OP_ADD,
            VK_BLEND_FACTOR_ONE,
            VK_BLEND_FACTOR_ZERO,
            VK_BLEND_OP_ADD,
            VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|
            VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT
        };
    }
    std::vector<VkDescriptorSetLayoutBinding> bindings = s.get_bindings();
    bindings.push_back({10, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});

    gfx.init(
        gfx_params,
        sd,
        ctx.get_image_count(), 
        bindings,
        sizeof(push_constants)
    );

    push_constants pc = {0, 0};

    for(uint32_t i = 0; i < ctx.get_image_count(); ++i)
    {
        // Assign descriptors
        s.set_descriptors(gfx, i);
        gfx.set_descriptor(i, 10, {brdf_integration.get_image_view(i)}, {brdf_integration_sampler.get()});

        // Record command buffer
        VkCommandBuffer buf = graphics_commands();
        stage_timer.start(buf, i);

        gfx.begin_render_pass(buf, i);
        gfx.bind(buf, i);

        std::vector<size_t> transparents;
        for(size_t j = 0; j < s.get_instance_count(); ++j)
        {
            if(s.is_instance_visible(j))
            {
                if(s.get_instance_material(j)->transmittance == 0.0f)
                {
                    pc.instance_id = j;
                    gfx.push_constants(buf, &pc);
                    s.draw_instance(buf, j);
                }
                else
                {
                    transparents.push_back(j);
                }
            }
        }

        for(size_t j: transparents)
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
