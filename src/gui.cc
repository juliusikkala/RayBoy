#include "gui.hh"
#include "backends/imgui_impl_sdl.h"
#include "backends/imgui_impl_vulkan.h"
#include "nfd.h"
#include "io.hh"


namespace
{

PFN_vkVoidFunction loader_func(const char* name, void* userdata)
{
    context* ctx = (context*)userdata;
    return vkGetInstanceProcAddr(ctx->get_instance(), name);
}

void push_file_event(const char* name)
{
    SDL_Event e;
    e.type = SDL_DROPFILE;
    e.drop.file = (char*)SDL_malloc(strlen(name)+1);
    strcpy(e.drop.file, name);
    SDL_PushEvent(&e);
}

}

gui::gui(context& ctx, options& opts)
:   ctx(&ctx), opts(&opts), show_menubar(true), show_controls(false),
    show_license(false), show_about(false)
{
    ImGui::CreateContext();
    static std::string ini_path = (get_writable_path()/"imgui.ini").string();
    ImGui::GetIO().IniFilename = ini_path.c_str();
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForVulkan(ctx.get_window());
    ImGui_ImplVulkan_LoadFunctions(loader_func, &ctx);
}

gui::~gui()
{
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
}

void gui::handle_event(const SDL_Event& event)
{
    if(!ImGui::GetIO().WantCaptureKeyboard && event.type == SDL_KEYDOWN)
    {
        if(event.key.keysym.sym == SDLK_LALT)
        {
            show_menubar = !show_menubar;
        }
    }
    ImGui_ImplSDL2_ProcessEvent(&event);
}

void gui::update()
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    if(show_menubar && ImGui::BeginMainMenuBar())
    {
        if(ImGui::BeginMenu("File"))
        {
            menu_file();
            ImGui::EndMenu();
        }

        if(ImGui::BeginMenu("Window"))
        {
            menu_window();
            ImGui::EndMenu();
        }

        if(ImGui::BeginMenu("Graphics"))
        {
            menu_graphics();
            ImGui::EndMenu();
        }

        if(ImGui::BeginMenu("Help"))
        {
            menu_help();
            ImGui::EndMenu();
        }
        ImGui::TextColored(
            ImVec4(0.4, 0.4, 0.4, 1.0),
            "  Press left alt to toggle this bar on and off"
        );
        ImGui::EndMainMenuBar();
    }

    if(show_controls) help_controls();
    if(show_license) help_license();
    if(show_about) help_about();

    ImGui::Render();
}

void gui::menu_file()
{
    if(ImGui::MenuItem("Open ROM"))
    {
        nfdchar_t* file_path = nullptr;
        if(NFD_OpenDialog("gb,gbc", NULL, &file_path) == NFD_OKAY)
        {
            push_file_event(file_path);
            free(file_path);
        }
    }

    if(ImGui::BeginMenu("Open Recent"))
    {
        for(std::string& name: opts->recent_roms)
        {
            if(ImGui::MenuItem(name.c_str()))
                push_file_event(name.c_str());
        }
        ImGui::EndMenu();
    }

    if(ImGui::MenuItem("Load save"))
    {
        nfdchar_t* file_path = nullptr;
        if(NFD_OpenDialog("sav", NULL, &file_path) == NFD_OKAY)
        {
            push_file_event(file_path);
            free(file_path);
        }
    }

    if(ImGui::MenuItem("Quit"))
    {
        SDL_Event e;
        e.type = SDL_QUIT;
        SDL_PushEvent(&e);
    }
}

void gui::menu_help()
{
    if(ImGui::MenuItem("Controls"))
    {
        show_controls = true;
    }

    if(ImGui::MenuItem("License"))
    {
        show_license = true;
    }

    if(ImGui::MenuItem("About"))
    {
        show_about = true;
    }
}

