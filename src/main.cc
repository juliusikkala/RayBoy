#include "context.hh"
#include "gltf.hh"
#include "ecs.hh"
#include "gui.hh"
#include "plain_render_pipeline.hh"
#include "fancy_render_pipeline.hh"
#include "emulator.hh"
#include "imgui.h"
#include <iostream>
#include <memory>
#include <unordered_map>

void handle_emulator_input(emulator& emu, const SDL_Event& event)
{
    std::unordered_map<SDL_Keycode, GB_key_t> bindings = {
        {SDLK_z, GB_KEY_A},
        {SDLK_x, GB_KEY_B},
        {SDLK_COMMA, GB_KEY_B},
        {SDLK_PERIOD, GB_KEY_A},
        {SDLK_RETURN, GB_KEY_START},
        {SDLK_BACKSPACE, GB_KEY_SELECT},
        {SDLK_UP, GB_KEY_UP},
        {SDLK_DOWN, GB_KEY_DOWN},
        {SDLK_LEFT, GB_KEY_LEFT},
        {SDLK_RIGHT, GB_KEY_RIGHT},
        {SDLK_w, GB_KEY_UP},
        {SDLK_s, GB_KEY_DOWN},
        {SDLK_a, GB_KEY_LEFT},
        {SDLK_d, GB_KEY_RIGHT},
        {SDLK_k, GB_KEY_UP},
        {SDLK_j, GB_KEY_DOWN},
        {SDLK_h, GB_KEY_LEFT},
        {SDLK_l, GB_KEY_RIGHT}
    };
    auto it = bindings.find(event.key.keysym.sym);
    if(it != bindings.end())
    {
        emu.set_button(it->second, event.type == SDL_KEYDOWN);
    }
}

int main()
{
    options opts;
    load_options(opts);

    ecs entities;
    ecs_updater& updater = entities.ensure_system<ecs_updater>();
    context ctx(opts.window_size, opts.fullscreen, opts.vsync);
    gui g(ctx, opts);
    emulator emu;
    emu.set_power(true);

    gltf_data main_scene = load_gltf(ctx, "data/white_room.glb", entities);
    gltf_data console = load_gltf(ctx, "data/gbcv2_contraband_asset.glb", entities);

    transformable* cam_transform = entities.get<transformable>(main_scene.entities["Camera"]);
    camera* cam = entities.get<camera>(main_scene.entities["Camera_Orientation"]);
    transformable* gbc = entities.get<transformable>(console.entities["GBC"]);
    gbc->set_parent(cam_transform);

    std::unique_ptr<render_pipeline> pipeline;
    plain_render_pipeline::options plain_options = {};
    fancy_render_pipeline::options fancy_options = {
        opts.resolution_scaling, (VkSampleCountFlagBits)opts.msaa_samples
    };
    pipeline.reset(new plain_render_pipeline(ctx, emu, plain_options));
    //pipeline.reset(new fancy_render_pipeline(ctx, entities, fancy_options));

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
            case SDL_KEYUP:
                if(ImGui::GetIO().WantCaptureKeyboard) break;
                if(event.key.keysym.sym == SDLK_ESCAPE)
                {
                    running = false;
                }
                if(event.key.keysym.sym == SDLK_t && event.type == SDL_KEYDOWN)
                {
                    ctx.dump_timing();
                }
                handle_emulator_input(emu, event);
                break;

            case SDL_DROPFILE:
                {
                fs::path path(event.drop.file);
                if(path.extension() == ".sav")
                {
                    emu.load_sav(event.drop.file);
                }
                else if(path.extension() == ".gbc" || path.extension() == ".gb")
                {
                    if(emu.load_rom(event.drop.file))
                    {
                        opts.push_recent_rom(event.drop.file);
                        emu.print_info();
                    }
                }
                SDL_free(event.drop.file);
                }
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
                    fancy_options.resolution_scaling = opts.resolution_scaling;
                    if(auto* ptr = dynamic_cast<fancy_render_pipeline*>(pipeline.get()))
                    {
                        ptr->set_options(fancy_options);
                        need_pipeline_reset = true;
                    }
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
                    fancy_options.samples = (VkSampleCountFlagBits)opts.msaa_samples;
                    if(auto* ptr = dynamic_cast<fancy_render_pipeline*>(pipeline.get()))
                    {
                        ptr->set_options(fancy_options);
                        need_pipeline_reset = true;
                    }
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
