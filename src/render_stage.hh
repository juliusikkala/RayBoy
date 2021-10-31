#ifndef RAYBOY_RENDER_STAGE_HH
#define RAYBOY_RENDER_STAGE_HH

#include "context.hh"
#include <vector>

class render_stage
{
public:
    render_stage(context& ctx);
    virtual ~render_stage() = default;

    VkSemaphore run(uint32_t image_index, VkSemaphore wait);

protected:
    virtual void update_buffers(uint32_t image_index);

    // Compute pipelines only:
    VkCommandBuffer compute_commands();
    void use_compute_commands(VkCommandBuffer buf, uint32_t image_index);

    // Graphics pipelines only:
    VkCommandBuffer graphics_commands();
    void use_graphics_commands(VkCommandBuffer buf, uint32_t image_index);

    // General:
    VkCommandBuffer commands(VkCommandPool pool);
    void use_commands(VkCommandBuffer buf, VkCommandPool pool, uint32_t image_index);

    context* ctx;

private:
    VkDescriptorSetLayoutBinding find_binding(size_t binding_index) const;
    void ensure_semaphores(size_t count);

    std::vector<std::vector<vkres<VkCommandBuffer>>> command_buffers;
    std::vector<vkres<VkSemaphore>> finished;
};

#endif

