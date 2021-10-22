#include "helpers.hh"

vkres<VkImageView> create_image_view(
    context& ctx,
    VkImage image,
    VkFormat format,
    VkImageAspectFlags aspect
){
    VkImageView view;
    VkImageViewCreateInfo view_info = {
        VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        nullptr,
        {},
        image,
        VK_IMAGE_VIEW_TYPE_2D,
        format,
        {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
         VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY},
        {aspect, 0, 1, 0, 1}
    };
    vkCreateImageView(ctx.get_device().logical_device, &view_info, nullptr, &view);
    return {ctx, view};
}

vkres<VkDescriptorSetLayout> create_descriptor_set_layout(
    context& ctx,
    const std::vector<VkDescriptorSetLayoutBinding>& bindings
){
    VkDescriptorSetLayoutCreateInfo descriptor_set_layout_info = {
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        nullptr, {},
        (uint32_t)bindings.size(), bindings.data()
    };
    VkDescriptorSetLayout layout;
    vkCreateDescriptorSetLayout(
        ctx.get_device().logical_device,
        &descriptor_set_layout_info,
        nullptr,
        &layout
    );
    return {ctx, layout};
}

vkres<VkSemaphore> create_binary_semaphore(context& ctx)
{
    VkSemaphoreCreateInfo sem_info = {
        VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, nullptr, 0
    };
    VkSemaphore sem;
    vkCreateSemaphore(ctx.get_device().logical_device, &sem_info, nullptr, &sem);
    return {ctx, sem};
}

vkres<VkSemaphore> create_timeline_semaphore(context& ctx, uint64_t start_value)
{
    VkSemaphoreTypeCreateInfo sem_type_info = {
      VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO, nullptr,
      VK_SEMAPHORE_TYPE_TIMELINE, 0
    };
    VkSemaphoreCreateInfo sem_info = {
        VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &sem_type_info, 0
    };
    VkSemaphore sem;
    vkCreateSemaphore(ctx.get_device().logical_device, &sem_info, nullptr, &sem);
    return {ctx, sem};
}

void wait_timeline_semaphore(context& ctx, VkSemaphore sem, uint64_t wait_value)
{
    VkSemaphoreWaitInfo wait_info = {
        VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO, nullptr, 0, 1, &sem, &wait_value
    };
    vkWaitSemaphores(ctx.get_device().logical_device, &wait_info, UINT64_MAX);
}

vkres<VkShaderModule> load_shader(context& ctx, size_t bytes, const uint32_t* data)
{
    VkShaderModuleCreateInfo create_info{
        VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        nullptr,
        0,
        bytes,
        data
    };
    VkShaderModule mod;
    vkCreateShaderModule(
        ctx.get_device().logical_device,
        &create_info,
        nullptr,
        &mod
    );
    return {ctx, mod};
}   

vkres<VkBuffer> create_gpu_buffer(context& ctx, size_t bytes, VkBufferUsageFlagBits usage)
{
    VkBufferCreateInfo info = {
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        bytes,
        usage,
        VK_SHARING_MODE_EXCLUSIVE,
        0,
        nullptr
    };
    VmaAllocationCreateInfo alloc_info = {};
    alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    VkBuffer buffer;
    VmaAllocation alloc;
    vmaCreateBuffer(
        ctx.get_device().allocator, &info,
        &alloc_info, &buffer,
        &alloc, nullptr
    );
    return vkres<VkBuffer>(ctx, buffer, alloc);
}

vkres<VkBuffer> create_cpu_buffer(
    context& ctx,
    size_t bytes,
    void* initial_data
){
    VkBufferCreateInfo info = {
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        bytes,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_SHARING_MODE_EXCLUSIVE,
        0,
        nullptr
    };
    VmaAllocationCreateInfo alloc_info = {};
    alloc_info.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

    VkBuffer buffer;
    VmaAllocation alloc;
    vmaCreateBuffer(
        ctx.get_device().allocator, &info,
        &alloc_info, &buffer,
        &alloc, nullptr
    );

    if(initial_data)
    {
        void* mapped = nullptr;
        vmaMapMemory(ctx.get_device().allocator, alloc, &mapped);
        memcpy(mapped, initial_data, bytes);
        vmaUnmapMemory(ctx.get_device().allocator, alloc);
    }

    return vkres<VkBuffer>(ctx, buffer, alloc);
}

