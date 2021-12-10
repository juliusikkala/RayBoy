#include "graphics_pipeline.hh"
#include "mesh.hh"
#include "helpers.hh"

graphics_pipeline::params::params(const std::vector<render_target*>& targets)
: targets(targets)
{
    vertex_input_info = {
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        nullptr,
        0,
        std::size(mesh::bindings),
        mesh::bindings,
        std::size(mesh::attributes),
        mesh::attributes
    };

    input_assembly_info = {
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        nullptr,
        0,
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        VK_FALSE
    };

    uvec2 size = targets[0]->get_size();
    viewport = {0.f, float(size.y), float(size.x), -float(size.y), 0.f, 1.f};
    scissor = {{0, 0}, {size.x, size.y}};

    rasterization_info = {
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        nullptr,
        0,
        VK_FALSE,
        VK_FALSE,
        VK_POLYGON_MODE_FILL,
        VK_CULL_MODE_BACK_BIT,
        VK_FRONT_FACE_COUNTER_CLOCKWISE,
        VK_FALSE, 0.0f, 0.0f, 0.0f,
        0.0f
    };

    multisample_info = {
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        nullptr,
        0,
        targets[0]->get_samples(),
        VK_FALSE,
        1.0f,
        nullptr,
        VK_FALSE,
        VK_FALSE
    };

    bool has_depth_stencil = false;
    for(render_target* rt: targets)
        has_depth_stencil |= rt->is_depth_stencil();

    depth_stencil_info = {
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        nullptr,
        0,
        has_depth_stencil ? VK_TRUE : VK_FALSE,
        has_depth_stencil ? VK_TRUE : VK_FALSE,
        VK_COMPARE_OP_LESS_OR_EQUAL,
        VK_FALSE,
        VK_FALSE,
        {},
        {},
        0.0f, 1.0f
    };

    blend_states = std::vector(
        targets.size() - (has_depth_stencil ? 1:0),
        VkPipelineColorBlendAttachmentState{
            VK_FALSE,
            VK_BLEND_FACTOR_SRC_ALPHA,
            VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
            VK_BLEND_OP_ADD,
            VK_BLEND_FACTOR_ONE,
            VK_BLEND_FACTOR_ZERO,
            VK_BLEND_OP_ADD,
            VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|
            VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT
        }
    );

    attachments = std::vector<VkAttachmentDescription>(targets.size());
    clear_values = std::vector<VkClearValue>(targets.size());
    for(size_t i = 0; i < targets.size(); ++i)
    {
        attachments[i] = VkAttachmentDescription{
            0,
            targets[i]->get_format(),
            targets[i]->get_samples(),
            VK_ATTACHMENT_LOAD_OP_CLEAR,
            VK_ATTACHMENT_STORE_OP_STORE,
            VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            VK_ATTACHMENT_STORE_OP_DONT_CARE,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR
        };
        VkClearValue clear;
        if(targets[i]->is_depth_stencil())
        {
            attachments[i].initialLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR;
            attachments[i].finalLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR;
            clear.depthStencil = {1.0f, 0};
        }
        else
        {
            clear.color = {1.0f, 0.0f, 0.0f, 1.0f};
        }
        clear_values[i] = clear;
    }
}

