#ifndef RAYBOY_HELPERS_HH
#define RAYBOY_HELPERS_HH

#include "context.hh"
#include "vkres.hh"

vkres<VkImageView> create_image_view(
    context& ctx,
    VkImage image,
    VkFormat format,
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT
);

vkres<VkDescriptorSetLayout> create_descriptor_set_layout(
    context& ctx,
    const std::vector<VkDescriptorSetLayoutBinding>& bindings
);

vkres<VkSemaphore> create_binary_semaphore(context& ctx);
vkres<VkSemaphore> create_timeline_semaphore(context& ctx, uint64_t start_value = 0);
void wait_timeline_semaphore(context& ctx, VkSemaphore sem, uint64_t wait_value);
vkres<VkShaderModule> load_shader(context& ctx, size_t bytes, const uint32_t* data);
vkres<VkBuffer> create_gpu_buffer(context& ctx, size_t bytes, VkBufferUsageFlags usage);
vkres<VkBuffer> create_cpu_buffer(context& ctx, size_t bytes, void* initial_data = nullptr);
vkres<VkImage> create_gpu_image(
    context& ctx,
    uvec2 size,
    VkFormat format,
    VkImageLayout layout,
    VkSampleCountFlagBits samples,
    VkImageTiling tiling,
    VkImageUsageFlags usage,
    size_t bytes = 0,
    void* data = nullptr,
    bool mipmapped = false
);

void generate_mipmaps(
    VkCommandBuffer cmd,
    VkImage img,
    uvec2 size,
    VkImageLayout before,
    VkImageLayout after
);

void copy_buffer(
    context& ctx,
    VkBuffer dst_buffer,
    VkBuffer src_buffer,
    size_t bytes
);
vkres<VkBuffer> upload_buffer(
    context& ctx,
    size_t bytes,
    void* data,
    VkBufferUsageFlags usage
);

VkCommandBuffer begin_command_buffer(context& ctx);
void end_command_buffer(context& ctx, VkCommandBuffer buf);

std::vector<VkDescriptorPoolSize> calculate_descriptor_pool_sizes(
    size_t binding_count,
    const VkDescriptorSetLayoutBinding* bindings,
    uint32_t multiplier = 1 
);

void image_barrier(
    VkCommandBuffer cmd,
    VkImage image,
    VkImageLayout layout_before,
    VkImageLayout layout_after,
    uint32_t mip_level = 0,
    uint32_t mip_count = VK_REMAINING_MIP_LEVELS,
    VkAccessFlags2KHR happens_before = VK_ACCESS_2_MEMORY_WRITE_BIT_KHR | VK_ACCESS_2_MEMORY_READ_BIT_KHR,
    VkAccessFlags2KHR happens_after = VK_ACCESS_2_MEMORY_WRITE_BIT_KHR | VK_ACCESS_2_MEMORY_READ_BIT_KHR,
    VkPipelineStageFlags2KHR stage_before = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT_KHR,
    VkPipelineStageFlags2KHR stage_after = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT_KHR
);

void interlace(void* dst, const void* src, const void* fill, size_t src_stride, size_t dst_stride, size_t entries);

#endif
