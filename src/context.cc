#include "context.hh"
#include "helpers.hh"
#include <stdexcept>
#include <iostream>
#include <algorithm>

context::context(
    ivec2 size,
    bool fullscreen,
    bool vsync,
    bool grab_mouse
): size(size), vsync(vsync) {
    init_sdl(fullscreen, grab_mouse);
    init_vulkan();
    if(!SDL_Vulkan_CreateSurface(win, vulkan, &surface))
        throw std::runtime_error(SDL_GetError());
    dev.reset(new device(vulkan, surface, validation_layers));
    init_swapchain();
}

context::~context()
{
    dev->finish();
    deinit_swapchain();
    reap.flush();
    dev.reset();
    vkDestroySurfaceKHR(vulkan, surface, nullptr);
    deinit_vulkan();
    deinit_sdl();
}

const device& context::get_device() const
{
    return *dev;
}

bool context::start_frame()
{
    frame_counter++;
    reap.start_frame();

    // This is the binary semaphore we will be using
    VkSemaphore sem = binary_start_semaphores[frame_counter%binary_start_semaphores.size()];

    // Wait until that semaphore cannot be in use anymore
    if(frame_counter >= binary_start_semaphores.size())
    {
        wait_timeline_semaphore(
            *this, frame_start_semaphore,
            frame_counter - (binary_start_semaphores.size() - 1)
        );
        reap.finish_frame();
    }

    // Get next swapchain image index
    VkResult res = vkAcquireNextImageKHR(
        dev->logical_device, swapchain, UINT64_MAX, sem, VK_NULL_HANDLE,
        &image_index
    );
    if(res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR)
    {
        return true;
    }

    // Convert the binary semaphore into a timeline semaphore
    VkSemaphoreSubmitInfoKHR wait_info = {
        VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO_KHR, nullptr,
        sem, 0, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT_KHR, 0
    };
    VkSemaphoreSubmitInfoKHR signal_info = {
        VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO_KHR, nullptr,
        frame_start_semaphore, frame_counter,
        VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT_KHR, 0
    };
    VkSubmitInfo2KHR submit_info = {
        VK_STRUCTURE_TYPE_SUBMIT_INFO_2_KHR,
        nullptr, 0,
        1, &wait_info,
        0, nullptr,
        1, &signal_info
    };
    vkQueueSubmit2KHR(dev->graphics_queue, 1, &submit_info, VK_NULL_HANDLE);
    return false;
}

uint32_t context::get_image_index() const
{
    return image_index;
}

uint32_t context::get_image_count() const
{
    return swapchain_images.size();
}

render_target context::get_render_target() const
{
    std::vector<render_target::frame> frames(swapchain_images.size());
    for(size_t i = 0; i < swapchain_images.size(); ++i)
    {
        frames[i].image = swapchain_images[i];
        frames[i].view = *swapchain_image_views[i];
        frames[i].layout = VK_IMAGE_LAYOUT_UNDEFINED;
    }
    return render_target(frames);
}

const std::vector<VkImage>& context::get_images() const
{
    return swapchain_images;
}

VkSemaphore context::get_start_semaphore()
{
    return frame_start_semaphore;
}

void context::finish_frame(VkSemaphore wait)
{
    VkSemaphore sem = binary_finish_semaphores[frame_counter%binary_finish_semaphores.size()];

    // Convert the input timeline semaphore into a binary semaphore
    VkSemaphoreSubmitInfoKHR wait_info = {
        VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO_KHR, nullptr,
        wait, frame_counter, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT_KHR, 0
    };
    VkSemaphoreSubmitInfoKHR signal_info = {
        VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO_KHR, nullptr,
        sem, 0, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT_KHR, 0
    };
    VkSubmitInfo2KHR submit_info = {
        VK_STRUCTURE_TYPE_SUBMIT_INFO_2_KHR,
        nullptr, 0,
        1, &wait_info,
        0, nullptr,
        1, &signal_info
    };
    vkQueueSubmit2KHR(dev->graphics_queue, 1, &submit_info, VK_NULL_HANDLE);

    // Present!
    VkPresentInfoKHR present_info = {
        VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        nullptr,
        1, &sem,
        1, &swapchain, &image_index,
        nullptr
    };

    vkQueuePresentKHR(dev->graphics_queue, &present_info);
}

