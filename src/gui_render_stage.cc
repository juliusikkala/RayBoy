#include "gui_render_stage.hh"
#include "backends/imgui_impl_vulkan.h"
#include "helpers.hh"

gui_render_stage::gui_render_stage(context& ctx, render_target& target)
: render_stage(ctx), target(target), stage_timer(ctx, "gui_render_stage")
{
    const device& dev = ctx.get_device();
    uvec2 size = ctx.get_size();

    // Create descriptor pool
    // These sizes seem insane to me (especially the maxSets value, WTF?), but
    // they are what the example code for ImGui uses so I'll use them too.
    VkDescriptorPoolSize pool_sizes[] =
    {
        { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
    };
    VkDescriptorPoolCreateInfo pool_create_info = {
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        nullptr,
        VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        (uint32_t)1000 * IM_ARRAYSIZE(pool_sizes),
        (uint32_t)IM_ARRAYSIZE(pool_sizes),
        pool_sizes
    };
    VkDescriptorPool tmp_pool;
    vkCreateDescriptorPool(
        dev.logical_device,
        &pool_create_info,
        nullptr,
        &tmp_pool
    );
    descriptor_pool = vkres(ctx, tmp_pool);

    // Mostly copied from imgui_impl_vulkan.cpp
    {
        VkAttachmentDescription attachment = {};
        attachment.format = target.get_format();
        attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout = target.get_layout();
        attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        VkAttachmentReference color_attachment = {};
        color_attachment.attachment = 0;
        color_attachment.layout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR;
        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &color_attachment;
        VkSubpassDependency dependency = {};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        VkRenderPassCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        info.attachmentCount = 1;
        info.pAttachments = &attachment;
        info.subpassCount = 1;
        info.pSubpasses = &subpass;
        info.dependencyCount = 1;
        info.pDependencies = &dependency;
        VkRenderPass tmp_render_pass;
        vkCreateRenderPass(dev.logical_device, &info, nullptr, &tmp_render_pass);
        render_pass = vkres(ctx, tmp_render_pass);

        target.set_layout(VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    }

    {
        VkImageView attachment[1];
        VkFramebufferCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        info.renderPass = render_pass;
        info.attachmentCount = 1;
        info.pAttachments = attachment;
        info.width = size.x;
        info.height = size.y;
        info.layers = 1;
        for (uint32_t i = 0; i < ctx.get_image_count(); i++)
        {
            attachment[0] = target[i].view;
            VkFramebuffer framebuffer;
            vkCreateFramebuffer(dev.logical_device, &info, nullptr, &framebuffer);
            framebuffers.emplace_back(ctx, framebuffer);
        }
    }

    ImGui_ImplVulkan_InitInfo init_info = {
        ctx.get_instance(),
        dev.physical_device,
        dev.logical_device,
        (uint32_t)dev.graphics_family_index,
        dev.graphics_queue,
        VK_NULL_HANDLE,
        descriptor_pool,
        0,
        2,
        ctx.get_image_count()+1, // There's some in-flight resource destroy issue without the +1.
        VK_SAMPLE_COUNT_1_BIT,
        nullptr,
        nullptr
    };

    ImGui_ImplVulkan_Init(&init_info, render_pass);

    VkCommandBuffer buf = begin_command_buffer(ctx);
    ImGui_ImplVulkan_CreateFontsTexture(buf);
    end_command_buffer(ctx, buf);
    ImGui_ImplVulkan_DestroyFontUploadObjects();
}

gui_render_stage::~gui_render_stage()
{
    clear_commands();
    ctx->sync_flush();
    ImGui_ImplVulkan_Shutdown();
}

void gui_render_stage::update_buffers(uint32_t image_index)
{
    clear_commands();
    VkCommandBuffer buf = graphics_commands(true);
    stage_timer.start(buf, image_index);
    image_barrier(
        buf,
        target[image_index].image,
        target.get_format(),
        target.get_layout(),
        target.get_layout()
    );

    VkRenderPassBeginInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    info.renderPass = render_pass;
    info.framebuffer = framebuffers[image_index];
    uvec2 size = ctx->get_size();
    info.renderArea.extent.width = size.x;
    info.renderArea.extent.height = size.y;
    info.clearValueCount = 0;
    info.pClearValues = nullptr;
    vkCmdBeginRenderPass(buf, &info, VK_SUBPASS_CONTENTS_INLINE);

    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), buf);

    vkCmdEndRenderPass(buf);

    stage_timer.stop(buf, image_index);
    use_graphics_commands(buf, image_index);
}
