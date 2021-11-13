#ifndef RAYBOY_GAME_HH
#define RAYBOY_GAME_HH

#include "context.hh"
#include "gltf.hh"
#include "ecs.hh"
#include "gui.hh"
#include "plain_render_pipeline.hh"
#include "fancy_render_pipeline.hh"
#include "audio.hh"
#include "emulator.hh"
#include "io.hh"
#include <memory>

class game
{
public:
    game(const char* initial_rom);
    ~game();

    void load_common_assets();
    void load_scene(const std::string& name);
    bool handle_input();
    void update();
    void render();

private:
    void create_pipeline();
    void refresh_pipeline_options();
    void update_gbc_material();
    static uint32_t autosave(uint32_t interval, void* param);

    ecs ecs_scene;
    options opt;
    bool need_swapchain_reset;
    bool need_pipeline_reset;
    ecs_updater& updater;
    std::unique_ptr<context> gfx_ctx;
    std::unique_ptr<audio> audio_ctx;
    std::unique_ptr<gui> ui;
    std::unique_ptr<emulator> emu;
    std::unique_ptr<render_pipeline> pipeline;
    gltf_data console_data;
    gltf_data scene_data;
    float delta_time;
    std::chrono::steady_clock::time_point frame_start;

    std::map<int /*index*/, SDL_GameController*> controllers;

    transformable* gbc;
    transformable* cam_transform;
    camera* cam;
    struct
    {
        float pitch = 0, yaw = 0;
        float sensitivity = 0.1;
        float distance_steps = 0;
        vec3 direction = vec3(0);
    } viewer;
    SDL_TimerID save_timer;
};

#endif