void gui::menu_window()
{
    if(ImGui::MenuItem("Fullscreen", NULL, opts->fullscreen))
    {
        opts->fullscreen = !opts->fullscreen;
        SDL_Event e;
        e.type = SDL_USEREVENT;
        e.user.code = FULLSCREEN_TOGGLE;
        SDL_PushEvent(&e);
    }

    if(ImGui::MenuItem("Vertical sync", NULL, opts->vsync))
    {
        opts->vsync = !opts->vsync;
        SDL_Event e;
        e.type = SDL_USEREVENT;
        e.user.code = VSYNC_TOGGLE;
        SDL_PushEvent(&e);
    }

    if(ctx->is_hdr_available() && ImGui::MenuItem("HDR", NULL, opts->hdr))
    {
        opts->hdr = !opts->hdr;
        SDL_Event e;
        e.type = SDL_USEREVENT;
        e.user.code = HDR_TOGGLE;
        SDL_PushEvent(&e);
    }

    if(ImGui::BeginMenu("Resolution"))
    {
        static constexpr struct {
            const char* name;
            float value;
        } scale_options[] = {
            {"10%", 0.1},
            {"20%", 0.2},
            {"25%", 0.25},
            {"33%", 1.0/3.0},
            {"40%", 0.4},
            {"50%", 0.5},
            {"60%", 0.6},
            {"66%", 2.0/3.0},
            {"75%", 0.75},
            {"80%", 0.8},
            {"85%", 0.85},
            {"90%", 0.9},
            {"95%", 0.95},
            {"100%", 1.0}
        };
        for(auto [name, value]: scale_options)
        {
            if(ImGui::MenuItem(name, NULL, fabs(value-opts->resolution_scaling) <= 1e-3))
            {
                opts->resolution_scaling = value;
                SDL_Event e;
                e.type = SDL_USEREVENT;
                e.user.code = SET_RESOLUTION_SCALING;
                SDL_PushEvent(&e);
            }
        }
        ImGui::EndMenu();
    }

    if(ImGui::BeginMenu("Display"))
    {
        static std::vector<std::string> names;
        for(int i = -1; i < ctx->get_available_displays(); ++i)
        {
            if(i+2 > names.size())
            {
                if(i == -1) names.push_back("automatic");
                else names.push_back(std::to_string(i));
            }

            if(ImGui::MenuItem(names[i+1].c_str(), NULL, i == opts->display_index))
            {
                opts->display_index = i;
                SDL_Event e;
                e.type = SDL_USEREVENT;
                e.user.code = SET_DISPLAY;
                SDL_PushEvent(&e);
            }
        }
        ImGui::EndMenu();
    }
}

