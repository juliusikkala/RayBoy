#ifndef RAYBOY_COMPUTE_PIPELINE_HH
#define RAYBOY_COMPUTE_PIPELINE_HH

#include "gpu_pipeline.hh"

class compute_pipeline: public gpu_pipeline
{
public:
    using gpu_pipeline::gpu_pipeline;

    void init(
        size_t shader_bytes,
        const uint32_t* shader_data,
        size_t descriptor_set_count,
        const std::vector<VkDescriptorSetLayoutBinding>& bindings,
        size_t push_constant_size = 0
    );

    void bind(VkCommandBuffer buf, size_t set_index);
};

#endif
