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

    gltf_data main_scene = load_gltf(ctx, "data/white_room.glb", entities);
    gltf_data console = load_gltf(ctx, "data/gbcv2_contraband_asset.glb", entities);

    transformable* cam_transform = entities.get<transformable>(main_scene.entities["Camera"]);
    camera* cam = entities.get<camera>(main_scene.entities["Camera_Orientation"]);
    transformable* gbc = entities.get<transformable>(console.entities["GBC"]);
    gbc->set_parent(cam_transform);

    uvec2 size = ctx.get_size();
    float aspect = size.x/float(size.y);
    entities([&](entity id, camera& cam){cam.set_aspect(aspect);});

    std::unique_ptr<render_pipeline> pipeline;
    pipeline.reset(new plain_render_pipeline(ctx, entities, {VK_SAMPLE_COUNT_1_BIT}));

    bool running = true;
    unsigned counter = 0;
    float pitch = 0, yaw = 0;
    int distance_steps = 0;
    float distance = 0;
    float sensitivity = 0.3f;
    vec3 direction = vec3(0);
    gbc->set_position(vec3(0, -0.1, 0));
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

            case SDL_MOUSEMOTION:
                if(event.motion.state&SDL_BUTTON_LMASK)
                {
                    pitch += event.motion.yrel * sensitivity;
                    yaw += event.motion.xrel * sensitivity;
                    pitch = clamp(pitch, -110.0f, 110.0f);
                }

                if(event.motion.state&SDL_BUTTON_RMASK)
                {
                    vec2 next_uv = vec2(
                        event.motion.x,
                        event.motion.y
                    )/vec2(ctx.get_size());
                    next_uv.y = 1.0 - next_uv.y;

                    vec2 prev_uv = vec2(
                        event.motion.x-event.motion.xrel,
                        event.motion.y+event.motion.yrel
                    )/vec2(ctx.get_size());
                    prev_uv.y = 1.0 - prev_uv.y;

                    ray next_ray = cam->get_view_ray(next_uv, 0.0f);
                    ray prev_ray = cam->get_view_ray(prev_uv, 0.0f);
                    
                    vec3 delta =
                        next_ray.dir/next_ray.dir.z-
                        prev_ray.dir/prev_ray.dir.z;

                    direction.x -= delta.x;
                    direction.z -= delta.y;
                    direction.x = clamp(direction.x, -0.5f, 0.5f);
                    direction.z = clamp(direction.z, -0.5f, 0.5f);
                }
                break;

            case SDL_MOUSEWHEEL:
                distance_steps -= event.wheel.y;
                distance_steps = clamp(distance_steps, 0, 10);
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
        gbc->set_orientation(yaw, vec3(0,0,-1));
        gbc->rotate(pitch, vec3(1,0,0));
        distance = 0.1 * pow(1.1, distance_steps);
        direction.y = -1;
        gbc->set_position(distance * direction);

        //std::cout << counter++ << std::endl;
        updater.update(entities);
        pipeline->render();
    }

    return 0;
}
