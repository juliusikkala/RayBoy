#include "emulator_render_stage.hh"
#include "emulator_transform.comp.h"
#include "io.hh"
#include "helpers.hh"

namespace
{

struct push_constants
{
    pivec2 input_size;
    uint32_t use_color_mapping;
    uint32_t apply_gamma;
    int32_t mip_layer;
};

}

emulator_render_stage::emulator_render_stage(
    context& ctx,
    emulator& emu,
    render_target& target,
    bool do_generate_mipmaps,
    bool color_mapping,
    bool apply_gamma
):  render_stage(ctx), emu(&emu),
    transform_pipeline(ctx),
    image_buffer(
        ctx,
        sizeof(uint32_t)*emu.get_screen_size().x*emu.get_screen_size().y,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        false
    ),
    color_lut(ctx, get_readonly_path("data/gbc_lut.png"), VK_IMAGE_LAYOUT_GENERAL),
    subpixel(ctx, get_readonly_path("data/subpixel.png")),
    subpixel_sampler(ctx),
    stage_timer(ctx, "emulator_render_stage")
{
    transform_pipeline.init(
        sizeof(emulator_transform_comp_shader_binary),
        emulator_transform_comp_shader_binary,
        ctx.get_image_count(), 
        {
            {0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        },
        sizeof(push_constants)
    );

    push_constants pc = {
        emu.get_screen_size(),
        color_mapping ? 1u : 0u,
        apply_gamma ? 1u : 0u,
        0
    };
    ivec2 pixel_size = target.get_size()/emu.get_screen_size();

    // Thinking is too hard
    for(unsigned h = subpixel.get_size().y; h >= pixel_size.y; ++pc.mip_layer, h/=2);
    if(pc.mip_layer > 0) pc.mip_layer--;

    for(size_t i = 0; i < ctx.get_image_count(); ++i)
    {
        // Assign descriptors
        transform_pipeline.set_descriptor(i, 0, {target[i].view});
        transform_pipeline.set_descriptor(i, 1, {image_buffer[i]});
        transform_pipeline.set_descriptor(i, 2, {color_lut.get_image_view(i)});
        transform_pipeline.set_descriptor(i, 3, {subpixel.get_image_view(i)}, {subpixel_sampler.get()});

        // Record command buffers
        VkCommandBuffer cmd = compute_commands();
        stage_timer.start(cmd, i);

        image_buffer.upload(cmd, i);
        transform_pipeline.bind(cmd, i);
        transform_pipeline.push_constants(cmd, &pc);

        target.transition_layout(cmd, i, VK_IMAGE_LAYOUT_GENERAL);

        ivec2 size = target.get_size();
        vkCmdDispatch(cmd, (size.x+7)/8, (size.y+7)/8, 1);

        if(!do_generate_mipmaps)
        {
            target.transition_layout(cmd, i, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            stage_timer.stop(cmd, i);
        }

        use_compute_commands(cmd, i);

        if(do_generate_mipmaps)
        {
            VkCommandBuffer cmd = graphics_commands();
            generate_mipmaps(
                cmd,
                target[i].image,
                target.get_format(),
                target.get_size(),
                VK_IMAGE_LAYOUT_GENERAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            );
            stage_timer.stop(cmd, i);
            use_graphics_commands(cmd, i);
        }
    }
}

void emulator_render_stage::update_buffers(uint32_t image_index)
{
    emu->lock_framebuffer();
    image_buffer.update(image_index, emu->get_framebuffer_data());
    emu->unlock_framebuffer();
}
