#include "emulator_render_stage.hh"
#include "emulator_transform.comp.h"

namespace
{

struct push_constants
{
    pivec2 input_size;
    uint32_t use_color_mapping;
};

}

emulator_render_stage::emulator_render_stage(
    context& ctx,
    emulator& emu,
    render_target& target,
    bool generate_mipmaps,
    bool color_mapping,
    bool faded
):  render_stage(ctx), emu(&emu), faded(faded),
    transform_pipeline(ctx),
    image_buffer(
        ctx,
        sizeof(uint32_t)*emu.get_screen_size().x*emu.get_screen_size().y,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        false
    ),
    stage_timer(ctx, "emulator_render_stage")
{
    transform_pipeline.init(
        sizeof(emulator_transform_comp_shader_binary),
        emulator_transform_comp_shader_binary,
        ctx.get_image_count(), 
        {
            {0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        },
        sizeof(push_constants)
    );

    push_constants pc = {emu.get_screen_size(), color_mapping ? 1u : 0u};

    for(size_t i = 0; i < ctx.get_image_count(); ++i)
    {
        // Assign descriptors
        transform_pipeline.set_descriptor(i, 0, {target[i].view});
        transform_pipeline.set_descriptor(i, 1, {image_buffer[i]});

        // Record command buffers
        VkCommandBuffer cmd = compute_commands();
        stage_timer.start(cmd, i);

        image_buffer.upload(cmd, i);
        transform_pipeline.bind(cmd, i);
        transform_pipeline.push_constants(cmd, &pc);

        target.transition_layout(cmd, i, VK_IMAGE_LAYOUT_GENERAL);

        ivec2 size = target.get_size();
        vkCmdDispatch(cmd, (size.x+7)/8, (size.y+7)/8, 1);

        target.transition_layout(cmd, i, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        stage_timer.stop(cmd, i);
        use_compute_commands(cmd, i);
    }
}

void emulator_render_stage::update_buffers(uint32_t image_index)
{
    emu->lock_framebuffer();
    image_buffer.update(image_index, emu->get_framebuffer_data(faded));
    emu->unlock_framebuffer();
}
