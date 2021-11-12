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
        SET_RENDERING_MODE
    };

    void handle_event(const SDL_Event& event);
    void update();

private:
    bool show_menubar;
    context* ctx;
    options* opts;
};

#endif