void gui::menu_graphics()
{
    if(ImGui::BeginMenu("Graphics mode"))
    {
        static constexpr struct {
            const char* name;
            const char* id;
        } mode_options[] = {
            {"Plain 2D", "plain"},
            {"Fancy 3D", "fancy"}
        };
        for(auto [name, id]: mode_options)
        {
            if(ImGui::MenuItem(name, NULL, opts->mode == id) && opts->mode != id)
            {
                opts->mode = id;
                SDL_Event e;
                e.type = SDL_USEREVENT;
                e.user.code = SET_RENDERING_MODE;
                SDL_PushEvent(&e);
            }
        }
        ImGui::EndMenu();
    }

    if(opts->mode == "plain")
    {
        if(ImGui::MenuItem("Realistic colors", NULL, opts->colormapping))
        {
            opts->colormapping = !opts->colormapping;
            SDL_Event e;
            e.type = SDL_USEREVENT;
            e.user.code = COLORMAPPING_TOGGLE;
            SDL_PushEvent(&e);
        }
        if(ImGui::MenuItem("Subpixels", NULL, opts->render_subpixels))
        {
            opts->render_subpixels = !opts->render_subpixels;
            SDL_Event e;
            e.type = SDL_USEREVENT;
            e.user.code = SUBPIXELS_TOGGLE;
            SDL_PushEvent(&e);
        }
        if(ImGui::MenuItem("Pixel transition", NULL, opts->pixel_transitions))
        {
            opts->pixel_transitions = !opts->pixel_transitions;
            SDL_Event e;
            e.type = SDL_USEREVENT;
            e.user.code = PIXEL_TRANSITIONS_TOGGLE;
            SDL_PushEvent(&e);
        }
    }

    if(opts->mode == "fancy")
    {
        if(ctx->get_device().supports_ray_tracing)
        {
            if(ImGui::MenuItem("Ray tracing", NULL, opts->ray_tracing))
            {
                opts->ray_tracing = !opts->ray_tracing;
                SDL_Event e;
                e.type = SDL_USEREVENT;
                e.user.code = SET_RT_OPTION;
                SDL_PushEvent(&e);
            }
        }
        else
        {
            ImGui::TextColored(
                ImVec4(0.4, 0.4, 0.4, 1.0),
                "Ray tracing not available"
            );
        }

        if(ctx->get_device().supports_ray_tracing && opts->ray_tracing)
        {
            if(ImGui::BeginMenu("Shadow quality"))
            {
                static constexpr struct {
                    const char* name;
                    unsigned value;
                } shadow_options[] = {
                    {"Off", 0},
                    {"Lowest (1 ray)", 1},
                    {"Low (2 rays)", 2},
                    {"Medium (4 rays)", 4},
                    {"High (8 rays)", 8},
                    {"Highest (16 rays)", 16},
                    {"Lagfest (32 rays)", 32},
                    {"Bullshot mode (64 rays)", 64}
                };
                for(auto [name, value]: shadow_options)
                {
                    if(ImGui::MenuItem(name, NULL, value == opts->shadow_rays))
                    {
                        opts->shadow_rays = value;
                        SDL_Event e;
                        e.type = SDL_USEREVENT;
                        e.user.code = SET_RT_OPTION;
                        SDL_PushEvent(&e);
                    }
                }
                ImGui::EndMenu();
            }
            if(ImGui::MenuItem("Secondary shadows", NULL, opts->secondary_shadows))
            {
                opts->secondary_shadows = !opts->secondary_shadows;
                SDL_Event e;
                e.type = SDL_USEREVENT;
                e.user.code = SET_RT_OPTION;
                SDL_PushEvent(&e);
            }
            if(ImGui::BeginMenu("Reflection quality"))
            {
                static constexpr struct {
                    const char* name;
                    unsigned value;
                } reflection_options[] = {
                    {"Off", 0},
                    {"Lowest (1 ray)", 1},
                    {"Low (2 rays)", 2},
                    {"Medium (4 rays)", 4},
                    {"High (8 rays)", 8},
                    {"Highest (16 rays)", 16},
                    {"Lagfest (32 rays)", 32},
                    {"Bullshot mode (64 rays)", 64}
                };
                for(auto [name, value]: reflection_options)
                {
                    if(ImGui::MenuItem(name, NULL, value == opts->reflection_rays))
                    {
                        opts->reflection_rays = value;
                        SDL_Event e;
                        e.type = SDL_USEREVENT;
                        e.user.code = SET_RT_OPTION;
                        SDL_PushEvent(&e);
                    }
                }
                ImGui::EndMenu();
            }
            if(
                opts->gb_color == "atomic-purple" &&
                ImGui::BeginMenu("Refraction quality")
            ){
                static constexpr struct {
                    const char* name;
                    unsigned value;
                } refraction_options[] = {
                    {"Lowest (1 ray)", 1},
                    {"Low (2 rays)", 2},
                    {"Medium (4 rays)", 4},
                    {"High (8 rays)", 8},
                    {"Highest (16 rays)", 16},
                    {"Lagfest (32 rays)", 32},
                    {"Bullshot mode (64 rays)", 64}
                };
                for(auto [name, value]: refraction_options)
                {
                    if(ImGui::MenuItem(name, NULL, value == opts->refraction_rays))
                    {
                        opts->refraction_rays = value;
                        SDL_Event e;
                        e.type = SDL_USEREVENT;
                        e.user.code = SET_RT_OPTION;
                        SDL_PushEvent(&e);
                    }
                }
                ImGui::EndMenu();
            }
            if(ImGui::BeginMenu("Sample accumulation"))
            {
                static constexpr struct {
                    const char* name;
                    int value;
                } accumulation_options[] = {
                    {"Auto (based on ray counts)", -1},
                    {"Off (noisy)", 0},
                    {"Short (noisy)", 1},
                    {"Medium (middle road)", 2},
                    {"Long (noise-free)", 3},
                    {"Very long", 4},
                    {"Outer space", 8}
                };
                for(auto [name, value]: accumulation_options)
                {
                    if(ImGui::MenuItem(name, NULL, value == opts->accumulation))
                    {
                        opts->accumulation = value;
                        SDL_Event e;
                        e.type = SDL_USEREVENT;
                        e.user.code = SET_RT_OPTION;
                        SDL_PushEvent(&e);
                    }
                }
                ImGui::EndMenu();
            }
        }

        if(ImGui::BeginMenu("Console color"))
        {
            static constexpr struct {
                const char* name;
                const char* id;
            } color_options[] = {
                {"Grape", "grape"},
                {"Teal", "teal"},
                {"Kiwi", "kiwi"},
                {"Berry", "berry"},
                {"Dandelion", "dandelion"},
                {"Atomic purple", "atomic-purple"},
                {"Aluminum", "aluminum"},
                {"Black", "black"},
                {"White", "white"}
            };
            for(auto [name, id]: color_options)
            {
                if((!opts->ray_tracing || !ctx->get_device().supports_ray_tracing) && !strcmp(id, "atomic-purple"))
                    continue;

                if(ImGui::MenuItem(name, NULL, opts->gb_color == id))
                {
                    opts->gb_color = id;
                    SDL_Event e;
                    e.type = SDL_USEREVENT;
                    e.user.code = SET_GB_COLOR;
                    SDL_PushEvent(&e);
                }
            }
            ImGui::EndMenu();
        }

        if(ImGui::BeginMenu("Scene"))
        {
            static constexpr struct {
                const char* name;
                const char* id;
            } scene_options[] = {
                {"White room", "white_room"},
                {"Undercover", "undercover"}
            };

            for(auto [name, id]: scene_options)
            {
                if(ImGui::MenuItem(name, NULL, opts->scene == id))
                {
                    opts->scene = id;
                    SDL_Event e;
                    e.type = SDL_USEREVENT;
                    e.user.code = SET_SCENE;
                    SDL_PushEvent(&e);
                }
            }
            ImGui::EndMenu();
        }

        if(ImGui::BeginMenu("Antialiasing"))
        {
            static constexpr const char* sample_count_names[] = {
                "None", "2x", "4x", "8x", "16x"
            };
            int flag = 1;
            for(const char* name: sample_count_names)
            {
                bool available = flag & ctx->get_device().available_sample_counts;
                if(available && ImGui::MenuItem(name, NULL, flag == opts->msaa_samples))
                {
                    opts->msaa_samples = flag;
                    SDL_Event e;
                    e.type = SDL_USEREVENT;
                    e.user.code = SET_ANTIALIASING;
                    SDL_PushEvent(&e);
                }

                flag *= 2;
            }
            ImGui::EndMenu();
        }
    }
}

