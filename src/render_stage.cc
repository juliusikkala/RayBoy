#include "render_stage.hh"
#include "helpers.hh"
#include <cassert>

render_stage::render_stage(context& ctx)
: ctx(&ctx)
{
    command_buffers.resize(ctx.get_image_count());
}

VkSemaphore render_stage::run(uint32_t image_index, VkSemaphore wait)
{
    uint64_t frame_counter = ctx->get_frame_counter();
    VkSemaphore prev = wait;

    update_buffers(image_index);

    for(size_t i = 0; i < command_buffers[image_index].size(); ++i)
    {
        const vkres<VkCommandBuffer>& cmd = command_buffers[image_index][i];
        const vkres<VkSemaphore>& cur = finished[i];

        bool compute = cmd.get_pool() == ctx->get_device().compute_pool;
        VkSemaphoreSubmitInfoKHR wait_infos[2] = {
            {
                VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO_KHR, nullptr,
                prev, frame_counter,
                compute ? VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR : VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT_KHR,
                0
            },
            {
                VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO_KHR, nullptr,
                *finished.back(), frame_counter-1,
                VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT_KHR,
                0
            }
        };
        VkCommandBufferSubmitInfoKHR command_info = {
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO_KHR, nullptr, *cmd, 0
        };
        VkSemaphoreSubmitInfoKHR signal_info = {
            VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO_KHR, nullptr,
            *cur, frame_counter,
            compute ? VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR : VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR,
            0
        };
        VkSubmitInfo2KHR submit_info = {
            VK_STRUCTURE_TYPE_SUBMIT_INFO_2_KHR,
            nullptr, 0,
            frame_counter != 0 && i == 0 ? 2u : 1u, wait_infos,
            1, &command_info,
            1, &signal_info
        };
        vkQueueSubmit2KHR(compute ? ctx->get_device().compute_queue : ctx->get_device().graphics_queue, 1, &submit_info, VK_NULL_HANDLE);
        prev = *cur;
    }

    return prev;
}

void render_stage::update_buffers(uint32_t)
{
}

VkCommandBuffer render_stage::compute_commands()
{
    return commands(ctx->get_device().compute_pool);
}

void render_stage::use_compute_commands(VkCommandBuffer buf, uint32_t image_index)
{
    use_commands(buf, ctx->get_device().compute_pool, image_index);
}

VkCommandBuffer render_stage::graphics_commands()
{
    return commands(ctx->get_device().graphics_pool);
}

void render_stage::use_graphics_commands(VkCommandBuffer buf, uint32_t image_index)
{
    use_commands(buf, ctx->get_device().graphics_pool, image_index);
}

VkCommandBuffer render_stage::commands(VkCommandPool pool)
{
    VkCommandBufferAllocateInfo command_buffer_alloc_info = {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        nullptr,
        pool,
        VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        1
    };
    VkCommandBuffer buf;
    vkAllocateCommandBuffers(
        ctx->get_device().logical_device,
        &command_buffer_alloc_info,
        &buf
    );
    VkCommandBufferBeginInfo begin_info = {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        nullptr,
        VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT,
        nullptr
    };
    vkBeginCommandBuffer(buf, &begin_info);

    return buf;
}

void render_stage::use_commands(VkCommandBuffer buf, VkCommandPool pool, uint32_t image_index)
{
    vkEndCommandBuffer(buf);
    command_buffers[image_index].emplace_back(*ctx, pool, buf);
    ensure_semaphores(command_buffers[image_index].size());
}

void render_stage::clear_commands()
{
    for(auto& cmds: command_buffers)
        cmds.clear();
}

void render_stage::ensure_semaphores(size_t count)
{
    while(finished.size() < count)
        finished.emplace_back(std::move(create_timeline_semaphore(*ctx)));
}
