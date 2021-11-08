#include "tonemap_render_stage.hh"
#include "tonemap.comp.h"
#include "tonemap_msaa.comp.h"
#include "render_target.hh"

namespace
{

struct push_constants
{
    uint32_t algorithm;
    uint32_t samples;
};

struct uniform_buffer
{
    float exposure;
    float gamma;
};

}

tonemap_render_stage::tonemap_render_stage(
    context& ctx,
    render_target& src,
    render_target& dst
):  render_stage(ctx), tonemap_pipeline(ctx),
    uniforms(ctx, sizeof(uniform_buffer)),
    stage_timer(ctx, "tonemap_render_stage")
{
    size_t shader_size = 0;
    const uint32_t* shader_src = nullptr;

    if(src.get_samples() != VK_SAMPLE_COUNT_1_BIT)
    {
        shader_size = sizeof(tonemap_msaa_comp_shader_binary);
        shader_src = tonemap_msaa_comp_shader_binary;
    }
    else
    {
        shader_size = sizeof(tonemap_comp_shader_binary);
        shader_src = tonemap_comp_shader_binary;
    }

    // Create pipeline
    tonemap_pipeline.init(
        shader_size, shader_src,
        ctx.get_image_count(), 
        {
            {0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        },
        sizeof(push_constants)
    );

    push_constants pc = {0, src.get_samples()};

    // Assign parameters to the shader
    for(size_t i = 0; i < ctx.get_image_count(); ++i)
    {
        // Assign descriptors
        tonemap_pipeline.set_descriptor(i, 0, {src[i].view});
        tonemap_pipeline.set_descriptor(i, 1, {dst[i].view});
        tonemap_pipeline.set_descriptor(i, 2, {uniforms[i]});

        // Record command buffer
        VkCommandBuffer buf = compute_commands();
        stage_timer.start(buf, i);

        uniforms.upload(buf, i);
        tonemap_pipeline.bind(buf, i);
        tonemap_pipeline.push_constants(buf, &pc);

        src.transition_layout(buf, i, VK_IMAGE_LAYOUT_GENERAL);
        dst.transition_layout(buf, i, VK_IMAGE_LAYOUT_GENERAL);

        ivec2 size = ctx.get_size();
        vkCmdDispatch(buf, (size.x+7)/8, (size.y+7)/8, 1);

        dst.transition_layout(buf, i, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR);
        stage_timer.stop(buf, i);
        use_compute_commands(buf, i);
    }
}

void tonemap_render_stage::update_buffers(uint32_t image_index)
{
    uniforms.update(image_index, uniform_buffer{1.0f, 1.0f/2.2f});
}
