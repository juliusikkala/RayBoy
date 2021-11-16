#include "gpu_buffer.hh"
#include "helpers.hh"

gpu_buffer::gpu_buffer(
    context& ctx,
    size_t bytes,
    VkBufferUsageFlags usage,
    bool single_gpu_buffer
): ctx(&ctx), bytes(0), single_gpu_buffer(single_gpu_buffer), usage(usage)
{
    resize(bytes);
}

bool gpu_buffer::resize(size_t size)
{
    if(this->bytes >= size) return false;

    this->bytes = size;
    buffers.clear();
    staging_buffers.clear();

    size_t buf_count = single_gpu_buffer ? 1 : ctx->get_image_count();
    for(size_t i = 0; i < buf_count; ++i)
        buffers.emplace_back(create_gpu_buffer(*ctx, bytes, usage|VK_BUFFER_USAGE_TRANSFER_DST_BIT));
    for(size_t i = 0; i < ctx->get_image_count(); ++i)
        staging_buffers.emplace_back(create_cpu_buffer(*ctx, bytes));
    return true;
}

VkBuffer gpu_buffer::operator[](uint32_t image_index) const
{
    return image_index < buffers.size() ? *buffers[image_index] : *buffers.front();
}

VkDeviceAddress gpu_buffer::get_device_address(uint32_t image_index) const
{
    VkBuffer buf = operator[](image_index);
    VkBufferDeviceAddressInfo info = {
        VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, nullptr, buf
    };
    return vkGetBufferDeviceAddress(ctx->get_device().logical_device, &info);
}

void gpu_buffer::update_ptr(uint32_t image_index, const void* data, size_t bytes)
{
    if(staging_buffers.size() == 0) return;

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
    if(buffers.size() > 0)
    {
        VkBuffer target = operator[](image_index);
        VkBuffer source = staging_buffers[image_index];
        VkBufferCopy copy = {0, 0, bytes};
        vkCmdCopyBuffer(cmd, source, target, 1, &copy);
    }
}
