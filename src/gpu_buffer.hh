#ifndef RAYBOY_GPU_BUFFER_HH
#define RAYBOY_GPU_BUFFER_HH

#include "context.hh"
#include <vector>

// This class is specifically for easy-to-update GPU buffers; it abstracts 
// staging buffers and in-flight frames. If your buffer doesn't update
// dynamically, this class isn't of much use and is likely just bloat.
class gpu_buffer
{
public:
    gpu_buffer(
        context& ctx,
        size_t bytes,
        VkBufferUsageFlags usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        bool single_gpu_buffer = true
    );

    VkBuffer operator[](uint32_t image_index) const;

    void update(uint32_t image_index, const void* data, size_t bytes = 0);
    template<typename T, typename F>
    void update(uint32_t image_index, F&& f);
    template<typename T>
    void update(uint32_t image_index, const T& t);
    void upload(VkCommandBuffer cmd, uint32_t image_index);

protected:
    context* ctx;
    size_t bytes;
    std::vector<vkres<VkBuffer>> buffers;
    std::vector<vkres<VkBuffer>> staging_buffers;
};

template<typename T, typename F>
void gpu_buffer::update(uint32_t image_index, F&& f)
{
    T* dst = nullptr;
    vkres<VkBuffer>& staging = staging_buffers[image_index];
    const VmaAllocator& allocator = ctx->get_device().allocator;
    vmaMapMemory(allocator, staging.get_allocation(), (void**)&dst);
    f(dst);
    vmaUnmapMemory(allocator, staging.get_allocation());
}

template<typename T>
void gpu_buffer::update(uint32_t image_index, const T& t)
{
    update(image_index, &t, sizeof(T));
}

#endif
