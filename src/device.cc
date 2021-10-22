#include "device.hh"
#include <string>
#include <iostream>

device::device(
    VkInstance vulkan,
    VkSurfaceKHR surface,
    const std::vector<const char*>& validation_layers
){
    const char* required_device_extensions[] = {
        VK_KHR_MAINTENANCE1_EXTENSION_NAME,
        VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };

    // Find all physical devices
    uint32_t physical_device_count = 0;
    vkEnumeratePhysicalDevices(vulkan, &physical_device_count, nullptr);
    std::vector<VkPhysicalDevice> physical_devices(physical_device_count);
    vkEnumeratePhysicalDevices(vulkan, &physical_device_count, physical_devices.data());

    // Iterate through devices, looking for the one that supports all the things
    // we require
    bool found_device = false;
    for(VkPhysicalDevice device: physical_devices)
    {
        // Check for extensions
        uint32_t available_count = 0;
        vkEnumerateDeviceExtensionProperties(device, nullptr, &available_count, nullptr);
        std::vector<VkExtensionProperties> properties(available_count);
        vkEnumerateDeviceExtensionProperties(device, nullptr, &available_count, properties.data());

        uint32_t found_all_required = true;
        for(const std::string& required_name: required_device_extensions)
        {
            bool found = false;
            for(VkExtensionProperties props: properties)
            {
                if(required_name == props.extensionName)
                {
                    found = true;
                    break;
                }
            }

            if(!found)
            {
                found_all_required = false;
                break;
            }
        }

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
        physical_device_props = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, nullptr};
        vkGetPhysicalDeviceProperties2(device, &physical_device_props);

        // Found a suitable device
        if(found_all_required && compute_family != -1 && graphics_family != -1
        // && physical_device_props.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU
        ){
            physical_device = device;
            compute_family_index = compute_family;
            graphics_family_index = graphics_family;
            found_device = true;
            break;
        }
    }

    if(!found_device)
        throw std::runtime_error("Failed to find a device suitable for rendering");

    std::cout << "Using " << physical_device_props.properties.deviceName << std::endl;

    // Get features
    physical_device_features = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &vulkan12_features};
    vulkan12_features = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, &sync2_features};
    sync2_features = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR, nullptr};
    vkGetPhysicalDeviceFeatures2(physical_device, &physical_device_features);

    physical_device_features.features.samplerAnisotropy = VK_TRUE;
    vulkan12_features.timelineSemaphore = VK_TRUE;
    vulkan12_features.scalarBlockLayout = VK_TRUE;
    sync2_features.synchronization2 = VK_TRUE;

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
        std::size(required_device_extensions), required_device_extensions,
        nullptr
    };
    vkCreateDevice(physical_device, &device_create_info, nullptr, &logical_device);

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
    vmaCreateAllocator(&allocator_info, &allocator);
}

device::~device()
{
    vmaDestroyAllocator(allocator);
    vkDestroyCommandPool(logical_device, graphics_pool, nullptr);
    vkDestroyCommandPool(logical_device, compute_pool, nullptr);
    vkDestroyDevice(logical_device, nullptr);
}

void device::finish()
{
    vkDeviceWaitIdle(logical_device);
}
