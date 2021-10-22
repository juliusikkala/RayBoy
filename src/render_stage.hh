#ifndef RAYBOY_RENDER_STAGE_HH
#define RAYBOY_RENDER_STAGE_HH

#include "context.hh"
#include <vector>

class render_stage
{
public:
    render_stage(context& ctx);
    ~render_stage();

    VkSemaphore run(uint32_t image_index, VkSemaphore wait) const;

protected:
    // General
    void init_bindings(
        size_t count,
        const std::vector<VkDescriptorSetLayoutBinding>& bindings
    );

    void set_descriptor(
        size_t set_index,
        size_t binding_index,
        std::vector<VkImageView> views,
        std::vector<VkSampler> samplers = {}
    );


    // Compute pipelines only:
    void init_compute_pipeline(size_t bytes, const uint32_t* data);
    VkCommandBuffer begin_compute_commands(size_t set_index);
    void finish_compute_commands(VkCommandBuffer buf);

    context* ctx;

private:
    VkDescriptorSetLayoutBinding find_binding(size_t binding_index) const;

    std::vector<vkres<VkCommandBuffer>> command_buffers;
    vkres<VkPipeline> pipeline;
    std::vector<VkDescriptorSet> descriptor_sets;
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    vkres<VkDescriptorSetLayout> descriptor_set_layout;
    vkres<VkDescriptorPool> descriptor_pool;
    vkres<VkPipelineLayout> pipeline_layout;
    vkres<VkSemaphore> finished;
};

#endif

