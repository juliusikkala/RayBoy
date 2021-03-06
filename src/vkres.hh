#ifndef RAYBOY_VKRES_HH
#define RAYBOY_VKRES_HH

#include "volk.h"
#include "vk_mem_alloc.h"

class context;

// Automatic safely deleted Vulkan resources
template<typename T>
class vkres
{
public:
    vkres();
    vkres(context& ctx, T t = VK_NULL_HANDLE);
    vkres(vkres<T>& other) = delete;
    vkres(vkres<T>&& other);
    ~vkres();

    void reset(T other = VK_NULL_HANDLE);
    void operator=(vkres<T>&& other);

    const T& operator*() const;
    operator T();

private:
    T value;
    context* ctx;
};

template<>
class vkres<VkCommandBuffer>
{
public:
    vkres();
    vkres(context& ctx, VkCommandPool pool, VkCommandBuffer buf = VK_NULL_HANDLE);
    vkres(vkres<VkCommandBuffer>& other) = delete;
    vkres(vkres<VkCommandBuffer>&& other);
    ~vkres();

    void reset(VkCommandBuffer other = VK_NULL_HANDLE);
    void operator=(vkres<VkCommandBuffer>&& other);

    const VkCommandBuffer& operator*() const;
    operator VkCommandBuffer();

    VkCommandPool get_pool() const;

private:
    VkCommandBuffer value;
    VkCommandPool pool;
    context* ctx;
};

template<>
class vkres<VkBuffer>
{
public:
    vkres();
    vkres(context& ctx, VkBuffer buf = VK_NULL_HANDLE, VmaAllocation alloc = VK_NULL_HANDLE);
    vkres(vkres<VkBuffer>& other) = delete;
    vkres(vkres<VkBuffer>&& other);
    ~vkres();

    void reset(VkBuffer buf = VK_NULL_HANDLE, VmaAllocation alloc = VK_NULL_HANDLE);
    void operator=(vkres<VkBuffer>&& other);

    const VkBuffer& operator*() const;
    operator VkBuffer();

    VmaAllocation get_allocation() const;

private:
    VkBuffer buffer;
    VmaAllocation allocation;
    context* ctx;
};

template<>
class vkres<VkImage>
{
public:
    vkres();
    vkres(context& ctx, VkImage img = VK_NULL_HANDLE, VmaAllocation alloc = VK_NULL_HANDLE);
    vkres(context& ctx, VkImage img, VkDeviceMemory memory);
    vkres(vkres<VkImage>& other) = delete;
    vkres(vkres<VkImage>&& other);
    ~vkres();

    void reset(VkImage img = VK_NULL_HANDLE, VmaAllocation alloc = VK_NULL_HANDLE);
    void operator=(vkres<VkImage>&& other);

    const VkImage& operator*() const;
    operator VkImage();

private:
    VkImage image;
    VmaAllocation allocation;
    VkDeviceMemory memory;
    context* ctx;
};


#endif
