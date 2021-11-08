#include "context.hh"
#include "gltf.hh"
#include "ecs.hh"
#include "gui.hh"
#include "plain_render_pipeline.hh"
#include "imgui.h"
#include <iostream>
#include <memory>

int main()
{
    options opts;
    load_options(opts);

    ecs entities;
    ecs_updater& updater = entities.ensure_system<ecs_updater>();
    context ctx(opts.window_size, opts.fullscreen, opts.vsync);
    gui g(ctx, opts);

    gltf_data main_scene = load_gltf(ctx, "data/white_room.glb", entities);
    gltf_data console = load_gltf(ctx, "data/gbcv2_contraband_asset.glb", entities);

    transformable* cam_transform = entities.get<transformable>(main_scene.entities["Camera"]);
    camera* cam = entities.get<camera>(main_scene.entities["Camera_Orientation"]);
    transformable* gbc = entities.get<transformable>(console.entities["GBC"]);
    gbc->set_parent(cam_transform);

    std::unique_ptr<render_pipeline> pipeline;
    plain_render_pipeline::options plain_options = {
        opts.resolution_scaling, (VkSampleCountFlagBits)opts.msaa_samples
    };
    pipeline.reset(new plain_render_pipeline(ctx, entities, plain_options));

    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);

    bool running = true;
    float pitch = 0, yaw = 0;
    int distance_steps = 0;
    float distance = 0;
    float sensitivity = 0.3f;
    bool need_swapchain_reset = false;
    bool need_pipeline_reset = false;
    vec3 direction = vec3(0);
    gbc->set_position(vec3(0, -0.1, 0));
    while(running)
    {
        SDL_Event event;
        while(SDL_PollEvent(&event))
        {
            g.handle_event(event);
            switch(event.type)
            {
            case SDL_QUIT:
                running = false;
                break;

            case SDL_MOUSEMOTION:
                if(ImGui::GetIO().WantCaptureMouse) break;
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
                if(ImGui::GetIO().WantCaptureKeyboard) break;
                if(event.key.keysym.sym == SDLK_ESCAPE)
                {
                    running = false;
                }
                if(event.key.keysym.sym == SDLK_t)
                {
                    ctx.dump_timing();
                }
                break;

            case SDL_DROPFILE:
                // TODO
                std::cout << "Should load file " << event.drop.file << std::endl;
                opts.push_recent_rom(event.drop.file);
                SDL_free(event.drop.file);
                break;

            case SDL_WINDOWEVENT:
                if(
                    event.window.event == SDL_WINDOWEVENT_RESIZED ||
                    event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED
                ){
                    opts.window_size = ivec2(event.window.data1, event.window.data2);
                    if(opts.window_size != ctx.get_size())
                        need_swapchain_reset = true;
                }
                break;

            case SDL_USEREVENT:
                switch(event.user.code)
                {
                case gui::SET_RESOLUTION_SCALING:
                    plain_options.resolution_scaling = opts.resolution_scaling;
                    if(auto* ptr = dynamic_cast<plain_render_pipeline*>(pipeline.get()))
                        ptr->set_options(plain_options);
                    need_pipeline_reset = true;
                    break;
                case gui::SET_DISPLAY:
                    ctx.set_current_display(opts.display_index);
                    need_swapchain_reset = true;
                    break;
                case gui::FULLSCREEN_TOGGLE:
                    ctx.set_fullscreen(opts.fullscreen);
                    if(!opts.fullscreen)
                        ctx.set_size(opts.window_size);
                    need_swapchain_reset = true;
                    break;
                case gui::VSYNC_TOGGLE:
                    ctx.set_vsync(opts.vsync);
                    need_swapchain_reset = true;
                    break;
                case gui::SET_ANTIALIASING:
                    plain_options.samples = (VkSampleCountFlagBits)opts.msaa_samples;
                    if(auto* ptr = dynamic_cast<plain_render_pipeline*>(pipeline.get()))
                        ptr->set_options(plain_options);
                    need_pipeline_reset = true;
                    break;
                }
                break;
            }
        }

        if(need_swapchain_reset)
        {
            need_swapchain_reset = false;
            if(!opts.fullscreen)
                ctx.set_size(opts.window_size);
            ctx.reset_swapchain();
            need_pipeline_reset = true;
        }

        if(need_pipeline_reset)
        {
            need_pipeline_reset = false;
            pipeline->reset();
        }

        uvec2 size = ctx.get_size();
        float aspect = size.x/float(size.y);
        entities([&](entity id, camera& cam){cam.set_aspect(aspect);});

        gbc->set_orientation(yaw, vec3(0,0,-1));
        gbc->rotate(pitch, vec3(1,0,0));
        distance = 0.1 * pow(1.1, distance_steps);
        direction.y = -1;
        gbc->set_position(distance * direction);

        g.update();

        updater.update(entities);
        pipeline->render();
    }

    write_options(opts);
    return 0;
}
