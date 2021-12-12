#include "context.hh"
#include "helpers.hh"
#include <stdexcept>
#include <iostream>
#include <algorithm>
#include <numeric>

constexpr uint32_t MAX_TIMER_COUNT = 32;

context::context(
    ivec2 size,
    bool fullscreen,
    bool vsync,
    bool hdr,
    bool grab_mouse,
    int display
): size(size), fullscreen(fullscreen), vsync(vsync), hdr(hdr)
{
    init_sdl(fullscreen, grab_mouse, display);
    init_vulkan();
    if(!SDL_Vulkan_CreateSurface(win, vulkan, &surface))
        throw std::runtime_error(SDL_GetError());
    dev.reset(new device(vulkan, surface, validation_layers));
    init_swapchain();
    init_timing();
}

context::~context()
{
    dev->finish();
    deinit_timing();
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

SDL_Window* context::get_window() const
{
    return win;
}

VkInstance context::get_instance() const
{
    return vulkan;
}

bool context::start_frame()
{
    frame_counter++;
    reap.start_frame();

    // This is the binary semaphore we will be using
    VkSemaphore sem = binary_start_semaphores[frame_counter%binary_start_semaphores.size()];
    uint32_t image_history_index = frame_counter%image_index_history.size();

    // Wait until that semaphore cannot be in use anymore
    if(frame_counter >= binary_start_semaphores.size())
    {
        wait_timeline_semaphore(
            *this, frame_finish_semaphore,
            frame_counter - (binary_start_semaphores.size() - 1)
        );
        reap.finish_frame();
        update_timing_results(image_index_history[image_history_index]);
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
    image_index_history[image_history_index] = image_index;
    auto cpu_next_start_time = std::chrono::steady_clock::now();
    cpu_frame_duration = cpu_next_start_time - cpu_frame_start_time;
    cpu_frame_start_time = cpu_next_start_time;

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
    return render_target(frames, uvec2(size), VK_SAMPLE_COUNT_1_BIT, surface_format.format);
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
    VkSemaphoreSubmitInfoKHR signal_infos[2] = {
        {
            VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO_KHR, nullptr,
            sem, 0, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT_KHR, 0
        },
        {
            VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO_KHR, nullptr,
            frame_finish_semaphore, frame_counter, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT_KHR, 0
        }
    };
    VkSubmitInfo2KHR submit_info = {
        VK_STRUCTURE_TYPE_SUBMIT_INFO_2_KHR,
        nullptr, 0,
        1, &wait_info,
        0, nullptr,
        2, signal_infos
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

void context::set_size(ivec2 size)
{
    SDL_SetWindowSize(win, size.x, size.y);
}

ivec2 context::get_size() const
{
    return size;
}

void context::at_frame_finish(std::function<void()>&& cleanup)
{
    reap.at_finish(std::move(cleanup));
}

void context::sync_flush()
{
    dev->finish();
    reap.flush();
}

VkQueryPool context::get_timestamp_query_pool(uint32_t image_index)
{
    return timestamp_query_pools[image_index];
}

int32_t context::add_timer(const std::string& name)
{
    if(free_queries.size() == 0)
    {
        std::cerr << "Failed to get a timer query for " << name << std::endl;
        return -1;
    }
    int32_t index = free_queries.back();
    free_queries.pop_back();
    timers.emplace(index, name);
    return index;
}

void context::remove_timer(uint32_t image_index)
{
    free_queries.push_back(image_index);
    timers.erase(image_index);
}

void context::dump_timing() const
{
    std::cout << "Timing:" << std::endl;
    for(const auto& pair: timing_results)
    {
        std::cout
            << "\t[" << pair.first << "]: "
            << pair.second*1e3 << "ms" << std::endl;
    }
}

int context::get_available_displays() const
{
    return SDL_GetNumVideoDisplays();
}

void context::set_current_display(int display)
{
    int cur_display = get_current_display();
    if(cur_display == display || display == -1 || cur_display == -1)
        return;

    // Yes, I know this is really crappy... But it's the only way I got it to
    // work right... You are welcome to improve it. The delays were needed to
    // avoid tripping X11.
    if(fullscreen) SDL_SetWindowFullscreen(win, 0);
    SDL_Delay(100);
    SDL_SetWindowPosition(
        win,
        SDL_WINDOWPOS_CENTERED_DISPLAY(display),
        SDL_WINDOWPOS_CENTERED_DISPLAY(display)
    );
    SDL_Delay(100);
    if(fullscreen)
    {
        SDL_SetWindowFullscreen(win, SDL_WINDOW_FULLSCREEN_DESKTOP);
        SDL_GetWindowSize(win, &size.x, &size.y);
    }
}

int context::get_current_display() const
{
    uint32_t flags = SDL_GetWindowFlags(win);
    if(!(flags & SDL_WINDOW_FULLSCREEN_DESKTOP)) return -1;
    return SDL_GetWindowDisplayIndex(win);
}

void context::set_fullscreen(bool fullscreen)
{
    if(this->fullscreen == fullscreen) return;

    SDL_SetWindowFullscreen(win, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);

    SDL_GetWindowSize(win, &size.x, &size.y);

    this->fullscreen = fullscreen;
}

bool context::is_fullscreen() const
{
    return fullscreen;
}

void context::set_vsync(bool vsync)
{
    this->vsync = vsync;
}

bool context::get_vsync() const
{
    return vsync;
}

void context::set_hdr(bool hdr)
{
    this->hdr = hdr;
}

bool context::get_hdr() const
{
    return hdr;
}

bool context::is_hdr_available() const
{
    return hdr_available;
}

bool context::is_hdr_used() const
{
    return surface_format.colorSpace == VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT;
}

void context::init_sdl(bool fullscreen, bool grab_mouse, int display)
{
    if(SDL_Init(SDL_INIT_EVERYTHING))
        throw std::runtime_error(SDL_GetError());

    win = SDL_CreateWindow(
        "RayBoy",
        display >= 0 ? SDL_WINDOWPOS_CENTERED_DISPLAY(display) : SDL_WINDOWPOS_UNDEFINED,
        display >= 0 ? SDL_WINDOWPOS_CENTERED_DISPLAY(display) : SDL_WINDOWPOS_UNDEFINED,
        size.x,
        size.y,
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | (fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0)
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
    extensions.push_back(VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME);
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

#ifndef NDEBUG
    for(auto& layer: available_layers)
    {
        if(strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0)
        {
            validation_layers.push_back("VK_LAYER_KHRONOS_validation");
        }
    }
#endif

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

#ifndef NDEBUG
    VkDebugUtilsMessengerCreateInfoEXT messenger_info = {
        VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        nullptr,
        0,
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT|
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT|
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT|
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT|
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
#endif
}

void context::deinit_vulkan()
{
#ifndef NDEBUG
    vkDestroyDebugUtilsMessengerEXT(vulkan, messenger, nullptr);
#endif
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
        if(format.colorSpace == VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT)
        {
            hdr_available = true;
        }

        if(hdr && format.colorSpace == VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT)
        {
            surface_format = format;
            found_format = true;
            break;
        }
        else if(
            (format.format == VK_FORMAT_B8G8R8A8_UNORM ||
            format.format == VK_FORMAT_R8G8B8A8_UNORM) &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR
        ){
            surface_format = format;
            found_format = true;
            if(!hdr) break;
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
            break;
        }
    }

    if(!found_mode)
        present_mode = modes[0];

    // Get surface capabilities
    VkSurfaceCapabilitiesKHR surface_caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(dev->physical_device, surface, &surface_caps);

    // Extent check
    size.x = clamp((uint32_t)size.x, surface_caps.minImageExtent.width, surface_caps.maxImageExtent.width);
    size.y = clamp((uint32_t)size.y, surface_caps.minImageExtent.height, surface_caps.maxImageExtent.height);

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
        VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,
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
    vkGetSwapchainImagesKHR(dev->logical_device, swapchain, &image_count, nullptr);
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
    frame_finish_semaphore = create_timeline_semaphore(*this);
    frame_counter = 0;
    image_index_history.resize(image_count, -1);
}

void context::deinit_swapchain()
{
    dev->finish();
    image_index_history.clear();
    frame_start_semaphore.reset();
    frame_finish_semaphore.reset();
    binary_start_semaphores.clear();
    binary_finish_semaphores.clear();
    swapchain_image_views.clear();
    vkDestroySwapchainKHR(dev->logical_device, swapchain, nullptr);
}

void context::init_timing()
{
    uint32_t image_count = get_image_count();
    timestamp_query_pools.resize(image_count);
    for(uint32_t i = 0; i < image_count; ++i)
    {
        VkQueryPoolCreateInfo info = {
            VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
            nullptr,
            {},
            VK_QUERY_TYPE_TIMESTAMP,
            MAX_TIMER_COUNT*2,
            0
        };
        vkCreateQueryPool(
            dev->logical_device,
            &info,
            nullptr,
            &timestamp_query_pools[i]
        );
    }
    free_queries.resize(MAX_TIMER_COUNT);
    std::iota(free_queries.begin(), free_queries.end(), 0u);
}

void context::deinit_timing()
{
    for(VkQueryPool pool: timestamp_query_pools)
        vkDestroyQueryPool(dev->logical_device, pool, nullptr);
    timestamp_query_pools.clear();
    free_queries.clear();
    timers.clear();
}

void context::update_timing_results(uint32_t image_index)
{
    std::vector<uint64_t> results(MAX_TIMER_COUNT*2);
    vkGetQueryPoolResults(
        dev->logical_device,
        timestamp_query_pools[image_index],
        0, (uint32_t)results.size(),
        results.size()*sizeof(uint64_t), results.data(),
        0,
        VK_QUERY_RESULT_64_BIT
    );
    timing_results.clear();

    struct timestamp
    {
        uint64_t start, end;
        std::string name;
    };
    std::vector<timestamp> tmp;
    uint64_t min_start = UINT64_MAX, max_end = 0;
    for(auto& pair: timers)
    {
        tmp.push_back({
            results[pair.first*2],
            results[pair.first*2+1],
            pair.second
        });
        min_start = std::min(min_start, results[pair.first*2]);
        max_end = std::max(max_end, results[pair.first*2+1]);
    }
    std::sort(
        tmp.begin(), tmp.end(),
        [](const timestamp& a, const timestamp& b){
            return a.start < b.start;
        }
    );
    tmp.push_back({
        min_start,
        max_end,
        "GPU total"
    });
    tmp.push_back({
        0,
        (uint64_t)std::chrono::nanoseconds(cpu_frame_duration).count(),
        "CPU total"
    });

    for(timestamp t: tmp)
    {
        timing_results.push_back({
            t.name,
            double(t.end-t.start)*1e-9
        });
    }
}
