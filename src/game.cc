#include "game.hh"
#include "environment_map.hh"
#include "imgui.h"
#include "scene.hh"
#define AUTOSAVE_INTERVAL (60*1000)
#define BUTTON_ANIMATION_LENGTH_US (75000l)

namespace
{

struct console_entity {};
struct scene_entity {};

float deadzone(float value, float dz)
{
    float magnitude = max(abs(value)-dz, 0.0f)/(1.0f-dz);
    return sign(value)*magnitude;
}

float calc_accumulation_ratio(int accumulation)
{
    return 0.5f/(accumulation*accumulation+1);
}

void handle_emulator_input(emulator& emu, const SDL_Event& event)
{
    std::unordered_map<SDL_Keycode, GB_key_t> kb_bindings = {
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
    std::unordered_map<SDL_GameControllerButton, GB_key_t> ctrl_bindings = {
        {SDL_CONTROLLER_BUTTON_A, GB_KEY_A},
        {SDL_CONTROLLER_BUTTON_B, GB_KEY_B},
        {SDL_CONTROLLER_BUTTON_START, GB_KEY_START},
        {SDL_CONTROLLER_BUTTON_BACK, GB_KEY_SELECT},
        {SDL_CONTROLLER_BUTTON_DPAD_UP, GB_KEY_UP},
        {SDL_CONTROLLER_BUTTON_DPAD_DOWN, GB_KEY_DOWN},
        {SDL_CONTROLLER_BUTTON_DPAD_LEFT, GB_KEY_LEFT},
        {SDL_CONTROLLER_BUTTON_DPAD_RIGHT, GB_KEY_RIGHT},
    };
    if(event.type == SDL_KEYDOWN || event.type == SDL_KEYUP)
    {
        auto it = kb_bindings.find(event.key.keysym.sym);
        if(it != kb_bindings.end())
        {
            emu.set_button(it->second, event.type == SDL_KEYDOWN);
        }
    }
    else if(event.type == SDL_CONTROLLERBUTTONDOWN || event.type == SDL_CONTROLLERBUTTONUP)
    {
        auto it = ctrl_bindings.find((SDL_GameControllerButton)event.cbutton.button);
        if(it != ctrl_bindings.end())
        {
            emu.set_button(it->second, event.type == SDL_CONTROLLERBUTTONDOWN);
        }
    }
}

}

game::game(const char* initial_rom)
:   updater(ecs_scene.ensure_system<ecs_updater>()),
    need_swapchain_reset(false), need_pipeline_reset(false),
    delta_time(0), gbc(nullptr), cam_transform(nullptr), cam(nullptr)
{
    frame_start = std::chrono::steady_clock::now();
    load_options(opt);
    gfx_ctx.reset(new context(opt.window_size, opt.fullscreen, opt.vsync));
    audio_ctx.reset(new audio());
    ui.reset(new gui(*gfx_ctx, opt));
    emu.reset(new emulator(*audio_ctx));
    emu->set_power(true);

    if(initial_rom && emu->load_rom(initial_rom))
    {
        opt.push_recent_rom(initial_rom);
        emu->print_info();
    }

    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);
    save_timer = SDL_AddTimer(AUTOSAVE_INTERVAL, autosave, this);

    load_common_assets();
    load_scene(opt.scene);
}

game::~game()
{
    for(auto pair: controllers)
    {
        SDL_GameControllerClose(pair.second);
    }
    SDL_RemoveTimer(save_timer);
    emu->save_sav();
    write_options(opt);
}

