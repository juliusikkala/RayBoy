#include "context.hh"
#include "xor_render_stage.hh"
#include <iostream>
#include <memory>

class render_pipeline
{
public:
    render_pipeline(context& ctx)
    : ctx(ctx)
    {
        reset();
    }

    void reset()
    {
        render_target target = ctx.get_render_target();
        xor_stage.reset(new xor_render_stage(ctx, target));
    }

    void render()
    {
        while(ctx.start_frame())
        {
            ctx.reset_swapchain();
            reset();
        }
        uint32_t image_index = ctx.get_image_index();

        VkSemaphore prev = ctx.get_start_semaphore();
        prev = xor_stage->run(image_index, prev);

        // Finish the frame with the semaphore of the last rendering step
        ctx.finish_frame(prev);
    }

private:
    context& ctx;
    std::unique_ptr<xor_render_stage> xor_stage;
};

int main()
{
    context ctx;
    render_pipeline pipeline(ctx);

    bool running = true;
    unsigned counter = 0;
    while(running)
    {
        SDL_Event event;
        while(SDL_PollEvent(&event))
        {
            switch(event.type)
            {
            case SDL_QUIT:
                running = false;
                break;
            case SDL_KEYDOWN:
                if(event.key.keysym.sym == SDLK_ESCAPE)
                {
                    running = false;
                }
                break;
            }
        }

        //std::cout << counter++ << std::endl;
        pipeline.render();
    }

    return 0;
}
