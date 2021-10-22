#ifndef RAYBOY_VKRES_HH
#define RAYBOY_VKRES_HH

#include "volk.h"

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

private:
    VkCommandBuffer value;
    VkCommandPool pool;
    context* ctx;
};

#endif
