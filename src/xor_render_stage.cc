#include "xor_render_stage.hh"
#include "xor.comp.h"
#include "render_target.hh"

namespace
{

struct push_constants
{
    uint32_t scale;
};

struct uniform_buffer
{
    uint32_t frame_count;
};

}

xor_render_stage::xor_render_stage(context& ctx, render_target& target)
:   render_stage(ctx), xor_pipeline(ctx), uniforms(ctx, sizeof(uniform_buffer)),
    stage_timer(ctx, "xor_render_stage")
{
    // Create pipeline
    xor_pipeline.init(
        sizeof(xor_shader_binary), xor_shader_binary,
        ctx.get_image_count(), 
        {
            {0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        },
        sizeof(push_constants)
    );

    push_constants pc = {512};

    // Assign parameters to the shader
    for(size_t i = 0; i < ctx.get_image_count(); ++i)
    {
        // Assign descriptors
        xor_pipeline.set_descriptor(i, 0, {target[i].view});
        xor_pipeline.set_descriptor(i, 1, uniforms[i]);

        // Record command buffer
        VkCommandBuffer buf = compute_commands();

        uniforms.upload(buf, i);
        xor_pipeline.bind(buf, i);
        xor_pipeline.push_constants(buf, &pc);

        stage_timer.start(buf, i);
        target.transition_layout(buf, i, VK_IMAGE_LAYOUT_GENERAL);

        ivec2 size = ctx.get_size();
        vkCmdDispatch(buf, (size.x+7)/8, (size.y+7)/8, 1);

        target.transition_layout(buf, i, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        stage_timer.stop(buf, i);
        use_compute_commands(buf, i);
    }
}

void xor_render_stage::update_buffers(uint32_t image_index)
{
    uniforms.update(image_index, uniform_buffer{(uint32_t)ctx->get_frame_counter()});
}
