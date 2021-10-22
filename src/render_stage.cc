#include "render_stage.hh"
#include "helpers.hh"
#include <cassert>

render_stage::render_stage(context& ctx)
: ctx(&ctx)
{
    // Create semaphore
    finished = create_timeline_semaphore(ctx);
}

render_stage::~render_stage()
{
}

VkSemaphore render_stage::run(uint32_t image_index, VkSemaphore wait) const
{
    uint64_t frame_counter = ctx->get_frame_counter();

    VkSemaphoreSubmitInfoKHR wait_info = {
        VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO_KHR, nullptr,
        wait, frame_counter, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0
    };
    VkCommandBufferSubmitInfoKHR command_info = {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO_KHR,
        nullptr,
        *command_buffers[image_index],
        0
    };
    VkSemaphoreSubmitInfoKHR signal_info = {
        VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO_KHR, nullptr,
        *finished, frame_counter, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0
    };
    VkSubmitInfo2KHR submit_info = {
        VK_STRUCTURE_TYPE_SUBMIT_INFO_2_KHR,
        nullptr, 0,
        1, &wait_info,
        1, &command_info,
        1, &signal_info
    };
    vkQueueSubmit2KHR(ctx->get_device().compute_queue, 1, &submit_info, VK_NULL_HANDLE);

    return *finished;
}

void render_stage::init_bindings(
    size_t count, const std::vector<VkDescriptorSetLayoutBinding>& bindings
){
    this->bindings = bindings;
    descriptor_set_layout = create_descriptor_set_layout(*ctx, bindings);

    VkDevice logical_device = ctx->get_device().logical_device;

    // Create pipeline layout
    VkPipelineLayoutCreateInfo pipeline_layout_info = {
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        nullptr, {},
        1, &*descriptor_set_layout,
        0, nullptr // TODO: Push constant support
    };
    VkPipelineLayout tmp_layout;
    vkCreatePipelineLayout(
        logical_device, &pipeline_layout_info, nullptr, &tmp_layout
    );
    pipeline_layout = vkres(*ctx, tmp_layout);

    // Create descriptor pool
    std::vector<VkDescriptorPoolSize> pool_sizes = calculate_descriptor_pool_sizes(
        bindings.size(), bindings.data(), count
    );
    VkDescriptorPoolCreateInfo pool_create_info = {
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        nullptr,
        0,
        (uint32_t)count,
        (uint32_t)pool_sizes.size(),
        pool_sizes.data()
    };
    VkDescriptorPool tmp_pool;
    vkCreateDescriptorPool(
        logical_device, 
        &pool_create_info,
        nullptr,
        &tmp_pool
    );
    descriptor_pool = vkres(*ctx, tmp_pool);

    // Create descriptor sets
    VkDescriptorSetAllocateInfo descriptor_alloc_info = {
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        nullptr,
        descriptor_pool,
        1,
        &*descriptor_set_layout
    };

    descriptor_sets.resize(count);
    for(size_t i = 0; i < descriptor_sets.size(); ++i)
    {
        vkAllocateDescriptorSets(
            logical_device,
            &descriptor_alloc_info,
            &descriptor_sets[i]
        );
    }
}

void render_stage::init_compute_pipeline(size_t bytes, const uint32_t* data)
{
    vkres<VkShaderModule> shader = load_shader(*ctx, bytes, data);
    VkPipelineShaderStageCreateInfo shader_info = {
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        nullptr,
        {},
        VK_SHADER_STAGE_COMPUTE_BIT,
        shader,
        "main",
        nullptr
    };
    VkComputePipelineCreateInfo pipeline_info = {
        VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        nullptr,
        {},
        shader_info,
        pipeline_layout,
        VK_NULL_HANDLE,
        0
    };
    VkPipeline pipeline;
    vkCreateComputePipelines(
        ctx->get_device().logical_device,
        VK_NULL_HANDLE,
        1,
        &pipeline_info,
        nullptr,
        &pipeline
    );
    this->pipeline = vkres(*ctx, pipeline);
}

void render_stage::set_descriptor(
    size_t set_index, size_t binding_index,
    std::vector<VkImageView> views, std::vector<VkSampler> samplers
){
    VkDescriptorSetLayoutBinding bind = find_binding(binding_index);
    std::vector<VkDescriptorImageInfo> image_infos(bind.descriptorCount);

    assert(views.size() == bind.descriptorCount);

    for(size_t i = 0; i < bind.descriptorCount; ++i)
    {
        VkSampler sampler = VK_NULL_HANDLE;
        if(samplers.size() == 1)
            sampler = samplers[0];
        else if(samplers.size() > 1)
        {
            assert(samplers.size() == bind.descriptorCount);
            sampler = samplers[i];
        }
        image_infos[i] = VkDescriptorImageInfo{
            sampler,
            views[i],
            bind.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE ?
                VK_IMAGE_LAYOUT_GENERAL :
                VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR
        };
    }
    VkWriteDescriptorSet write = {
        VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
        descriptor_sets[set_index], (uint32_t)binding_index, 0,
        bind.descriptorCount, bind.descriptorType,
        image_infos.data(), nullptr, nullptr
    };
    vkUpdateDescriptorSets(ctx->get_device().logical_device, 1, &write, 0,  nullptr);
}

VkDescriptorSetLayoutBinding render_stage::find_binding(size_t binding_index) const
{
    for(const VkDescriptorSetLayoutBinding& bind: bindings)
    {
        if(bind.binding == binding_index)
        {
            return bind;
        }
    }
    assert(false && "Missing binding index");
}

VkCommandBuffer render_stage::begin_compute_commands(size_t set_index)
{
    VkCommandBufferAllocateInfo command_buffer_alloc_info = {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        nullptr,
        ctx->get_device().compute_pool,
        VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        1
    };
    VkCommandBuffer buf;
    vkAllocateCommandBuffers(
        ctx->get_device().logical_device,
        &command_buffer_alloc_info,
        &buf
    );
    VkCommandBufferBeginInfo begin_info = {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        nullptr,
        VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT,
        nullptr
    };
    vkBeginCommandBuffer(buf, &begin_info);

    vkCmdBindPipeline(buf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(
        buf, 
        VK_PIPELINE_BIND_POINT_COMPUTE,
        *pipeline_layout,
        0, 1, &descriptor_sets[set_index],
        0, nullptr
    );

    return buf;
}

void render_stage::finish_compute_commands(VkCommandBuffer buf)
{
    vkEndCommandBuffer(buf);
    command_buffers.emplace_back(*ctx, ctx->get_device().compute_pool, buf);
}
