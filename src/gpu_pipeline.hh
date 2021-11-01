#ifndef RAYBOY_GPU_PIPELINE_HH
#define RAYBOY_GPU_PIPELINE_HH

#include "context.hh"
#include <vector>

class gpu_pipeline
{
public:
    gpu_pipeline(context& ctx);
    ~gpu_pipeline();

    // General
    void init_bindings(
        size_t count,
        const std::vector<VkDescriptorSetLayoutBinding>& bindings,
        size_t push_constant_size = 0
    );

    void set_descriptor(
        size_t set_index,
        size_t binding_index,
        std::vector<VkImageView> views,
        std::vector<VkSampler> samplers = {}
    );

    void set_descriptor(
        size_t set_index,
        size_t binding_index,
        std::vector<VkBuffer> buffer
    );

    void push_constants(VkCommandBuffer buf, const void* data);

    context* ctx;

protected:
    VkDescriptorSetLayoutBinding find_binding(size_t binding_index) const;

    vkres<VkPipeline> pipeline;
    std::vector<VkDescriptorSet> descriptor_sets;
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    size_t push_constant_size;
    vkres<VkDescriptorSetLayout> descriptor_set_layout;
    vkres<VkDescriptorPool> descriptor_pool;
    vkres<VkPipelineLayout> pipeline_layout;
};

#endif