void copy_buffer(
    context& ctx,
    VkBuffer dst_buffer,
    VkBuffer src_buffer,
    size_t bytes
){
    VkCommandBuffer buf = begin_command_buffer(ctx);
    VkBufferCopy region = {0, 0, bytes};
    vkCmdCopyBuffer(buf, src_buffer, dst_buffer, 1, &region);
    end_command_buffer(ctx, buf);
}

vkres<VkBuffer> upload_buffer(
    context& ctx,
    size_t bytes,
    void* data,
    VkBufferUsageFlags usage
){
    vkres<VkBuffer> staging = create_cpu_buffer(ctx, bytes, data);

    VkBufferCreateInfo info = {
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0,
        bytes,
        usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_SHARING_MODE_EXCLUSIVE,
        0,
        nullptr
    };
    VmaAllocationCreateInfo alloc_info = {};
    alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    VkBuffer buffer;
    VmaAllocation alloc;
    vmaCreateBuffer(
        ctx.get_device().allocator, &info,
        &alloc_info, &buffer,
        &alloc, nullptr
    );
    copy_buffer(ctx, buffer, staging, bytes);
    return vkres<VkBuffer>(ctx, buffer, alloc);
}

VkCommandBuffer begin_command_buffer(context& ctx)
{
    VkCommandBufferAllocateInfo command_buffer_alloc_info = {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        nullptr,
        ctx.get_device().graphics_pool,
        VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        1
    };
    VkCommandBuffer buf;
    vkAllocateCommandBuffers(
        ctx.get_device().logical_device,
        &command_buffer_alloc_info,
        &buf
    );
    VkCommandBufferBeginInfo begin_info = {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        nullptr,
        VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        nullptr
    };
    vkBeginCommandBuffer(buf, &begin_info);
    return buf;
}

void end_command_buffer(context& ctx, VkCommandBuffer buf)
{
    vkEndCommandBuffer(buf);
    VkSubmitInfo2KHR info = {
        VK_STRUCTURE_TYPE_SUBMIT_INFO_2_KHR,
        nullptr,
        0,
        0, nullptr,
        0, nullptr,
        0, nullptr
    };
    vkQueueSubmit2KHR(ctx.get_device().graphics_queue, 1, &info, VK_NULL_HANDLE);
    ctx.get_device().finish();
    vkFreeCommandBuffers(
        ctx.get_device().logical_device,
        ctx.get_device().graphics_pool,
        1,
        &buf
    );
}

std::vector<VkDescriptorPoolSize> calculate_descriptor_pool_sizes(
    size_t binding_count,
    const VkDescriptorSetLayoutBinding* bindings,
    uint32_t multiplier
){
    std::vector<VkDescriptorPoolSize> pool_sizes;
    for(size_t i = 0; i < binding_count; ++i)
    {
        VkDescriptorSetLayoutBinding b = bindings[i];
        bool found = false;
        for(VkDescriptorPoolSize& size: pool_sizes)
        {
            if(size.type == b.descriptorType)
            {
                found = true;
                size.descriptorCount += b.descriptorCount * multiplier;
            }
        }

        if(!found)
        {   
            pool_sizes.push_back({b.descriptorType, b.descriptorCount * multiplier});
        }
    }
    return pool_sizes;
}

void image_barrier(
    VkCommandBuffer cmd,
    VkImage image,
    VkImageLayout layout_before,
    VkImageLayout layout_after,
    VkAccessFlags2KHR happens_before,
    VkAccessFlags2KHR happens_after,
    VkPipelineStageFlags2KHR stage_before,
    VkPipelineStageFlags2KHR stage_after
){
    VkImageMemoryBarrier2KHR image_barrier = {
        VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2_KHR,
        nullptr,
        stage_before,
        happens_before,
        stage_after,
        happens_after,
        layout_before,
        layout_after,
        VK_QUEUE_FAMILY_IGNORED,
        VK_QUEUE_FAMILY_IGNORED,
        image,
        {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
    };
    VkDependencyInfoKHR dependency_info = {
        VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR,
        nullptr,
        0,
        0, nullptr,
        0, nullptr,
        1, &image_barrier
    };
    vkCmdPipelineBarrier2KHR(cmd, &dependency_info);
}