void graphics_pipeline::init(
    const params& p,
    const shader_data& sd,
    size_t descriptor_set_count,
    const std::vector<VkDescriptorSetLayoutBinding>& bindings,
    size_t push_constant_size
){
    create_params = p;

    // AMD Fix: MSAA is broken by default, so force sample shading with the matching minSampleShading
    if (ctx->get_device().physical_device_props.properties.vendorID == 4098)
    {
        create_params.multisample_info.sampleShadingEnable = VK_TRUE;
        create_params.multisample_info.minSampleShading = clamp(1.0f / create_params.multisample_info.rasterizationSamples + 0.01f, 0.0f, 1.0f);
    }

    init_bindings(descriptor_set_count, bindings, push_constant_size);

    // Load shaders
    std::vector<VkPipelineShaderStageCreateInfo> stages;
    vkres<VkShaderModule> vertex_shader = load_shader(*ctx, sd.vertex_bytes, sd.vertex_data);
    if(vertex_shader != VK_NULL_HANDLE)
    {
        stages.push_back({
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            nullptr, {}, VK_SHADER_STAGE_VERTEX_BIT, vertex_shader,
            "main", &sd.vertex_specialization
        });
    }

    vkres<VkShaderModule> fragment_shader = load_shader(*ctx, sd.fragment_bytes, sd.fragment_data);
    if(fragment_shader != VK_NULL_HANDLE)
    {
        stages.push_back({
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            nullptr, {}, VK_SHADER_STAGE_FRAGMENT_BIT,
            fragment_shader, "main", &sd.fragment_specialization
        });
    }

    // Setup fixed function structs
    VkPipelineViewportStateCreateInfo viewport_info = {
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        nullptr, 0, 1, &create_params.viewport, 1, &create_params.scissor
    };

    VkPipelineColorBlendStateCreateInfo blend_info = {
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        nullptr,
        0,
        VK_FALSE,
        VK_LOGIC_OP_COPY,
        uint32_t(create_params.blend_states.size()),
        create_params.blend_states.data(),
        {0.0f, 0.0f, 0.0f, 0.0f}
    };

    VkPipelineDynamicStateCreateInfo dynamic_info = {
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        nullptr, 0, 0, nullptr
    };

    std::vector<VkAttachmentReference> color;
    std::vector<VkAttachmentReference> depth_stencil;
    for(size_t i = 0; i < create_params.targets.size(); ++i)
    {
        VkAttachmentReference ref = {(uint32_t)i, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR};
        if(create_params.targets[i]->is_depth_stencil()) depth_stencil.push_back(ref);
        else color.push_back(ref);
    }

    VkSubpassDescription subpass = {
        0,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        0, nullptr,
        (uint32_t)color.size(), color.data(),
        nullptr,
        depth_stencil.empty() ? nullptr : depth_stencil.data(),
        0,
        nullptr
    };

    VkSubpassDependency subpass_dependency = {
        VK_SUBPASS_EXTERNAL, 0,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        0,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        0
    };
    if(!depth_stencil.empty())
    {
        subpass_dependency.srcStageMask |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        subpass_dependency.dstStageMask |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        subpass_dependency.dstAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    }

    VkRenderPass tmp_render_pass;
    VkRenderPassCreateInfo render_pass_info = {
        VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        nullptr,
        0,
        (uint32_t)create_params.attachments.size(),
        create_params.attachments.data(),
        1,
        &subpass,
        1,
        &subpass_dependency
    };
    vkCreateRenderPass(
        ctx->get_device().logical_device,
        &render_pass_info,
        nullptr,
        &tmp_render_pass
    );
    render_pass = vkres(*ctx, tmp_render_pass);

    VkGraphicsPipelineCreateInfo pipeline_info = {
        VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        nullptr,
        0,
        (uint32_t)stages.size(),
        stages.data(),
        &create_params.vertex_input_info,
        &create_params.input_assembly_info,
        nullptr,
        &viewport_info,
        &create_params.rasterization_info,
        &create_params.multisample_info,
        depth_stencil.empty() ? nullptr : &create_params.depth_stencil_info,
        &blend_info,
        &dynamic_info,
        pipeline_layout,
        *render_pass,
        0,
        VK_NULL_HANDLE,
        -1
    };

    VkPipeline pipeline;
    vkCreateGraphicsPipelines(
        ctx->get_device().logical_device,
        VK_NULL_HANDLE,
        1,
        &pipeline_info,
        nullptr,
        &pipeline
    );
    this->pipeline = vkres(*ctx, pipeline);

    uvec2 size = create_params.targets[0]->get_size();

    std::vector<VkImageView> image_views(create_params.targets.size());
    for(uint32_t i = 0; i < ctx->get_image_count(); ++i)
    {
        for(uint32_t j = 0; j < create_params.targets.size(); ++j)
        {
            create_params.targets[j]->set_layout(create_params.attachments[j].finalLayout);
            image_views[j] = (*create_params.targets[j])[i].view;
        }
        VkFramebufferCreateInfo framebuffer_info = {
            VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            nullptr,
            0,
            render_pass,
            (uint32_t)image_views.size(),
            image_views.data(),
            size.x,
            size.y,
            1
        };
        VkFramebuffer fb;
        vkCreateFramebuffer(ctx->get_device().logical_device, &framebuffer_info, nullptr, &fb);
        framebuffers.emplace_back(*ctx, fb);
    }
}

void graphics_pipeline::begin_render_pass(
    VkCommandBuffer buf,
    uint32_t image_index
){
    uvec2 size = create_params.targets[0]->get_size();
    VkRenderPassBeginInfo render_pass_info = {
        VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        nullptr,
        render_pass,
        framebuffers[image_index],
        {{0,0}, {size.x, size.y}},
        (uint32_t)create_params.clear_values.size(),
        create_params.clear_values.data()
    };
    vkCmdBeginRenderPass(buf, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);
}

void graphics_pipeline::end_render_pass(VkCommandBuffer buf)
{
    vkCmdEndRenderPass(buf);
}

void graphics_pipeline::bind(VkCommandBuffer buf, size_t set_index)
{
    vkCmdBindPipeline(buf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(
        buf,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        *pipeline_layout,
        0, 1, &descriptor_sets[set_index],
        0, nullptr
    );
}
