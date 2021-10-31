#include "context.hh"
#include "gltf.hh"
#include "ecs.hh"
#include "plain_render_pipeline.hh"
#include <iostream>
#include <memory>

int main()
{
    ecs entities;
    ecs_updater& updater = entities.ensure_system<ecs_updater>();
    context ctx;

    gltf_data gltf = load_gltf(ctx, "data/teapot_sponza.glb", entities);

    std::unique_ptr<render_pipeline> pipeline;
    pipeline.reset(new plain_render_pipeline(ctx, entities));

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
                if(event.key.keysym.sym == SDLK_t)
                {
                    ctx.dump_timing();
                }
                break;
            }
        }

        //std::cout << counter++ << std::endl;
        updater.update(entities);
        pipeline->render();
    }

    return 0;
}
