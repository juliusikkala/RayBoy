#ifndef RAYBOY_GUI_HH
#define RAYBOY_GUI_HH

#include "context.hh"
#include "options.hh"

class gui
{
public:
    gui(context& ctx, options& opts);
    ~gui();

    enum option_events
    {
        SET_RESOLUTION_SCALING = 0,
        FULLSCREEN_TOGGLE,
        VSYNC_TOGGLE,
        COLORMAPPING_TOGGLE,
        SUBPIXELS_TOGGLE,
        PIXEL_TRANSITIONS_TOGGLE,
        SET_DISPLAY,
        SET_ANTIALIASING,
        SET_RENDERING_MODE,
        SET_GB_COLOR,
        SET_RT_OPTION,
        SET_SCENE
    };

    void handle_event(const SDL_Event& event);
    void update();

private:
    void menu_file();
    void menu_window();
    void menu_graphics();
    void menu_help();
    void help_controls();
    void help_license();
    void help_about();

    bool show_menubar;
    bool show_controls;
    bool show_license;
    bool show_about;
    context* ctx;
    options* opts;
};

#endif
