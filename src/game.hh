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
    void update_button_animations();
    static uint32_t autosave(uint32_t interval, void* param);

    ecs ecs_scene;
    options opt;
    ivec2 window_size;
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

    // The button animations are generated and updated programmatically, simply
    // because that's easier than authoring 72 different animations for the
    // dpad.
    struct
    {
        // The dpad animation is the only one that actually changes. The others
        // are predetermined and their timers just go back and forth based on
        // button state.
        mat4 dpad_initial_state;
        animation dpad_button;
        time_ticks dpad_time;
        int dpad_state;

        animation a_button;
        time_ticks a_time;

        animation b_button;
        time_ticks b_time;

        animation start_button;
        time_ticks start_time;

        animation select_button;
        time_ticks select_time;
    } button_animations;
    SDL_TimerID save_timer;
};

#endif

