#include "device.hh"
#include <string>
#include <iostream>
#include <cassert>

namespace
{

bool has_all_extensions(
    const std::vector<VkExtensionProperties>& props,
    const char** extensions,
    size_t extension_count
){
    for(size_t i = 0; i < extension_count; ++i)
    {
        std::string required_name = extensions[i];
        bool found = false;
        for(VkExtensionProperties props: props)
        {
            if(required_name == props.extensionName)
            {
                found = true;
                break;
            }
        }

        if(!found)
            return false;
    }
    return true;
}

}

device::device(
    VkInstance vulkan,
    VkSurfaceKHR surface,
    const std::vector<const char*>& validation_layers
){
    const char* device_extensions[] = {
        VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
        VK_KHR_RAY_QUERY_EXTENSION_NAME,
        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
        VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME
    };
    const uint32_t required_extension_count = 2;
    const uint32_t rt_extension_count = required_extension_count + 4;

    // Find all physical devices
    uint32_t physical_device_count = 0;
    vkEnumeratePhysicalDevices(vulkan, &physical_device_count, nullptr);
    std::vector<VkPhysicalDevice> physical_devices(physical_device_count);
    vkEnumeratePhysicalDevices(vulkan, &physical_device_count, physical_devices.data());

    // Iterate through devices, looking for the one that supports all the things
    // we require
    bool found_device = false;
    bool found_rt_device = false;
    bool found_discrete_device = false;
    for(VkPhysicalDevice device: physical_devices)
    {
        // Check for extensions
        uint32_t available_count = 0;
        vkEnumerateDeviceExtensionProperties(device, nullptr, &available_count, nullptr);
        std::vector<VkExtensionProperties> extensions(available_count);
        vkEnumerateDeviceExtensionProperties(device, nullptr, &available_count, extensions.data());

        bool found_all_required = true;
        if(!has_all_extensions(extensions, device_extensions, required_extension_count))
            continue;

        bool current_has_rt = has_all_extensions(extensions, device_extensions, std::size(device_extensions));
        bool current_is_discrete = physical_device_props.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;

        // Find required queue families
        uint32_t queue_family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, nullptr);
        std::vector<VkQueueFamilyProperties> families(queue_family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, families.data());

        int32_t compute_family = -1;
        int32_t graphics_family = -1;

        for(uint32_t i = 0; i < queue_family_count; ++i)
        {
            VkQueueFamilyProperties props = families[i];
            if(props.queueFlags & VK_QUEUE_COMPUTE_BIT)
            {
                compute_family = i;
            }

            if(props.queueFlags & VK_QUEUE_GRAPHICS_BIT)
            {
                VkBool32 has_present = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &has_present);

                if(has_present)
                {
                    graphics_family = i;
                }
            }
        }

        // Get properties
        VkPhysicalDeviceProperties2 properties = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &as_properties};
        as_properties = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR, nullptr};
        vkGetPhysicalDeviceProperties2(device, &properties);

        // Found a suitable device
        if(
            found_all_required &&
            compute_family != -1 &&
            graphics_family != -1 &&
            (!found_rt_device || current_has_rt) &&
            (!found_discrete_device || current_is_discrete)
        ){
            physical_device = device;
            physical_device_props = properties;
            compute_family_index = compute_family;
            graphics_family_index = graphics_family;
            found_device = true;
            found_rt_device = current_has_rt;
            found_discrete_device = current_is_discrete;
        }
    }

    if(!found_device)
        throw std::runtime_error("Failed to find a device suitable for rendering");

    if(physical_device_props.properties.vendorID == 4098)
    {
        // AMD is being too nitpicky about something with the acceleration structure building :/
        found_rt_device = false;
    }

    std::cout << "Using " << physical_device_props.properties.deviceName << std::endl;
    supports_ray_tracing = found_rt_device;
    std::cout << "Ray tracing " << (supports_ray_tracing ? "enabled" : "disabled") << std::endl;

    // Get features
    physical_device_features = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &vulkan12_features};
    vulkan12_features = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, &sync2_features};
    sync2_features = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR, &rq_features};
    rq_features = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR, &as_features};
    as_features = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR, nullptr};
    vkGetPhysicalDeviceFeatures2(physical_device, &physical_device_features);

    if(!found_rt_device)
        sync2_features.pNext = nullptr;

    physical_device_features.features.samplerAnisotropy = VK_TRUE;
    vulkan12_features.timelineSemaphore = VK_TRUE;
    vulkan12_features.scalarBlockLayout = VK_TRUE;
    sync2_features.synchronization2 = VK_TRUE;
    if (supports_ray_tracing)
        vulkan12_features.bufferDeviceAddress = VK_TRUE;
    rq_features.rayQuery = VK_TRUE;
    as_features.accelerationStructure = VK_TRUE;

    // Create device
    float priority = 1.0f;
    VkDeviceQueueCreateInfo queue_infos[] = {
        // Graphics queue
        {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, nullptr, {}, (uint32_t)graphics_family_index, 1, &priority},
        // Compute queue
        {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, nullptr, {}, (uint32_t)compute_family_index, 1, &priority}
    };

    VkDeviceCreateInfo device_create_info = {
        VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        &physical_device_features,
        {},
        std::size(queue_infos), queue_infos,
        (uint32_t)validation_layers.size(), validation_layers.data(),
        found_rt_device ? rt_extension_count : required_extension_count, device_extensions,
        nullptr
    };
    VkResult res = vkCreateDevice(physical_device, &device_create_info, nullptr, &logical_device);

    // Get queues
    vkGetDeviceQueue(logical_device, graphics_family_index, 0, &graphics_queue);
    vkGetDeviceQueue(logical_device, compute_family_index, 0, &compute_queue);

    // Get pools
    VkCommandPoolCreateInfo graphics_pool_info = {
        VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr, {},
        (uint32_t)graphics_family_index
    };
    VkCommandPoolCreateInfo compute_pool_info = {
        VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr, {},
        (uint32_t)compute_family_index
    };
    vkCreateCommandPool(logical_device, &graphics_pool_info, nullptr, &graphics_pool);
    vkCreateCommandPool(logical_device, &compute_pool_info, nullptr, &compute_pool);

    // Create memory allocator
    VmaAllocatorCreateInfo allocator_info = {};
    allocator_info.physicalDevice = physical_device;
    allocator_info.device = logical_device;
    allocator_info.instance = vulkan;
    allocator_info.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    if(found_rt_device)
        allocator_info.flags |= VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    vmaCreateAllocator(&allocator_info, &allocator);

    // Save extra details
    available_sample_counts =
        physical_device_props.properties.limits.framebufferColorSampleCounts &
        physical_device_props.properties.limits.framebufferDepthSampleCounts;
}

device::~device()
{
    vmaDestroyAllocator(allocator);
    vkDestroyCommandPool(logical_device, graphics_pool, nullptr);
    vkDestroyCommandPool(logical_device, compute_pool, nullptr);
    vkDestroyDevice(logical_device, nullptr);
}

void device::finish() const
{
    assert(vkDeviceWaitIdle(logical_device) == VK_SUCCESS);
}
