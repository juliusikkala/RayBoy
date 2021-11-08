#ifndef RAYBOY_GUI_RENDER_STAGE_HH
#define RAYBOY_GUI_RENDER_STAGE_HH

#include "render_stage.hh"
#include "gui.hh"
#include "timer.hh"

class render_target;
class gui_render_stage: public render_stage
{
public:
    gui_render_stage(context& ctx);
    ~gui_render_stage();

protected:
    void update_buffers(uint32_t image_index) override;

private:
    vkres<VkDescriptorPool> descriptor_pool;
    vkres<VkRenderPass> render_pass;
    std::vector<vkres<VkFramebuffer>> framebuffers;
    timer stage_timer;
};

#endif