void gui::help_controls()
{
    if(ImGui::Begin("Controls", &show_controls, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text(R"(
The controls can't be bound to something else in this version of Rayboy, sorry.

Mouse controls:
    [Left click & drag]   = Rotate console
    [Right click & drag]  = Move console
    [Scroll wheel]        = Move console closer and further

Keyboard controls:
    [Escape]            = Close program
    [a, h, left arrow]  = D-Pad left
    [d, l, right arrow] = D-Pad right
    [s, j, down arrow]  = D-Pad down
    [w, k, up arrow]    = D-Pad up
    [z, .]              = A button
    [x, ,]              = B button
    [backspace]         = Select button
    [return]            = Start button
    [Left alt]          = Toggle menu bar
    [F11]               = Toggle fullscreen

Controller (XBOX binds, other controllers have something else):
    [Right stick]   = Rotate console
    [Left stick]    = Move console
    [Right trigger] = Move console closer
    [Left trigger]  = Move console further
    [D-Pad left]    = D-Pad left
    [D-Pad right]   = D-Pad right
    [D-Pad down]    = D-Pad down
    [D-Pad up]      = D-Pad up
    [A]             = A button
    [B]             = B button
    [Back]          = Select button
    [Start]         = Start button
        )");
    }
    ImGui::End();
}

void gui::help_license()
{
    if(ImGui::Begin("Licenses", &show_license, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Separator();
        ImGui::Text(R"(
Rayboy, a Game Boy Color emulator with 3D graphics
Copyright (C) 2021 Julius Ikkala

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
        )");
        ImGui::Separator();
        ImGui::Text(R"(
Rayboy should be distributed with its source code, including the used libraries.
You can find their licenses in the "external" directory. If you have not
received these files, please find them at the project page at
https://github.com/juliusikkala/RayBoy.
        )");
    }
    ImGui::End();
}

void gui::help_about()
{
    if(ImGui::Begin("About Rayboy", &show_about, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text(R"(
Rayboy 1.0.0 ("I haven't thought about versioning yet" edition)
Copyright 2021 Julius Ikkala & contributors
        )");
        ImGui::Separator();
        ImGui::Text(R"(
Rayboy is a Game Boy Color emulator with excessive and flashy 3D graphics. It
supports ray tracing, but can also be run on GPUs without it, with the caveat
that many very important graphical features like shadows are missing in that
case.

This is the first public release of Rayboy, everything _should_ kinda work but
it's barely tested on anything other than some high-end Nvidia GPUs.

If you run into issues, submit them on Github @
https://github.com/juliusikkala/RayBoy so I can take a look at them.  No
promises that I would fix them though. Also, don't blame me if you burn your
GPU with this program, see [About > License] or the COPYING file accompanying
the executable for more info.

Also, shoutouts to all the people making great libraries, Rayboy would not have
been possible without them. Rayboy uses SameBoy for the actual emulation, GLM
for linear algebra, Dear ImGui for GUI stuff like this window, Khronos's KTX
loader library for some special textures, stb_image for the regular textures,
nativefiledialog for the file selector, tiny_gltf for loading 3D models,
nlohmann's json.hpp for saving options, SDL for windowing, audio and inputs,
SoLoud for 3D audio, and Volk & VulkanMemoryAllocator for some Vulkan
boilerplate. Also thanks to Khronos for existing and making open APIs like
Vulkan.

- Julius
        )");
    }
    ImGui::End();
}
