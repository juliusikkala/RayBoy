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
: ctx(&ctx), opts(&opts), show_menubar(true)
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

            ImGui::EndMenu();
        }

        if(ImGui::BeginMenu("View"))
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

            if(ImGui::BeginMenu("Mode"))
            {
                static constexpr struct {
                    const char* name;
                    const char* id;
                } mode_options[] = {
                    {"Plain", "plain"},
                    {"Fancy", "fancy"}
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

            ImGui::EndMenu();
        }
        ImGui::TextColored(
            ImVec4(0.4, 0.4, 0.4, 1.0),
            "  Press left alt to toggle this bar on and off"
        );
        ImGui::EndMainMenuBar();
    }

    ImGui::Render();
}
