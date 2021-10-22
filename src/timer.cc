#include "timer.hh"

timer::timer(context& ctx, const std::string& name)
: ctx(&ctx), name(name)
{
    id = ctx.add_timer(name);
}

timer::timer(timer&& other)
: ctx(other.ctx), id(other.id)
{
    other.ctx = nullptr;
    other.id = -1;
}

timer::~timer()
{
    if(ctx)
        ctx->remove_timer(id);
}

const std::string& timer::get_name() const
{
    return name;
}

void timer::start(VkCommandBuffer buf, uint32_t image_index, VkPipelineStageFlags2KHR stage)
{
    if(id >= 0)
    {
        VkQueryPool pool = ctx->get_timestamp_query_pool(image_index);
        vkCmdResetQueryPool(buf, pool, id*2, 2);
        vkCmdWriteTimestamp2KHR(buf, stage, pool, (uint32_t)id*2);
    }
}

void timer::stop(VkCommandBuffer buf, uint32_t image_index, VkPipelineStageFlags2KHR stage)
{
    if(id >= 0)
    {
        VkQueryPool pool = ctx->get_timestamp_query_pool(image_index);
        vkCmdWriteTimestamp2KHR(buf, stage, pool, (uint32_t)id*2+1);
    }
}
