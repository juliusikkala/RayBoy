#include "game.hh"

int main()
{
    game g;
    g.load_common_assets();
    g.load_scene("white_room");
    for(;;)
    {
        if(!g.handle_input())
            break;
        g.update();
        g.render();
    }

    return 0;
}
