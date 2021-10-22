#ifndef RAYBOY_DEVICE_HH
#define RAYBOY_DEVICE_HH

#include "volk.h"
#include "vk_mem_alloc.h"
#include <vector>

struct device
{
    device(
        VkInstance vulkan,
        VkSurfaceKHR surface,
        const std::vector<const char*>& validation_layers
    );
    device(device& other) = delete;
    device(device&& other) = delete;
    ~device();

    void finish();

    VkPhysicalDevice physical_device;
    VkDevice logical_device;
    VkPhysicalDeviceProperties2 physical_device_props;
    VkPhysicalDeviceFeatures2 physical_device_features;
    VkPhysicalDeviceVulkan12Features vulkan12_features;
    VkPhysicalDeviceSynchronization2FeaturesKHR sync2_features;
    int32_t compute_family_index;
    int32_t graphics_family_index;
    VkQueue graphics_queue;
    VkQueue compute_queue;
    VkCommandPool graphics_pool;
    VkCommandPool compute_pool;
    VmaAllocator allocator;
};

#endif
