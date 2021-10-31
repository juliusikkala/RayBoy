#include "compute_pipeline.hh"
#include "helpers.hh"

void compute_pipeline::init(
    size_t shader_bytes,
    const uint32_t* shader_data,
    size_t descriptor_set_count,
    const std::vector<VkDescriptorSetLayoutBinding>& bindings,
    size_t push_constant_size
){
    init_bindings(descriptor_set_count, bindings, push_constant_size);

    vkres<VkShaderModule> shader = load_shader(*ctx, shader_bytes, shader_data);
    VkPipelineShaderStageCreateInfo shader_info = {
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        nullptr,
        {},
        VK_SHADER_STAGE_COMPUTE_BIT,
        shader,
        "main",
        nullptr
    };
    VkComputePipelineCreateInfo pipeline_info = {
        VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        nullptr,
        {},
        shader_info,
        pipeline_layout,
        VK_NULL_HANDLE,
        0
    };
    VkPipeline pipeline;
    vkCreateComputePipelines(
        ctx->get_device().logical_device,
        VK_NULL_HANDLE,
        1,
        &pipeline_info,
        nullptr,
        &pipeline
    );
    this->pipeline = vkres(*ctx, pipeline);
}