void game::load_common_assets()
{
    console_data = load_gltf(
        *gfx_ctx,
        get_readonly_path("data/gbcv2_contraband_asset.glb"),
        ecs_scene
    );
    gbc = ecs_scene.get<transformable>(console_data.entities["GBC"]);
    update_gbc_material();
    for(const auto& [name, id]: console_data.entities)
    {
        ecs_scene.attach(id, console_entity{});
        ecs_scene.attach(id, ray_traced{});
        if(!ecs_scene.has<outer_layer>(id))
            ecs_scene.remove<visible>(id);
    }
    ecs_scene.attach(
        console_data.entities["Screen"],
        ray_traced{true, false, false}
    );

    // Setup default animations
    mat4 a_initial_state = ecs_scene.get<transformable>(console_data.entities["A button"])->get_transform();
    mat4 b_initial_state = ecs_scene.get<transformable>(console_data.entities["B button"])->get_transform();
    mat4 start_initial_state = ecs_scene.get<transformable>(console_data.entities["Start"])->get_transform();
    mat4 select_initial_state = ecs_scene.get<transformable>(console_data.entities["Select"])->get_transform();
    button_animations.dpad_initial_state = ecs_scene.get<transformable>(console_data.entities["Dpad button"])->get_transform();

    mat4 a_pressed = glm::translate(a_initial_state, vec3(0,-0.0007,0));
    mat4 b_pressed = glm::translate(b_initial_state, vec3(0,-0.0007,0));
    mat4 start_pressed = glm::translate(start_initial_state, vec3(0,-0.0005,0));
    mat4 select_pressed = glm::translate(select_initial_state, vec3(0,-0.0005,0));

    button_animations.a_button.set_transform(
        animation::SMOOTHSTEP,
        {{0, a_initial_state}, {BUTTON_ANIMATION_LENGTH_US, a_pressed}}
    );

    button_animations.b_button.set_transform(
        animation::SMOOTHSTEP,
        {{0, b_initial_state}, {BUTTON_ANIMATION_LENGTH_US, b_pressed}}
    );

    button_animations.select_button.set_transform(
        animation::SMOOTHSTEP,
        {{0, select_initial_state}, {BUTTON_ANIMATION_LENGTH_US, select_pressed}}
    );

    button_animations.start_button.set_transform(
        animation::SMOOTHSTEP,
        {{0, start_initial_state}, {BUTTON_ANIMATION_LENGTH_US, start_pressed}}
    );

    button_animations.a_time = 0;
    button_animations.b_time = 0;
    button_animations.select_time = 0;
    button_animations.start_time = 0;
    button_animations.dpad_time = 0;
    button_animations.dpad_state = 0;
}

void game::load_scene(const std::string& name)
{
    // Just nuke the pipeline and ensure all things that could use assets
    // from the current scene are destroyed.
    pipeline.reset();
    if(gbc) gbc->set_parent(nullptr);
    scene_data.remove(ecs_scene);
    gfx_ctx->sync_flush();

    scene_data = load_gltf(
        *gfx_ctx,
        get_readonly_path("data/"+name+".glb"),
        ecs_scene
    );

    std::string radiance_path = get_readonly_path("data/"+name+"_radiance.ktx");
    std::string irradiance_path = get_readonly_path("data/"+name+"_irradiance.ktx");
    std::string lightmap_path = get_readonly_path("data/"+name+"_lightmap.hdr");

    texture* radiance_ptr = nullptr;
    texture* irradiance_ptr = nullptr;
    environment_map* envmap = nullptr;
    if(fs::exists(irradiance_path) && fs::exists(radiance_path))
    {
        radiance_ptr = new texture(*gfx_ctx, radiance_path);
        irradiance_ptr = new texture(*gfx_ctx, irradiance_path);
        scene_data.textures.emplace_back(radiance_ptr);
        scene_data.textures.emplace_back(irradiance_ptr);
        entity envmap_id = ecs_scene.add(environment_map{
            radiance_ptr, irradiance_ptr
        });
        scene_data.entities["ENVMAP"] = envmap_id;
        envmap = ecs_scene.get<environment_map>(envmap_id);
    }

    texture* lightmap_ptr = nullptr;
    if(fs::exists(lightmap_path))
    {
        lightmap_ptr = new texture(*gfx_ctx, lightmap_path);
        scene_data.textures.emplace_back(lightmap_ptr);
    }

    for(const auto& [name, id]: scene_data.entities)
    {
        ecs_scene.attach(id, scene_entity{});
    }

    ecs_scene([&](entity id, console_entity* ce, scene_entity* se, model& m){
        for(auto& vg: m)
        {
            vg.mat.envmap = envmap;
            if(se)
            {
                vg.mat.lightmap = {
                    scene_data.samplers.back().get(),
                    lightmap_ptr
                };
            }
        }
    });

    cam_transform = ecs_scene.get<transformable>(scene_data.entities["Camera"]);
    cam = ecs_scene.get<camera>(scene_data.entities["Camera_Orientation"]);
    if(gbc) gbc->set_parent(cam_transform);
    audio_ctx->set_listener(
        ecs_scene.get<transformable>(scene_data.entities["Camera_Orientation"])
    );
}

