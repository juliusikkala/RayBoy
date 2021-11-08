#ifndef RAYBOY_GUI_HH
#define RAYBOY_GUI_HH

#include "context.hh"

class gui
{
public:
    gui(context& ctx);
    ~gui();

    void handle_event(const SDL_Event& event);
    void update();

private:
    context* ctx;
};

#endif
