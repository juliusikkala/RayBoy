#include "game.hh"
#include "imgui.h"
#define AUTOSAVE_INTERVAL (60*1000)

namespace
{

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

}

game::game(const char* initial_rom)
:   updater(ecs_scene.ensure_system<ecs_updater>()),
    need_swapchain_reset(false), need_pipeline_reset(false), gbc(nullptr),
    cam_transform(nullptr), cam(nullptr)
{
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
}

game::~game()
{
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
    gbc->set_position(vec3(0, -0.1, 0));
}

void game::load_scene(const std::string& name)
{
    // Just nuke the pipeline and ensure all things that could use assets
    // from the current scene are destroyed.
    pipeline.reset();
    gfx_ctx->sync_flush();

    if(gbc) gbc->set_parent(nullptr);
    scene_data.remove(ecs_scene);
    scene_data = load_gltf(
        *gfx_ctx,
        get_readonly_path("data/"+name+".glb"),
        ecs_scene
    );

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
                viewer.pitch = clamp(viewer.pitch, -110.0f, 110.0f);
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
                viewer.direction.x = clamp(viewer.direction.x, -0.5f, 0.5f);
                viewer.direction.z = clamp(viewer.direction.z, -0.5f, 0.5f);
            }
            break;

        case SDL_MOUSEWHEEL:
            viewer.distance_steps -= event.wheel.y;
            viewer.distance_steps = clamp(viewer.distance_steps, 0, 10);
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
            }
            break;
        }
    }
    return true;
}

void game::update()
{
    uvec2 size = gfx_ctx->get_size();
    float aspect = size.x/float(size.y);
    ecs_scene([&](entity id, camera& cam){cam.set_aspect(aspect);});

    gbc->set_orientation(viewer.yaw, vec3(0,0,-1));
    gbc->rotate(viewer.pitch, vec3(1,0,0));
    float distance = 0.1 * pow(1.1, viewer.distance_steps);
    viewer.direction.y = -1;
    gbc->set_position(distance * viewer.direction);

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
            false,
            opt.colormapping,
            opt.render_subpixels
        };
        pipeline.reset(new plain_render_pipeline(*gfx_ctx, *emu, plain_options));
        emu->set_audio_mode(nullptr);
    }
    else if(opt.mode == "fancy")
    {
        fancy_render_pipeline::options fancy_options = {
            opt.resolution_scaling, (VkSampleCountFlagBits)opt.msaa_samples
        };
        pipeline.reset(new fancy_render_pipeline(*gfx_ctx, ecs_scene, fancy_options));
        emu->set_audio_mode(
            ecs_scene.get<transformable>(console_data.entities["Speaker"])
        );
    }
}

void game::refresh_pipeline_options()
{
    if(auto* ptr = dynamic_cast<plain_render_pipeline*>(pipeline.get()))
    {
        plain_render_pipeline::options plain_options = {
            false,
            opt.colormapping,
            opt.render_subpixels
        };
        ptr->set_options(plain_options);
    }
    if(auto* ptr = dynamic_cast<fancy_render_pipeline*>(pipeline.get()))
    {
        fancy_render_pipeline::options fancy_options = {
            opt.resolution_scaling, (VkSampleCountFlagBits)opt.msaa_samples
        };
        ptr->set_options(fancy_options);
    }
    need_pipeline_reset = true;
}

uint32_t game::autosave(uint32_t interval, void* param)
{
    game* self = (game*)param;
    self->emu->save_sav();
    return interval;
}