uint64_t context::get_frame_counter() const
{
    return frame_counter;
}

void context::reset_swapchain()
{
    dev->finish();
    deinit_swapchain();
    reap.flush();
    init_swapchain();
}

ivec2 context::get_size() const
{
    return size;
}

void context::at_frame_finish(std::function<void()>&& cleanup)
{
    reap.at_finish(std::move(cleanup));
}

void context::init_sdl(bool fullscreen, bool grab_mouse)
{
    if(SDL_Init(SDL_INIT_EVERYTHING))
        throw std::runtime_error(SDL_GetError());

    win = SDL_CreateWindow(
        "MyGraphicsProject",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        size.x,
        size.y,
        SDL_WINDOW_VULKAN | (fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0)
    );
    if(!win)
        throw std::runtime_error(SDL_GetError());

    SDL_GetWindowSize(win, &size.x, &size.y);
    SDL_SetWindowGrab(win, (SDL_bool)grab_mouse);
    SDL_SetRelativeMouseMode((SDL_bool)grab_mouse);

    unsigned count = 0;
    if(!SDL_Vulkan_GetInstanceExtensions(win, &count, nullptr))
        throw std::runtime_error(SDL_GetError());

    extensions.resize(count);
    if(!SDL_Vulkan_GetInstanceExtensions(win, &count, extensions.data()))
        throw std::runtime_error(SDL_GetError());
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
}

void context::deinit_sdl()
{
    SDL_DestroyWindow(win);
    SDL_Quit();
}

void context::init_vulkan()
{
    if(volkInitialize() != VK_SUCCESS)
        throw std::runtime_error("volk");

    VkApplicationInfo app_info {
        VK_STRUCTURE_TYPE_APPLICATION_INFO,
        nullptr,
        "MyGraphicsProject",
        VK_MAKE_VERSION(0,1,0),
        "MyGraphicsProjectEngine",
        VK_MAKE_VERSION(0,1,0),
        VK_API_VERSION_1_2
    };

    uint32_t available_layer_count = 0;
    vkEnumerateInstanceLayerProperties(&available_layer_count, nullptr);
    std::vector<VkLayerProperties> available_layers(available_layer_count);
    vkEnumerateInstanceLayerProperties(&available_layer_count, available_layers.data());

    for(auto& layer: available_layers)
    {
        if(strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0)
        {
            validation_layers.push_back("VK_LAYER_KHRONOS_validation");
        }
    }

    VkInstanceCreateInfo instance_info {
        VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        nullptr,
        0,
        &app_info,
        (uint32_t)validation_layers.size(), validation_layers.data(),
        (uint32_t)extensions.size(), extensions.data()
    };

    VkResult res = vkCreateInstance(&instance_info, nullptr, &vulkan);
    if(res != VK_SUCCESS)
        throw std::runtime_error("vkCreateInstance " + std::to_string(res));

    volkLoadInstance(vulkan);

    VkDebugUtilsMessengerCreateInfoEXT messenger_info = {
        VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        nullptr,
        0,
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT|
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT|
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT|
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        //VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT|
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT|
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        [](
            VkDebugUtilsMessageSeverityFlagBitsEXT,
            VkDebugUtilsMessageTypeFlagsEXT,
            const VkDebugUtilsMessengerCallbackDataEXT* data,
            void*
        ) -> VkBool32 {
            std::cerr << data->pMessage << std::endl;
            return false;
        },
        nullptr
    };
    vkCreateDebugUtilsMessengerEXT(vulkan, &messenger_info, nullptr, &messenger);
}

