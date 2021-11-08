#include "gui.hh"
#include "backends/imgui_impl_sdl.h"
#include "backends/imgui_impl_vulkan.h"

namespace
{

PFN_vkVoidFunction loader_func(const char* name, void* userdata)
{
    context* ctx = (context*)userdata;
    return vkGetInstanceProcAddr(ctx->get_instance(), name);
}

}

gui::gui(context& ctx)
: ctx(&ctx)
{
    ImGui::CreateContext();
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
    ImGui_ImplSDL2_ProcessEvent(&event);
}

void gui::update()
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    ImGui::Begin("Options");
    ImGui::Text("Toimii!!!!");
    ImGui::End();

    ImGui::Render();
}
