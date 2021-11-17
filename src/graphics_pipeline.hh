#ifndef RAYBOY_GRAPHICS_PIPELINE_HH
#define RAYBOY_GRAPHICS_PIPELINE_HH

#include "gpu_pipeline.hh"
#include "render_target.hh"

struct shader_data
{
    size_t vertex_bytes = 0;
    const uint32_t* vertex_data = nullptr;
    VkSpecializationInfo vertex_specialization = {0, nullptr, 0, nullptr};

    size_t fragment_bytes = 0;
    const uint32_t* fragment_data = nullptr;
    VkSpecializationInfo fragment_specialization = {0, nullptr, 0, nullptr};
};

class graphics_pipeline: public gpu_pipeline
{
public:
    using gpu_pipeline::gpu_pipeline;

    struct params
    {
        params() = default;
        params(const std::vector<render_target*>& targets);

        std::vector<render_target*> targets;

        // These are all auto-filled to reasonable defaults; you can modify
        // them afterwards to suit your specific needs.
        VkPipelineVertexInputStateCreateInfo vertex_input_info;
        VkPipelineInputAssemblyStateCreateInfo input_assembly_info;
        VkViewport viewport;
        VkRect2D scissor;
        VkPipelineRasterizationStateCreateInfo rasterization_info;
        VkPipelineMultisampleStateCreateInfo multisample_info;
        VkPipelineDepthStencilStateCreateInfo depth_stencil_info;

        std::vector<VkPipelineColorBlendAttachmentState> blend_states;
        std::vector<VkAttachmentDescription> attachments;
        std::vector<VkClearValue> clear_values;
    };

    void init(
        const params& p,
        const shader_data& sd,
        size_t descriptor_set_count,
        const std::vector<VkDescriptorSetLayoutBinding>& bindings,
        size_t push_constant_size = 0
    );

    void begin_render_pass(VkCommandBuffer buf, uint32_t image_index);
    void end_render_pass(VkCommandBuffer buf);

    void bind(VkCommandBuffer buf, size_t set_index);

private:
    params create_params;
    vkres<VkRenderPass> render_pass;
    std::vector<vkres<VkFramebuffer>> framebuffers;
};

#endif
