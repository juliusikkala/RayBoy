#ifndef RAYBOY_GPU_BUFFER_HH
#define RAYBOY_GPU_BUFFER_HH

#include "context.hh"
#include <vector>
#include <type_traits>

// This class is specifically for easy-to-update GPU buffers; it abstracts 
// staging buffers and in-flight frames. If your buffer doesn't update
// dynamically, this class isn't of much use and is likely just bloat.
class gpu_buffer
{
public:
    gpu_buffer(
        context& ctx,
        size_t bytes = 0,
        VkBufferUsageFlags usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        bool single_gpu_buffer = true
    );

    // Returns true if the resize truly occurred
    bool resize(size_t size);

    VkBuffer operator[](uint32_t image_index) const;
    VkDeviceAddress get_device_address(uint32_t image_index) const;

    void update_ptr(uint32_t image_index, const void* data, size_t bytes = 0);
    template<typename T>
    void update(uint32_t image_index, const T& t);
    template<typename T, typename F>
    void update(uint32_t image_index, F&& f);
    void upload(VkCommandBuffer cmd, uint32_t image_index);

protected:
    context* ctx;
    size_t bytes;
    bool single_gpu_buffer;
    VkBufferUsageFlags usage;
    std::vector<vkres<VkBuffer>> buffers;
    std::vector<vkres<VkBuffer>> staging_buffers;
};

template<typename T, typename F>
void gpu_buffer::update(uint32_t image_index, F&& f)
{
    if(staging_buffers.size() == 0) return;

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
    if constexpr(std::is_pointer_v<T>)
    {
        update_ptr(image_index, t, bytes);
    }
    else
    {
        update_ptr(image_index, &t, sizeof(T));
    }
}

#endif