void context::deinit_vulkan()
{
    vkDestroyDebugUtilsMessengerEXT(vulkan, messenger, nullptr);
    vkDestroyInstance(vulkan, nullptr);
}

void context::init_swapchain()
{
    // Find the format we want
    uint32_t format_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(dev->physical_device, surface, &format_count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(format_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(dev->physical_device, surface, &format_count, formats.data());

    bool found_format = false;
    for(VkSurfaceFormatKHR format: formats)
    {
        if(
            (format.format == VK_FORMAT_B8G8R8A8_UNORM ||
            format.format == VK_FORMAT_R8G8B8A8_UNORM) &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR
        ){
            surface_format = format;
            found_format = true;
            break;
        }
    }

    if(!found_format)
        surface_format = formats[0];

    // Find the presentation mode to use
    uint32_t mode_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(dev->physical_device, surface, &mode_count, nullptr);
    std::vector<VkPresentModeKHR> modes(mode_count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(dev->physical_device, surface, &mode_count, modes.data());

    bool found_mode = false;
    std::vector<VkPresentModeKHR> preferred_modes;
    if(vsync)
    {
        preferred_modes.push_back(VK_PRESENT_MODE_MAILBOX_KHR);
        preferred_modes.push_back(VK_PRESENT_MODE_FIFO_KHR);
    }
    preferred_modes.push_back(VK_PRESENT_MODE_IMMEDIATE_KHR);
    for(VkPresentModeKHR mode: preferred_modes)
    {
        if(std::count(modes.begin(), modes.end(), mode))
        {
            present_mode = mode;
            found_mode = true;
        }
    }

    if(!found_mode)
        present_mode = modes[0];

    // Get surface capabilities
    VkSurfaceCapabilitiesKHR surface_caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(dev->physical_device, surface, &surface_caps);

    // Extent check
    if(
        size.x < surface_caps.minImageExtent.width || size.x > surface_caps.maxImageExtent.width ||
        size.y < surface_caps.minImageExtent.height || size.y > surface_caps.maxImageExtent.height
    ){
        throw std::runtime_error("Cannot match desired resolution in swapchain");
    }

    // Go for three images just so that we have to do things the hard way
    uint32_t image_count = std::max(3u, surface_caps.minImageCount);
    if(surface_caps.maxImageCount != 0)
        image_count = std::min(image_count, surface_caps.maxImageCount);

    // Create the swapchain!
    VkSwapchainCreateInfoKHR swapchain_info = {
        VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        nullptr,
        {},
        surface,
        image_count,
        surface_format.format,
        surface_format.colorSpace,
        {(uint32_t)size.x, (uint32_t)size.y},
        1,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|
        VK_IMAGE_USAGE_STORAGE_BIT,
        VK_SHARING_MODE_EXCLUSIVE,
        1, (const uint32_t*)&dev->graphics_family_index,
        surface_caps.currentTransform,
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        present_mode,
        VK_TRUE,
        {}
    };
    vkCreateSwapchainKHR(dev->logical_device, &swapchain_info, nullptr, &swapchain);

    // Get swapchain images
    swapchain_images.resize(image_count);
    vkGetSwapchainImagesKHR(dev->logical_device, swapchain, &image_count, swapchain_images.data());

    for(VkImage img: swapchain_images)
    {
        swapchain_image_views.push_back(create_image_view(*this, img, surface_format.format, VK_IMAGE_ASPECT_COLOR_BIT));
    }

    for(size_t i = 0; i < image_count+1; ++i)
    {
        binary_start_semaphores.push_back(create_binary_semaphore(*this));
        binary_finish_semaphores.push_back(create_binary_semaphore(*this));
    }
    frame_start_semaphore = create_timeline_semaphore(*this);
    frame_counter = 0;
}

void context::deinit_swapchain()
{
    dev->finish();
    frame_start_semaphore.reset();
    binary_start_semaphores.clear();
    binary_finish_semaphores.clear();
    swapchain_image_views.clear();
    vkDestroySwapchainKHR(dev->logical_device, swapchain, nullptr);
}
