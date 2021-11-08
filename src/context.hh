#ifndef RAYBOY_CONTEXT_HH
#define RAYBOY_CONTEXT_HH

#include "device.hh"
#include "math.hh"
#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <vector>
#include <string>
#include <unordered_map>
#include <memory>
#include <chrono>
#include "reaper.hh"
#include "render_target.hh"
#include "vkres.hh"

class context
{
public:
    context(
        ivec2 size = ivec2(1280, 720),
        bool fullscreen = false,
        bool vsync = true,
        bool grab_mouse = false,
        int display = -1
    );
    ~context();

    const device& get_device() const;
    SDL_Window* get_window() const;
    VkInstance get_instance() const;

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
    void set_size(ivec2 size);
    ivec2 get_size() const;

    void at_frame_finish(std::function<void()>&& cleanup);
    void sync_flush();

    VkQueryPool get_timestamp_query_pool(uint32_t image_index);
    int32_t add_timer(const std::string& name);
    void remove_timer(uint32_t image_index);
    void dump_timing() const;

    int get_available_displays() const;

    void set_current_display(int display = -1);
    int get_current_display() const;

    void set_fullscreen(bool fullscreen);
    bool is_fullscreen() const;

    void set_vsync(bool vsync);
    bool get_vsync() const;

private:
    void init_sdl(bool fullscreen, bool grab_mouse, int display);
    void deinit_sdl();

    void init_vulkan();
    void deinit_vulkan();

    void init_swapchain();
    void deinit_swapchain();

    void init_timing();
    void deinit_timing();
    void update_timing_results(uint32_t image_index);

    // SDL-related members
    ivec2 size;
    bool fullscreen;
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
    vkres<VkSemaphore> frame_finish_semaphore;
    uint64_t frame_counter;
    uint32_t image_index;
    std::vector<int32_t> image_index_history;

    // Timing resources
    std::vector<VkQueryPool> timestamp_query_pools;
    std::vector<int32_t> free_queries;
    std::unordered_map<int32_t, std::string> timers;
    std::chrono::steady_clock::duration cpu_frame_duration;
    std::chrono::steady_clock::time_point cpu_frame_start_time;
    std::vector<std::pair<std::string, double>> timing_results;

    // Memory handling
    reaper reap;
};

#endif
