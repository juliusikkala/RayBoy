#include "forward_render_stage.hh"
#include "io.hh"
#include "model.hh"
#include "forward.frag.h"
#include "forward.vert.h"
#include "forward_rt.frag.h"
#include "depth.frag.h"
#include "depth.vert.h"

namespace
{

struct depth_push_constants
{
    uint32_t instance_id;
    uint32_t camera_id;
};

struct push_constants
{
    uint32_t instance_id;
    uint32_t camera_id;
    uint32_t disable_rt_reflection;
};

void init_shading_pipeline(
    bool ray_tracing,
    graphics_pipeline& gfx,
    context& ctx,
    render_target* color_target,
    render_target* depth_target,
    const scene& s,
    const forward_render_stage::options& opt,
    bool clear
){ // Regular non-RT shading pipeline
    shader_data sd;

    sd.vertex_bytes = sizeof(forward_vert_shader_binary);
    sd.vertex_data = forward_vert_shader_binary;
    auto spec_entries = s.get_specialization_entries();
    auto spec_data = s.get_specialization_data();

    if(ray_tracing)
    {
        sd.fragment_bytes = sizeof(forward_rt_frag_shader_binary);
        sd.fragment_data = forward_rt_frag_shader_binary;
        spec_entries.push_back({2, 2*sizeof(uint32_t), sizeof(uint32_t)});
        spec_entries.push_back({3, 3*sizeof(uint32_t), sizeof(uint32_t)});
        spec_data.push_back(opt.shadow_rays);
        spec_data.push_back(opt.reflection_rays);
    }
    else
    {
        sd.fragment_bytes = sizeof(forward_frag_shader_binary);
        sd.fragment_data = forward_frag_shader_binary;
    }
    sd.fragment_specialization.mapEntryCount = spec_entries.size();
    sd.fragment_specialization.pMapEntries = spec_entries.data();
    sd.fragment_specialization.dataSize = spec_data.size() * sizeof(uint32_t);
    sd.fragment_specialization.pData = spec_data.data();

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
    bindings.push_back({9, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});
    bindings.push_back({10, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});

    if(!clear)
    {
        gfx_params.attachments[0].initialLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR;
        gfx_params.attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    }
    gfx_params.attachments[1].initialLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR;
    gfx_params.attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;

    gfx.init(
        gfx_params,
        sd,
        ctx.get_image_count(), 
        bindings,
        sizeof(push_constants)
    );
}

void draw_entities(
    uint32_t image_index,
    VkCommandBuffer buf,
    const scene& s,
    graphics_pipeline& gfx,
    bool ray_traced,
    bool transparent
){
    push_constants pc = {0, 0, 0};
    s.get_ecs().foreach([&](entity id, model& m, visible&, struct ray_traced* rt){
        for(size_t i = 0; i < m.group_count(); ++i)
        {
            bool pot_transparent = m[i].mat.potentially_transparent();
            if((bool)rt == ray_traced && pot_transparent == transparent)
            {
                pc.instance_id = s.get_entity_instance_id(id, i);
                pc.disable_rt_reflection = rt ? !rt->reflection : true;
                gfx.push_constants(buf, &pc);
                m[i].mesh->draw(buf);
            }
        }
    });
}

}

forward_render_stage::forward_render_stage(
    context& ctx,
    render_target* color_target,
    render_target* depth_target,
    const scene& s,
    entity cam_id,
    const options& opt
):  render_stage(ctx), depth_pre_pass(ctx), gfx(ctx), rt_gfx(ctx), opt(opt),
    stage_timer(ctx, "forward_render_stage"),
    cam_id(cam_id),
    brdf_integration(ctx, get_readonly_path("data/brdf_integration.ktx")),
    blue_noise(ctx, get_readonly_path("data/blue_noise.png")),
    brdf_integration_sampler(
        ctx, VK_FILTER_LINEAR, VK_FILTER_LINEAR,
        VK_SAMPLER_MIPMAP_MODE_NEAREST,
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, 1, 0, 0
    )
{
    { // Depth pre-pass pipeline
        shader_data sd;

        sd.vertex_bytes = sizeof(depth_vert_shader_binary);
        sd.vertex_data = depth_vert_shader_binary;
        sd.fragment_bytes = sizeof(depth_frag_shader_binary);
        sd.fragment_data = depth_frag_shader_binary;

        std::vector<render_target*> targets;
        if(depth_target) targets.push_back(depth_target);

        graphics_pipeline::params pre_pass_params(targets);

        std::vector<VkDescriptorSetLayoutBinding> bindings = s.get_bindings();

        depth_pre_pass.init(
            pre_pass_params,
            sd,
            ctx.get_image_count(), 
            bindings,
            sizeof(push_constants)
        );
    }

    init_shading_pipeline(false, gfx, ctx, color_target, depth_target, s, opt, true);
    init_shading_pipeline(opt.ray_tracing, rt_gfx, ctx, color_target, depth_target, s, opt, false);

    for(uint32_t i = 0; i < ctx.get_image_count(); ++i)
    {
        // Assign descriptors
        s.set_descriptors(depth_pre_pass, i);

        s.set_descriptors(gfx, i);
        gfx.set_descriptor(i, 9, {blue_noise.get_image_view(i)}, {brdf_integration_sampler.get()});
        gfx.set_descriptor(i, 10, {brdf_integration.get_image_view(i)}, {brdf_integration_sampler.get()});

        s.set_descriptors(rt_gfx, i);
        rt_gfx.set_descriptor(i, 9, {blue_noise.get_image_view(i)}, {brdf_integration_sampler.get()});
        rt_gfx.set_descriptor(i, 10, {brdf_integration.get_image_view(i)}, {brdf_integration_sampler.get()});

        // Record command buffer
        VkCommandBuffer buf = graphics_commands();
        stage_timer.start(buf, i);

        // Pre-pass to prevent overdraw (it's ridiculously expensive with RT)
        depth_pre_pass.begin_render_pass(buf, i);
        depth_pre_pass.bind(buf, i);

        depth_push_constants pc = {0, 0};

        s.get_ecs().foreach([&](entity id, model& m, visible&){
            for(size_t i = 0; i < m.group_count(); ++i)
            {
                if(m[i].mat.transmittance == 0.0f)
                {
                    pc.instance_id = s.get_entity_instance_id(id, i);
                    depth_pre_pass.push_constants(buf, &pc);
                    m[i].mesh->draw(buf);
                }
            }
        });
        depth_pre_pass.end_render_pass(buf);

        // No-RT pass
        gfx.bind(buf, i);
        gfx.begin_render_pass(buf, i);
        draw_entities(i, buf, s, gfx, false, false);
        draw_entities(i, buf, s, gfx, false, true);
        gfx.end_render_pass(buf);

        // RT pass
        rt_gfx.bind(buf, i);
        rt_gfx.begin_render_pass(buf, i);
        draw_entities(i, buf, s, rt_gfx, true, false);
        draw_entities(i, buf, s, rt_gfx, true, true);
        rt_gfx.end_render_pass(buf);

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
