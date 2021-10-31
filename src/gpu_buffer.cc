#include "gpu_buffer.hh"
#include "helpers.hh"

gpu_buffer::gpu_buffer(
    context& ctx,
    size_t bytes,
    VkBufferUsageFlags usage,
    bool single_gpu_buffer
): ctx(&ctx), bytes(bytes)
{
    size_t buf_count = single_gpu_buffer ? 1 : ctx.get_image_count();
    for(size_t i = 0; i < buf_count; ++i)
        buffers.emplace_back(create_gpu_buffer(ctx, bytes, usage|VK_BUFFER_USAGE_TRANSFER_DST_BIT));
    for(size_t i = 0; i < ctx.get_image_count(); ++i)
        staging_buffers.emplace_back(create_cpu_buffer(ctx, bytes));
}

VkBuffer gpu_buffer::operator[](uint32_t image_index) const
{
    return image_index < buffers.size() ? *buffers[image_index] : *buffers.front();
}

void gpu_buffer::update(uint32_t image_index, const void* data, size_t bytes)
{
    if(bytes == 0 || bytes > this->bytes)
        bytes = this->bytes;

    void* dst = nullptr;
    vkres<VkBuffer>& staging = staging_buffers[image_index];
    const VmaAllocator& allocator = ctx->get_device().allocator;
    vmaMapMemory(allocator, staging.get_allocation(), &dst);
    memcpy(dst, data, bytes);
    vmaUnmapMemory(allocator, staging.get_allocation());
}

void gpu_buffer::upload(VkCommandBuffer cmd, uint32_t image_index)
{
    VkBuffer target = operator[](image_index);
    VkBuffer source = staging_buffers[image_index];
    VkBufferCopy copy = {0, 0, bytes};
    vkCmdCopyBuffer(cmd, source, target, 1, &copy);
}
