#include "forward_render_stage.hh"
#include "io.hh"
#include "model.hh"
#include "helpers.hh"
#include "forward.frag.h"
#include "forward.vert.h"
#include "depth.frag.h"
#include "depth.vert.h"
#include "generate.frag.h"
#include "gather.frag.h"

namespace
{

struct push_constants
{
    uint32_t instance_id;
    uint32_t camera_id;
    uint32_t disable_rt_reflection;
    uint32_t disable_rt_refraction;
};

struct accumulation_data_buffer
{
    float accumulation_ratio;
};

}

forward_render_stage::forward_render_stage(
    context& ctx,
    render_target* color_target,
    render_target* depth_target,
    const scene& s,
    entity cam_id,
    const options& opt
):  render_stage(ctx),
    rt{ctx, ctx, ctx, ctx, ctx, ctx},
    depth_pre_pass(ctx), default_raster(ctx), opt(opt),
    stage_timer(ctx, "forward_render_stage"),
    cam_id(cam_id),
    brdf_integration(ctx, get_readonly_path("data/brdf_integration.ktx")),
    blue_noise(ctx, get_readonly_path("data/blue_noise.png")),
    brdf_integration_sampler(
        ctx, VK_FILTER_LINEAR, VK_FILTER_LINEAR,
        VK_SAMPLER_MIPMAP_MODE_NEAREST,
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, 1, 0, 0
    ),
    buffer_sampler(
        ctx, VK_FILTER_NEAREST, VK_FILTER_NEAREST,
        VK_SAMPLER_MIPMAP_MODE_NEAREST,
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, 1, 0, 0
    ),
    accumulation_data(ctx, sizeof(accumulation_data_buffer)),
    history_frames(0)
{
    init_depth_pre_pass(depth_pre_pass, s, depth_target, VK_IMAGE_LAYOUT_UNDEFINED);
    init_forward_pass(default_raster, s, color_target, depth_target);

    if(opt.ray_tracing)
    {
        init_rt_textures(color_target->get_size());

        render_target opaque_depth = rt.opaque_depth->get_render_target();
        render_target opaque_normal = rt.opaque_normal->get_render_target();
        render_target opaque_accumulation = rt.opaque_accumulation->get_render_target();
        render_target transparent_depth = rt.transparent_depth->get_render_target();
        render_target transparent_normal = rt.transparent_normal->get_render_target();
        render_target transparent_accumulation = rt.transparent_accumulation->get_render_target();

        if(opt.reflection_rays >= 1 || opt.refraction_rays >= 1)
        {
            init_depth_pre_pass(
                rt.opaque_depth_pre_pass, s, &opaque_depth,
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR
            );
            init_depth_pre_pass(
                rt.transparent_depth_pre_pass, s, &transparent_depth,
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR
            );
            init_generate_pass(
                rt.opaque_generate_pass, s,
                &opaque_depth, &opaque_normal, &opaque_accumulation,
                true
            );
            init_generate_pass(
                rt.transparent_generate_pass, s,
                &transparent_depth, &transparent_normal, &transparent_accumulation,
                false
            );
        }

        init_gather_pass(rt.opaque_gather_pass, s, color_target, depth_target, true);
        init_gather_pass(rt.transparent_gather_pass, s, color_target, depth_target, false);
    }

    uvec2 size = color_target->get_size();

    uint32_t j = ctx.get_image_count()-1;
    for(uint32_t i = 0; i < ctx.get_image_count(); ++i, j = (j+1)%ctx.get_image_count())
    {
        // Record command buffer
        VkCommandBuffer buf = graphics_commands();
        stage_timer.start(buf, i);
        accumulation_data.upload(buf, i);

        if(opt.ray_tracing && (opt.reflection_rays >= 1 || opt.refraction_rays >= 1))
        {
            // Opaque depth pre-pass
            rt.opaque_depth_pre_pass.bind(buf, i);
            rt.opaque_depth_pre_pass.begin_render_pass(buf, i);
            draw_entities(i, buf, s, rt.opaque_depth_pre_pass, 1, 0);
            rt.opaque_depth_pre_pass.end_render_pass(buf);
            
            // Transparent depth pre-pass
            rt.transparent_depth_pre_pass.bind(buf, i);
            rt.transparent_depth_pre_pass.begin_render_pass(buf, i);
            draw_entities(i, buf, s, rt.transparent_depth_pre_pass, 1, 0);
            draw_entities(i, buf, s, rt.transparent_depth_pre_pass, 1, 1);
            rt.transparent_depth_pre_pass.end_render_pass(buf);

            // Opaque generate pass
            rt.opaque_generate_pass.bind(buf, i);
            rt.opaque_generate_pass.begin_render_pass(buf, i);
            draw_entities(i, buf, s, rt.opaque_generate_pass, 1, 0);
            rt.opaque_generate_pass.end_render_pass(buf);

            // Transparent generate pass
            rt.transparent_generate_pass.bind(buf, i);
            rt.transparent_generate_pass.begin_render_pass(buf, i);
            draw_entities(i, buf, s, rt.transparent_generate_pass, 1, 1);
            rt.transparent_generate_pass.end_render_pass(buf);

            image_barrier(
                buf,
                rt.opaque_depth->get_image(i),
                rt.opaque_depth->get_format(),
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            );
            image_barrier(
                buf,
                rt.transparent_depth->get_image(i),
                rt.transparent_depth->get_format(),
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            );
            image_barrier(
                buf,
                rt.opaque_accumulation->get_image(i),
                rt.opaque_accumulation->get_format(),
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            );
            image_barrier(
                buf,
                rt.transparent_accumulation->get_image(i),
                rt.transparent_accumulation->get_format(),
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            );
            image_barrier(
                buf,
                rt.opaque_normal->get_image(i),
                rt.opaque_normal->get_format(),
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            );
            image_barrier(
                buf,
                rt.transparent_normal->get_image(i),
                rt.transparent_normal->get_format(),
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            );
        }

        // Pre-pass to prevent overdraw (it's ridiculously expensive with RT)
        depth_pre_pass.bind(buf, i);
        depth_pre_pass.begin_render_pass(buf, i);
        draw_entities(i, buf, s, depth_pre_pass, -1, 0);
        depth_pre_pass.end_render_pass(buf);

        // No-RT pass
        default_raster.bind(buf, i);
        default_raster.begin_render_pass(buf, i);
        int rt_mode = opt.ray_tracing ? 0 : -1;
        draw_entities(i, buf, s, default_raster, rt_mode, 0);
        draw_entities(i, buf, s, default_raster, rt_mode, 1);
        default_raster.end_render_pass(buf);

        // RT pass
        if(opt.ray_tracing)
        {
            rt.opaque_gather_pass.bind(buf, i);
            rt.opaque_gather_pass.begin_render_pass(buf, i);
            draw_entities(i, buf, s, rt.opaque_gather_pass, 1, 0);
            rt.opaque_gather_pass.end_render_pass(buf);

            rt.transparent_gather_pass.bind(buf, i);
            rt.transparent_gather_pass.begin_render_pass(buf, i);
            draw_entities(i, buf, s, rt.transparent_gather_pass, 1, 1);
            rt.transparent_gather_pass.end_render_pass(buf);
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
    history_frames++;
    float accumulation_ratio = max(1.0f/history_frames, opt.accumulation_ratio);
    accumulation_data.update(image_index, accumulation_data_buffer{
        accumulation_ratio
    });
}

void forward_render_stage::init_depth_pre_pass(
    graphics_pipeline& dp,
    const scene& s,
    render_target* depth_target,
    VkImageLayout initial_layout,
    VkImageLayout final_layout,
    bool clear
){
    shader_data sd;

    sd.vertex_bytes = sizeof(depth_vert_shader_binary);
    sd.vertex_data = depth_vert_shader_binary;
    sd.fragment_bytes = sizeof(depth_frag_shader_binary);
    sd.fragment_data = depth_frag_shader_binary;

    std::vector<render_target*> targets;
    if(depth_target) targets.push_back(depth_target);

    graphics_pipeline::params pre_pass_params(targets);

    pre_pass_params.attachments[0].initialLayout = initial_layout;
    if(!clear)
        pre_pass_params.attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    pre_pass_params.attachments[0].finalLayout = final_layout;

    std::vector<VkDescriptorSetLayoutBinding> bindings = s.get_bindings();

    dp.init(
        pre_pass_params,
        sd,
        ctx->get_image_count(),
        bindings,
        sizeof(push_constants)
    );

    for(uint32_t i = 0; i < ctx->get_image_count(); ++i)
        s.set_descriptors(dp, i); // Assign descriptors
}

void forward_render_stage::init_forward_pass(
    graphics_pipeline& fp,
    const scene& s,
    render_target* color_target,
    render_target* depth_target
){
    shader_data sd;

    sd.vertex_bytes = sizeof(forward_vert_shader_binary);
    sd.vertex_data = forward_vert_shader_binary;
    auto spec_entries = s.get_specialization_entries();
    auto spec_data = s.get_specialization_data();

    sd.fragment_bytes = sizeof(forward_frag_shader_binary);
    sd.fragment_data = forward_frag_shader_binary;
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

    gfx_params.attachments[1].initialLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR;
    gfx_params.attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;

    fp.init(
        gfx_params,
        sd,
        ctx->get_image_count(),
        bindings,
        sizeof(push_constants)
    );

    for(uint32_t i = 0; i < ctx->get_image_count(); ++i)
    {
        s.set_descriptors(fp, i);
        fp.set_descriptor(i, 9, {blue_noise.get_image_view(i)}, {brdf_integration_sampler.get()});
        fp.set_descriptor(i, 10, {brdf_integration.get_image_view(i)}, {brdf_integration_sampler.get()});
    }
}

void forward_render_stage::init_rt_textures(uvec2 size)
{
    rt.opaque_depth.reset(new texture(
        *ctx,
        size,
        VK_FORMAT_D32_SFLOAT,
        0, nullptr,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_SAMPLE_COUNT_1_BIT
    ));
    rt.opaque_normal.reset(new texture(
        *ctx,
        size,
        VK_FORMAT_R16G16_SFLOAT,
        0, nullptr,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_SAMPLE_COUNT_1_BIT
    ));
    rt.opaque_accumulation.reset(new texture(
        *ctx,
        size,
        VK_FORMAT_R16G16B16A16_SFLOAT,
        0, nullptr,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_SAMPLE_COUNT_1_BIT
    ));
    rt.transparent_depth.reset(new texture(
        *ctx,
        size,
        VK_FORMAT_D32_SFLOAT,
        0, nullptr,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|
        VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_SAMPLE_COUNT_1_BIT
    ));
    rt.transparent_normal.reset(new texture(
        *ctx,
        size,
        VK_FORMAT_R16G16_SFLOAT,
        0, nullptr,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_SAMPLE_COUNT_1_BIT
    ));
    rt.transparent_accumulation.reset(new texture(
        *ctx,
        size,
        VK_FORMAT_R16G16B16A16_SFLOAT,
        0, nullptr,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_SAMPLE_COUNT_1_BIT
    ));
}

void forward_render_stage::init_generate_pass(
    graphics_pipeline& gp,
    const scene& s,
    render_target* depth,
    render_target* normal,
    render_target* accumulation,
    bool opaque
){
    shader_data sd;

    sd.vertex_bytes = sizeof(forward_vert_shader_binary);
    sd.vertex_data = forward_vert_shader_binary;
    auto spec_entries = s.get_specialization_entries();
    auto spec_data = s.get_specialization_data();

    sd.fragment_bytes = sizeof(generate_frag_shader_binary);
    sd.fragment_data = generate_frag_shader_binary;
    spec_entries.push_back({2, 2*sizeof(uint32_t), sizeof(uint32_t)});
    spec_entries.push_back({3, 3*sizeof(uint32_t), sizeof(uint32_t)});
    spec_entries.push_back({4, 4*sizeof(uint32_t), sizeof(uint32_t)});
    spec_entries.push_back({5, 5*sizeof(uint32_t), sizeof(uint32_t)});
    spec_data.push_back(opt.shadow_rays);
    spec_data.push_back(opt.reflection_rays);
    spec_data.push_back(opt.refraction_rays);
    spec_data.push_back(opt.secondary_shadows ? 1 : 0);

    sd.fragment_specialization.mapEntryCount = spec_entries.size();
    sd.fragment_specialization.pMapEntries = spec_entries.data();
    sd.fragment_specialization.dataSize = spec_data.size() * sizeof(uint32_t);
    sd.fragment_specialization.pData = spec_data.data();

    std::vector<render_target*> targets = {accumulation, normal, depth};

    graphics_pipeline::params gfx_params(targets);

    std::vector<VkDescriptorSetLayoutBinding> bindings = s.get_bindings();
    bindings.push_back({9, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});
    bindings.push_back({10, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});
    bindings.push_back({11, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});
    bindings.push_back({12, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});
    bindings.push_back({13, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});
    bindings.push_back({14, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});

    gfx_params.attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    gfx_params.attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    gfx_params.attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    gfx_params.attachments[1].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    gfx_params.attachments[2].initialLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR;
    gfx_params.attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    //gfx_params.attachments[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    gfx_params.attachments[2].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    gp.init(
        gfx_params,
        sd,
        ctx->get_image_count(),
        bindings,
        sizeof(push_constants)
    );

    uint32_t j = ctx->get_image_count()-1;
    for(uint32_t i = 0; i < ctx->get_image_count(); ++i, j = (j+1)%ctx->get_image_count())
    {
        s.set_descriptors(gp, i);
        gp.set_descriptor(i, 9, {blue_noise.get_image_view(i)}, {brdf_integration_sampler.get()});
        gp.set_descriptor(i, 10, {brdf_integration.get_image_view(i)}, {brdf_integration_sampler.get()});
        if(opaque)
        {
            gp.set_descriptor(i, 11, {rt.opaque_depth->get_image_view(j)}, {buffer_sampler.get()});
            gp.set_descriptor(i, 12, {rt.opaque_normal->get_image_view(j)}, {buffer_sampler.get()});
            gp.set_descriptor(i, 13, {rt.opaque_accumulation->get_image_view(j)}, {buffer_sampler.get()});
        }
        else
        {
            gp.set_descriptor(i, 11, {rt.transparent_depth->get_image_view(j)}, {buffer_sampler.get()});
            gp.set_descriptor(i, 12, {rt.transparent_normal->get_image_view(j)}, {buffer_sampler.get()});
            gp.set_descriptor(i, 13, {rt.transparent_accumulation->get_image_view(j)}, {buffer_sampler.get()});
        }
        gp.set_descriptor(i, 14, {accumulation_data[i]});
    }
}

void forward_render_stage::init_gather_pass(
    graphics_pipeline& fp,
    const scene& s,
    render_target* color_target,
    render_target* depth_target,
    bool opaque
){
    shader_data sd;

    sd.vertex_bytes = sizeof(forward_vert_shader_binary);
    sd.vertex_data = forward_vert_shader_binary;
    auto spec_entries = s.get_specialization_entries();
    auto spec_data = s.get_specialization_data();

    sd.fragment_bytes = sizeof(gather_frag_shader_binary);
    sd.fragment_data = gather_frag_shader_binary;
    spec_entries.push_back({2, 2*sizeof(uint32_t), sizeof(uint32_t)});
    spec_entries.push_back({3, 3*sizeof(uint32_t), sizeof(uint32_t)});
    spec_entries.push_back({4, 4*sizeof(uint32_t), sizeof(uint32_t)});
    spec_entries.push_back({5, 5*sizeof(uint32_t), sizeof(uint32_t)});
    spec_entries.push_back({6, 6*sizeof(uint32_t), sizeof(uint32_t)});
    spec_data.push_back(opt.shadow_rays);
    spec_data.push_back(opt.reflection_rays);
    spec_data.push_back(opt.refraction_rays);
    spec_data.push_back(opt.secondary_shadows ? 1 : 0);
    spec_data.push_back(color_target->get_samples() != VK_SAMPLE_COUNT_1_BIT ? 1 : 0);

    sd.fragment_specialization.mapEntryCount = spec_entries.size();
    sd.fragment_specialization.pMapEntries = spec_entries.data();
    sd.fragment_specialization.dataSize = spec_data.size() * sizeof(uint32_t);
    sd.fragment_specialization.pData = spec_data.data();

    std::vector<render_target*> targets;
    if(color_target) targets.push_back(color_target);
    if(depth_target) targets.push_back(depth_target);

    graphics_pipeline::params gfx_params(targets);

    if(color_target && !opaque)
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
    for(uint32_t i = 0; i < 3; ++i)
        bindings.push_back({
            11+i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr
        });

    gfx_params.attachments[0].initialLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR;
    gfx_params.attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    gfx_params.attachments[1].initialLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR;
    gfx_params.attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;

    fp.init(
        gfx_params,
        sd,
        ctx->get_image_count(),
        bindings,
        sizeof(push_constants)
    );

    for(uint32_t i = 0; i < ctx->get_image_count(); ++i)
    {
        s.set_descriptors(fp, i);
        fp.set_descriptor(i, 9, {blue_noise.get_image_view(i)}, {brdf_integration_sampler.get()});
        fp.set_descriptor(i, 10, {brdf_integration.get_image_view(i)}, {brdf_integration_sampler.get()});
        if(opaque)
        {
            fp.set_descriptor(i, 11, {rt.opaque_depth->get_image_view(i)}, {buffer_sampler.get()});
            fp.set_descriptor(i, 12, {rt.opaque_normal->get_image_view(i)}, {buffer_sampler.get()});
            fp.set_descriptor(i, 13, {rt.opaque_accumulation->get_image_view(i)}, {buffer_sampler.get()});
        }
        else
        {
            fp.set_descriptor(i, 11, {rt.transparent_depth->get_image_view(i)}, {buffer_sampler.get()});
            fp.set_descriptor(i, 12, {rt.transparent_normal->get_image_view(i)}, {buffer_sampler.get()});
            fp.set_descriptor(i, 13, {rt.transparent_accumulation->get_image_view(i)}, {buffer_sampler.get()});
        }
    }
}

void forward_render_stage::draw_entities(
    uint32_t image_index,
    VkCommandBuffer buf,
    const scene& s,
    graphics_pipeline& gfx,
    int ray_traced,
    int transparent
){
    push_constants pc = {0, 0, 0, 0};
    s.get_ecs().foreach([&](entity id, model& m, visible&, struct ray_traced* rt){
        for(size_t i = 0; i < m.group_count(); ++i)
        {
            pc.instance_id = s.get_entity_instance_id(id, i);
            pc.disable_rt_reflection = rt ? !rt->reflection : true;
            pc.disable_rt_refraction = rt ? !rt->refraction : true;
            bool pot_transparent = m[i].mat.potentially_transparent();
            if(!pc.disable_rt_refraction)
                pot_transparent = false;

            if(
                (ray_traced < 0 || (bool)rt == ray_traced) &&
                (transparent < 0 || pot_transparent == transparent)
            ){
                gfx.push_constants(buf, &pc);
                m[i].mesh->draw(buf);
            }
        }
    });
}
