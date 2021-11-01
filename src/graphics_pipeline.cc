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
        VK_CULL_MODE_NONE,
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
        VK_TRUE,
        VK_TRUE
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
        VK_COMPARE_OP_LESS,
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
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
        };
        VkClearValue clear;
        if(targets[i]->is_depth_stencil())
        {
            clear.depthStencil = {1.0f, 0};
        }
        else
        {
            clear.color = {0.0f, 0.0f, 0.0f, 0.0f};
        }
        clear_values.push_back(clear);
    }
}

void graphics_pipeline::init(
    const params& p,
    const shader_data& sd,
    size_t descriptor_set_count,
    const std::vector<VkDescriptorSetLayoutBinding>& bindings,
    size_t push_constant_size
){
    init_bindings(descriptor_set_count, bindings, push_constant_size);

    // Load shaders
    std::vector<VkPipelineShaderStageCreateInfo> stages;
    VkPipelineShaderStageCreateInfo tmp = {
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        nullptr, {}, {}, VK_NULL_HANDLE, "main", nullptr
    };
    vkres<VkShaderModule> vertex_shader = load_shader(*ctx, sd.vertex_bytes, sd.vertex_data);
    if(vertex_shader != VK_NULL_HANDLE)
    {
        tmp.stage = VK_SHADER_STAGE_VERTEX_BIT;
        tmp.module = vertex_shader;
        stages.push_back(tmp);
    }

    vkres<VkShaderModule> fragment_shader = load_shader(*ctx, sd.fragment_bytes, sd.fragment_data);
    if(fragment_shader != VK_NULL_HANDLE)
    {
        stages.push_back({
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            nullptr, {}, VK_SHADER_STAGE_FRAGMENT_BIT,
            fragment_shader, "main", nullptr
        });
    }

    // Setup fixed function structs
    VkPipelineViewportStateCreateInfo viewport_info = {
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        nullptr, 0, 1, &p.viewport, 1, &p.scissor
    };

    VkPipelineColorBlendStateCreateInfo blend_info = {
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        nullptr,
        0,
        VK_FALSE,
        VK_LOGIC_OP_COPY,
        uint32_t(p.blend_states.size()),
        p.blend_states.data(),
        {0.0f, 0.0f, 0.0f, 0.0f}
    };

    VkPipelineDynamicStateCreateInfo dynamic_info = {
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        nullptr, 0, 0, nullptr
    };

    std::vector<VkAttachmentReference> color;
    std::vector<VkAttachmentReference> depth_stencil;
    for(size_t i = 0; i < p.targets.size(); ++i)
    {
        VkAttachmentReference ref = {(uint32_t)i, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR};
        if(p.targets[i]->is_depth_stencil()) depth_stencil.push_back(ref);
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

    VkRenderPass tmp_render_pass;
    VkRenderPassCreateInfo render_pass_info = {
        VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        nullptr,
        0,
        (uint32_t)p.attachments.size(),
        p.attachments.data(),
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
        &p.vertex_input_info,
        &p.input_assembly_info,
        nullptr,
        &viewport_info,
        &p.rasterization_info,
        &p.multisample_info,
        depth_stencil.empty() ? nullptr : &p.depth_stencil_info,
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

    uvec2 size = p.targets[0]->get_size();

    std::vector<VkImageView> image_views(p.targets.size());
    for(uint32_t i = 0; i < ctx->get_image_count(); ++i)
    {
        for(uint32_t j = 0; j < p.targets.size(); ++j)
        {
            image_views[j] = (*p.targets[j])[i].view;
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

    create_params = p;
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