bool game::handle_input()
{
    SDL_Event event;
    while(SDL_PollEvent(&event))
    {
        ui->handle_event(event);
        switch(event.type)
        {
        case SDL_QUIT:
            return false;

        case SDL_MOUSEMOTION:
            if(ImGui::GetIO().WantCaptureMouse) break;
            if(event.motion.state&SDL_BUTTON_LMASK)
            {
                viewer.pitch += event.motion.yrel * viewer.sensitivity;
                viewer.yaw += event.motion.xrel * viewer.sensitivity;
            }

            if(event.motion.state&SDL_BUTTON_RMASK)
            {
                vec2 next_uv = vec2(
                    event.motion.x,
                    event.motion.y
                )/vec2(gfx_ctx->get_size());
                next_uv.y = 1.0 - next_uv.y;

                vec2 prev_uv = vec2(
                    event.motion.x-event.motion.xrel,
                    event.motion.y+event.motion.yrel
                )/vec2(gfx_ctx->get_size());
                prev_uv.y = 1.0 - prev_uv.y;

                ray next_ray = cam->get_view_ray(next_uv, 0.0f);
                ray prev_ray = cam->get_view_ray(prev_uv, 0.0f);

                vec3 delta =
                    next_ray.dir/next_ray.dir.z-
                    prev_ray.dir/prev_ray.dir.z;

                viewer.direction.x -= delta.x;
                viewer.direction.z -= delta.y;
            }
            break;

        case SDL_MOUSEWHEEL:
            viewer.distance_steps -= event.wheel.y;
            break;

        case SDL_CONTROLLERDEVICEADDED:
            if(SDL_GameController* ctrl = SDL_GameControllerOpen(event.cdevice.which))
            {
                controllers[event.cdevice.which] = ctrl;
            }
            break;

        case SDL_CONTROLLERDEVICEREMOVED:
            {
            auto it = controllers.find(event.cdevice.which);
            if(it != controllers.end())
            {
                SDL_GameControllerClose(it->second);
                controllers.erase(it);
            }
            }
            break;

        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERBUTTONUP:
            handle_emulator_input(*emu, event);
            break;

        case SDL_KEYDOWN:
        case SDL_KEYUP:
            if(ImGui::GetIO().WantCaptureKeyboard) break;
            if(event.key.keysym.sym == SDLK_ESCAPE)
            {
                return false;
            }
            if(event.key.keysym.sym == SDLK_t && event.type == SDL_KEYDOWN)
            {
                gfx_ctx->dump_timing();
            }
            handle_emulator_input(*emu, event);
            break;

        case SDL_DROPFILE:
            {
            fs::path path(event.drop.file);
            emu->save_sav();
            if(path.extension() == ".sav")
            {
                emu->load_sav(event.drop.file);
            }
            else if(path.extension() == ".gbc" || path.extension() == ".gb")
            {
                if(emu->load_rom(event.drop.file))
                {
                    opt.push_recent_rom(event.drop.file);
                    emu->print_info();
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
                opt.window_size = ivec2(event.window.data1, event.window.data2);
                if(opt.window_size != gfx_ctx->get_size())
                    need_swapchain_reset = true;
            }
            break;

        case SDL_USEREVENT:
            switch(event.user.code)
            {
            case gui::SET_RESOLUTION_SCALING:
            case gui::SET_ANTIALIASING:
            case gui::COLORMAPPING_TOGGLE:
            case gui::SUBPIXELS_TOGGLE:
            case gui::PIXEL_TRANSITIONS_TOGGLE:
                refresh_pipeline_options();
                break;
            case gui::SET_DISPLAY:
                gfx_ctx->set_current_display(opt.display_index);
                need_swapchain_reset = true;
                break;
            case gui::FULLSCREEN_TOGGLE:
                gfx_ctx->set_fullscreen(opt.fullscreen);
                if(!opt.fullscreen)
                    gfx_ctx->set_size(opt.window_size);
                need_swapchain_reset = true;
                break;
            case gui::VSYNC_TOGGLE:
                gfx_ctx->set_vsync(opt.vsync);
                need_swapchain_reset = true;
                break;
            case gui::SET_RENDERING_MODE:
                pipeline.reset();
                break;
            case gui::SET_GB_COLOR:
                update_gbc_material();
                break;
            case gui::SET_RT_OPTION:
                pipeline.reset();
                break;
            case gui::SET_SCENE:
                load_scene(opt.scene);
                break;
            }
            break;
        }
    }

    for(auto pair: controllers)
    {
        float xmot = SDL_GameControllerGetAxis(pair.second, SDL_CONTROLLER_AXIS_LEFTX)/32768.0;
        float ymot = SDL_GameControllerGetAxis(pair.second, SDL_CONTROLLER_AXIS_LEFTY)/32768.0;
        xmot = deadzone(xmot, 0.2);
        ymot = deadzone(ymot, 0.2);
        viewer.direction.x += xmot * delta_time * 0.5;
        viewer.direction.z += ymot * delta_time * 0.5;

        float xrot = SDL_GameControllerGetAxis(pair.second, SDL_CONTROLLER_AXIS_RIGHTX)/32768.0;
        float yrot = SDL_GameControllerGetAxis(pair.second, SDL_CONTROLLER_AXIS_RIGHTY)/32768.0;
        xrot = deadzone(xrot, 0.2);
        yrot = deadzone(yrot, 0.2);
        viewer.pitch += yrot * 200 * delta_time;
        viewer.yaw += xrot * 200 * delta_time;

        float zoomin = SDL_GameControllerGetAxis(pair.second, SDL_CONTROLLER_AXIS_TRIGGERRIGHT)/32768.0;
        float zoomout = SDL_GameControllerGetAxis(pair.second, SDL_CONTROLLER_AXIS_TRIGGERLEFT)/32768.0;
        zoomin = deadzone(zoomin, 0.1);
        zoomout = deadzone(zoomout, 0.1);
        float zoom = zoomout - zoomin;

        viewer.distance_steps += zoom * 10 * delta_time;
    }

    return true;
}

void game::update()
{
    auto frame_end = std::chrono::steady_clock::now();
    std::chrono::duration<float> delta = frame_end - frame_start;
    delta_time = delta.count();
    frame_start = frame_end;

    uvec2 size = gfx_ctx->get_size();
    float aspect = size.x/float(size.y);
    ecs_scene([&](entity id, camera& cam){cam.set_aspect(aspect);});

    viewer.pitch = clamp(viewer.pitch, -110.0f, 110.0f);
    viewer.direction.x = clamp(viewer.direction.x, -0.5f, 0.5f);
    viewer.direction.z = clamp(viewer.direction.z, -0.5f, 0.5f);
    viewer.distance_steps = clamp(viewer.distance_steps, 0.0f, 10.0f);

    gbc->set_orientation(viewer.yaw, vec3(0,0,-1));
    gbc->rotate(viewer.pitch, vec3(1,0,0));
    float distance = 0.08 * pow(1.1, viewer.distance_steps);
    viewer.direction.y = -1;
    gbc->set_position(distance * viewer.direction);

    update_button_animations();
    updater.update(ecs_scene);
    audio_ctx->update();
}

void game::render()
{
    if(need_swapchain_reset)
    {
        need_swapchain_reset = false;
        if(!opt.fullscreen)
            gfx_ctx->set_size(opt.window_size);
        gfx_ctx->reset_swapchain();
        need_pipeline_reset = true;
    }

    if(!pipeline)
    {
        create_pipeline();
        need_pipeline_reset = false;
    }

    if(need_pipeline_reset)
    {
        need_pipeline_reset = false;
        pipeline->reset();
    }

    ui->update();
    pipeline->render();
}

void game::create_pipeline()
{
    if(opt.mode == "plain")
    {
        plain_render_pipeline::options plain_options = {
            opt.colormapping,
            opt.render_subpixels,
        };
        pipeline.reset(new plain_render_pipeline(*gfx_ctx, *emu, plain_options));
        emu->set_audio_mode(nullptr);
        emu->set_framebuffer_fade(opt.pixel_transitions);
    }
    else if(opt.mode == "fancy")
    {
        fancy_render_pipeline::options fancy_options = {
            opt.resolution_scaling,
            (VkSampleCountFlagBits)opt.msaa_samples,
            gfx_ctx->get_device().supports_ray_tracing && opt.ray_tracing,
            opt.shadow_rays,
            opt.reflection_rays,
            opt.refraction_rays,
            calc_accumulation_ratio(opt.accumulation)
        };
        model* screen_model = ecs_scene.get<model>(console_data.entities["Screen"]);
        material* screen_mat = &(*screen_model)[3].mat;
        pipeline.reset(new fancy_render_pipeline(*gfx_ctx, ecs_scene, screen_mat, *emu, fancy_options));
        emu->set_audio_mode(
            ecs_scene.get<transformable>(console_data.entities["Speaker"])
        );
        emu->set_framebuffer_fade(true);
    }
}

void game::refresh_pipeline_options()
{
    if(auto* ptr = dynamic_cast<plain_render_pipeline*>(pipeline.get()))
    {
        plain_render_pipeline::options plain_options = {
            opt.colormapping,
            opt.render_subpixels
        };
        ptr->set_options(plain_options);
        emu->set_framebuffer_fade(opt.pixel_transitions);
    }
    if(auto* ptr = dynamic_cast<fancy_render_pipeline*>(pipeline.get()))
    {
        fancy_render_pipeline::options fancy_options = {
            opt.resolution_scaling, (VkSampleCountFlagBits)opt.msaa_samples,
            gfx_ctx->get_device().supports_ray_tracing && opt.ray_tracing,
            opt.shadow_rays,
            opt.reflection_rays,
            opt.refraction_rays,
            calc_accumulation_ratio(opt.accumulation)
        };
        ptr->set_options(fancy_options);
    }
    need_pipeline_reset = true;
}

void game::update_gbc_material()
{
    model* battery_cover = ecs_scene.get<model>(console_data.entities["Battery cover"]);
    model* back_panel = ecs_scene.get<model>(console_data.entities["Back panel"]);
    model* front_panel = ecs_scene.get<model>(console_data.entities["Front panel"]);
    model* led = ecs_scene.get<model>(console_data.entities["Red LED"]);

    vec3 color = vec3(0);
    float metallic = 0.0f;
    float transmittance = 0.0f;

    if(opt.gb_color == "grape")
    {
        color = vec3(0.07, 0, 0.25);
    }
    else if(opt.gb_color == "teal")
    {
        color = vec3(0.0, 0.128, 0.17);
    }
    else if(opt.gb_color == "kiwi")
    {
        color = vec3(0.2, 0.56, 0.0);
    }
    else if(opt.gb_color == "berry")
    {
        color = vec3(0.6, 0., 0.045);
    }
    else if(opt.gb_color == "dandelion")
    {
        color = vec3(0.7, 0.4, 0);
    }
    else if(opt.gb_color == "atomic-purple")
    {
        color = vec3(0.805, 0.74, 0.85);
        transmittance = 1.0f;
    }
    else if(opt.gb_color == "aluminum")
    {
        color = vec3(0.7);
        metallic = 1.0f;
    }
    else if(opt.gb_color == "black")
    {
        color = vec3(0.01);
    }
    else if(opt.gb_color == "white")
    {
        color = vec3(0.95);
    }

    for(auto& vg: *battery_cover)
    {
        vg.mat.color_factor = vec4(color, 1);
        vg.mat.metallic_factor = metallic;
        vg.mat.transmittance = transmittance;
    }

    for(auto& vg: *back_panel)
    {
        vg.mat.color_factor = vec4(color, 1);
        vg.mat.metallic_factor = metallic;
        vg.mat.transmittance = transmittance;
    }

    for(auto& vg: *front_panel)
    {
        vg.mat.color_factor = vec4(color, 1);
        vg.mat.metallic_factor = metallic;
        vg.mat.transmittance = transmittance;
    }
}

void game::update_button_animations()
{
    time_ticks dt = delta_time*1000000l;

    if(emu->get_button(GB_KEY_A)) button_animations.a_time += dt;
    else button_animations.a_time -= dt;
    button_animations.a_time = clamp(button_animations.a_time, 0l, BUTTON_ANIMATION_LENGTH_US);

    if(emu->get_button(GB_KEY_B)) button_animations.b_time += dt;
    else button_animations.b_time -= dt;
    button_animations.b_time = clamp(button_animations.b_time, 0l, BUTTON_ANIMATION_LENGTH_US);

    if(emu->get_button(GB_KEY_START)) button_animations.start_time += dt;
    else button_animations.start_time -= dt;
    button_animations.start_time = clamp(button_animations.start_time, 0l, BUTTON_ANIMATION_LENGTH_US);

    if(emu->get_button(GB_KEY_SELECT)) button_animations.select_time += dt;
    else button_animations.select_time -= dt;
    button_animations.select_time = clamp(button_animations.select_time, 0l, BUTTON_ANIMATION_LENGTH_US);

    int new_dpad_state = 0;
    if(emu->get_button(GB_KEY_DOWN)) new_dpad_state |= 1;
    if(emu->get_button(GB_KEY_UP)) new_dpad_state |= 2;
    if(emu->get_button(GB_KEY_LEFT)) new_dpad_state |= 4;
    if(emu->get_button(GB_KEY_RIGHT)) new_dpad_state |= 8;

    if(new_dpad_state != button_animations.dpad_state)
    {
        button_animations.dpad_state = new_dpad_state;
        button_animations.dpad_time = 0;
        mat4 cur_dpad_state = ecs_scene.get<transformable>(console_data.entities["Dpad button"])->get_transform();
        mat4 target_state = button_animations.dpad_initial_state;
        // If opposite sides are pressed, just push the dpad down a bit.
        if(
            (emu->get_button(GB_KEY_DOWN) && emu->get_button(GB_KEY_UP)) ||
            (emu->get_button(GB_KEY_LEFT) && emu->get_button(GB_KEY_RIGHT))
        ){
            target_state = glm::translate(target_state, vec3(0,-0.0005,0));
        }
        else if(new_dpad_state != 0)
        {
            vec2 axis = vec2(0);
            if(emu->get_button(GB_KEY_DOWN)) axis.x = 1;
            if(emu->get_button(GB_KEY_UP)) axis.x = -1;
            if(emu->get_button(GB_KEY_LEFT)) axis.y = 1;
            if(emu->get_button(GB_KEY_RIGHT)) axis.y = -1;
            axis = normalize(axis);
            target_state = glm::rotate(target_state, radians(3.5f), vec3(axis.x, 0, axis.y));
            target_state = glm::translate(target_state, vec3(0,-0.0002,0));
        }

        button_animations.dpad_button.set_transform(
            animation::SMOOTHSTEP,
            {{0, cur_dpad_state}, {BUTTON_ANIMATION_LENGTH_US, target_state}}
        );
    }
    button_animations.dpad_time += dt;

    button_animations.a_button.apply(
        *ecs_scene.get<transformable>(console_data.entities["A button"]),
        button_animations.a_time
    );

    button_animations.b_button.apply(
        *ecs_scene.get<transformable>(console_data.entities["B button"]),
        button_animations.b_time
    );

    button_animations.start_button.apply(
        *ecs_scene.get<transformable>(console_data.entities["Start"]),
        button_animations.start_time
    );

    button_animations.select_button.apply(
        *ecs_scene.get<transformable>(console_data.entities["Select"]),
        button_animations.select_time
    );

    button_animations.dpad_button.apply(
        *ecs_scene.get<transformable>(console_data.entities["Dpad button"]),
        button_animations.dpad_time
    );
}

uint32_t game::autosave(uint32_t interval, void* param)
{
    game* self = (game*)param;
    self->emu->save_sav();
    return interval;
}
