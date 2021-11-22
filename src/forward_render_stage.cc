#include "forward_render_stage.hh"
#include "io.hh"
#include "model.hh"
#include "forward.frag.h"
#include "forward.vert.h"
#include "forward_rt.frag.h"
#include "depth.frag.h"
#include "depth.vert.h"
#include "gbuffer.frag.h"
#include "gbuffer.vert.h"
#include "rt.frag.h"

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

enum pipeline_type
{
    PP_RASTER,
    PP_RT_GENERATE,
    PP_RT_GATHER
};

void init_shading_pipeline(
    pipeline_type type,
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

    if(type == PP_RASTER)
    {
        sd.fragment_bytes = sizeof(forward_frag_shader_binary);
        sd.fragment_data = forward_frag_shader_binary;
    }
    else if(type == PP_RT_GENERATE)
    {
        sd.fragment_bytes = sizeof(rt_frag_shader_binary);
        sd.fragment_data = rt_frag_shader_binary;
        spec_entries.push_back({2, 2*sizeof(uint32_t), sizeof(uint32_t)});
        spec_entries.push_back({3, 3*sizeof(uint32_t), sizeof(uint32_t)});
        spec_data.push_back(opt.shadow_rays);
        spec_data.push_back(opt.reflection_rays);
    }
    else if(type == PP_RT_GATHER)
    {
        sd.fragment_bytes = sizeof(forward_rt_frag_shader_binary);
        sd.fragment_data = forward_rt_frag_shader_binary;
        spec_entries.push_back({2, 2*sizeof(uint32_t), sizeof(uint32_t)});
        spec_entries.push_back({3, 3*sizeof(uint32_t), sizeof(uint32_t)});
        spec_data.push_back(opt.shadow_rays);
        spec_data.push_back(opt.reflection_rays);
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
    if(type == PP_RT_GATHER)
    {
        // G-Buffer data 
        // Normal texture
        bindings.push_back({11, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});
        // Depth texture
        bindings.push_back({12, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});
        // Reflection texture
        bindings.push_back({13, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});
    }

    if(!clear)
    {
        gfx_params.attachments[0].initialLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR;
        gfx_params.attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    }
    gfx_params.attachments[1].initialLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR;
    gfx_params.attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;

    if(type == PP_RT_GENERATE)
    {
        gfx_params.attachments[0].initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        gfx_params.attachments[1].initialLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR;
        gfx_params.attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        gfx_params.attachments[1].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

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
):  render_stage(ctx), geometry_pass(ctx), rt_generate(ctx),
    depth_pre_pass(ctx), raster(ctx), rt_gather(ctx), opt(opt),
    stage_timer(ctx, "forward_render_stage"),
    cam_id(cam_id),
    brdf_integration(ctx, get_readonly_path("data/brdf_integration.ktx")),
    blue_noise(ctx, get_readonly_path("data/blue_noise.png")),
    brdf_integration_sampler(
        ctx, VK_FILTER_LINEAR, VK_FILTER_LINEAR,
        VK_SAMPLER_MIPMAP_MODE_NEAREST,
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, 1, 0, 0
    ),
    gbuffer_sampler(
        ctx, VK_FILTER_NEAREST, VK_FILTER_NEAREST,
        VK_SAMPLER_MIPMAP_MODE_NEAREST,
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, 1, 0, 0
    )
{
    if(opt.ray_tracing)
    {
        depth_texture.reset(new texture(
            ctx,
            color_target->get_size(),
            VK_FORMAT_D32_SFLOAT,
            0, nullptr,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_SAMPLE_COUNT_1_BIT
        ));
        depth_texture_target = depth_texture->get_render_target();
        normal_texture.reset(new texture(
            ctx,
            color_target->get_size(),
            VK_FORMAT_R16G16_SFLOAT,
            0, nullptr,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_SAMPLE_COUNT_1_BIT
        ));
        normal_texture_target = normal_texture->get_render_target();
        reflection_texture.reset(new texture(
            ctx,
            color_target->get_size(),
            VK_FORMAT_R16G16B16A16_SFLOAT,
            0, nullptr,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_SAMPLE_COUNT_1_BIT
        ));
        reflection_texture_target = reflection_texture->get_render_target();
        { // Geometry pass pipeline
            shader_data sd;

            sd.vertex_bytes = sizeof(gbuffer_vert_shader_binary);
            sd.vertex_data = gbuffer_vert_shader_binary;
            sd.fragment_bytes = sizeof(gbuffer_frag_shader_binary);
            sd.fragment_data = gbuffer_frag_shader_binary;

            graphics_pipeline::params params({
                &normal_texture_target,
                &depth_texture_target
            });

            params.attachments[0].initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            params.attachments[1].initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            params.attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            params.attachments[1].finalLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR;
            normal_texture_target.set_layout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            depth_texture_target.set_layout(VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR);

            std::vector<VkDescriptorSetLayoutBinding> bindings = s.get_bindings();

            geometry_pass.init(
                params,
                sd,
                ctx.get_image_count(),
                bindings,
                sizeof(push_constants)
            );
        }
        init_shading_pipeline(PP_RT_GENERATE, rt_generate, ctx, &reflection_texture_target, &depth_texture_target, s, opt, false);
        init_shading_pipeline(PP_RT_GATHER, rt_gather, ctx, color_target, depth_target, s, opt, false);
    }

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

    init_shading_pipeline(PP_RASTER, raster, ctx, color_target, depth_target, s, opt, true);

    for(uint32_t i = 0; i < ctx.get_image_count(); ++i)
    {
        // Assign descriptors
        s.set_descriptors(depth_pre_pass, i);

        s.set_descriptors(raster, i);
        raster.set_descriptor(i, 9, {blue_noise.get_image_view(i)}, {brdf_integration_sampler.get()});
        raster.set_descriptor(i, 10, {brdf_integration.get_image_view(i)}, {brdf_integration_sampler.get()});

        if(opt.ray_tracing)
        {
            s.set_descriptors(geometry_pass, i);
            s.set_descriptors(rt_generate, i);
            rt_generate.set_descriptor(i, 9, {blue_noise.get_image_view(i)}, {brdf_integration_sampler.get()});
            rt_generate.set_descriptor(i, 10, {brdf_integration.get_image_view(i)}, {brdf_integration_sampler.get()});
            s.set_descriptors(rt_gather, i);
            rt_gather.set_descriptor(i, 9, {blue_noise.get_image_view(i)}, {brdf_integration_sampler.get()});
            rt_gather.set_descriptor(i, 10, {brdf_integration.get_image_view(i)}, {brdf_integration_sampler.get()});
            rt_gather.set_descriptor(i, 11, {normal_texture->get_image_view(i)}, {gbuffer_sampler.get()});
            rt_gather.set_descriptor(i, 12, {depth_texture->get_image_view(i)}, {gbuffer_sampler.get()});
            rt_gather.set_descriptor(i, 13, {reflection_texture->get_image_view(i)}, {gbuffer_sampler.get()});
        }

        // Record command buffer
        VkCommandBuffer buf = graphics_commands();
        stage_timer.start(buf, i);

        if(opt.ray_tracing)
        {
            // Opaque geometry pass
            geometry_pass.bind(buf, i);
            geometry_pass.begin_render_pass(buf, i);

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

            geometry_pass.end_render_pass(buf);

            rt_generate.bind(buf, i);
            rt_generate.begin_render_pass(buf, i);
            draw_entities(i, buf, s, rt_generate, true, false);
            rt_generate.end_render_pass(buf);
        }

        // Pre-pass to prevent overdraw (it's ridiculously expensive with RT)
        depth_pre_pass.bind(buf, i);
        depth_pre_pass.begin_render_pass(buf, i);

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
        raster.bind(buf, i);
        raster.begin_render_pass(buf, i);
        draw_entities(i, buf, s, raster, false, false);
        if(!opt.ray_tracing) draw_entities(i, buf, s, raster, true, false);
        draw_entities(i, buf, s, raster, false, true);
        if(!opt.ray_tracing) draw_entities(i, buf, s, raster, true, true);
        raster.end_render_pass(buf);

        // RT pass
        if(opt.ray_tracing)
        {
            // Finish opaques
            rt_gather.bind(buf, i);
            rt_gather.begin_render_pass(buf, i);
            draw_entities(i, buf, s, rt_gather, true, false);
            rt_gather.end_render_pass(buf);

            // Do transparents next
            geometry_pass.bind(buf, i);
            geometry_pass.begin_render_pass(buf, i);

            depth_push_constants pc = {0, 0};

            s.get_ecs().foreach([&](entity id, model& m, visible&){
                for(size_t i = 0; i < m.group_count(); ++i)
                {
                    if(m[i].mat.transmittance != 0.0f)
                    {
                        pc.instance_id = s.get_entity_instance_id(id, i);
                        depth_pre_pass.push_constants(buf, &pc);
                        m[i].mesh->draw(buf);
                    }
                }
            });

            geometry_pass.end_render_pass(buf);

            rt_generate.bind(buf, i);
            rt_generate.begin_render_pass(buf, i);
            draw_entities(i, buf, s, rt_generate, true, true);
            rt_generate.end_render_pass(buf);

            // Finish transparents
            rt_gather.bind(buf, i);
            rt_gather.begin_render_pass(buf, i);
            draw_entities(i, buf, s, rt_gather, true, true);
            rt_gather.end_render_pass(buf);
        }

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
