#ifndef RAYBOY_CONTEXT_HH
#define RAYBOY_CONTEXT_HH

#include "device.hh"
#include "glm/glm.hpp"
#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <vector>
#include <memory>
#include "reaper.hh"
#include "render_target.hh"
#include "vkres.hh"

using namespace glm;

class context
{
public:
    context(
        ivec2 size = ivec2(1280, 720),
        bool fullscreen = false,
        bool vsync = true,
        bool grab_mouse = false
    );
    ~context();

    const device& get_device() const;

    // Returns true when resources must be reset
    bool start_frame();
    uint32_t get_image_index() const;
    uint32_t get_image_count() const;
    render_target get_render_target() const;
    const std::vector<VkImage>& get_images() const;

    VkSemaphore get_start_semaphore();
    void finish_frame(VkSemaphore wait);

    uint64_t get_frame_counter() const;

    void reset_swapchain();
    ivec2 get_size() const;

    void at_frame_finish(std::function<void()>&& cleanup);

private:
    void init_sdl(bool fullscreen, bool grab_mouse);
    void deinit_sdl();

    void init_vulkan();
    void deinit_vulkan();

    void init_swapchain();
    void deinit_swapchain();

    // SDL-related members
    ivec2 size;
    bool vsync;
    SDL_Window* win;

    // Vulkan-related members
    VkInstance vulkan;
    VkSurfaceKHR surface;
    VkDebugUtilsMessengerEXT messenger;
    std::vector<const char*> extensions;
    std::vector<const char*> validation_layers;
    VkSurfaceFormatKHR surface_format;
    VkPresentModeKHR present_mode;
    std::unique_ptr<device> dev;

    // Swapchain resources
    VkSwapchainKHR swapchain;
    std::vector<VkImage> swapchain_images;
    std::vector<vkres<VkImageView>> swapchain_image_views;
    std::vector<vkres<VkSemaphore>> binary_start_semaphores;
    std::vector<vkres<VkSemaphore>> binary_finish_semaphores;
    vkres<VkSemaphore> frame_start_semaphore;
    uint64_t frame_counter;
    uint32_t image_index;

    // Memory handling
    reaper reap;
};

#endif
