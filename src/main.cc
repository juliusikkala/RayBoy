#include "game.hh"

int main(int argc, char** argv)
{
    game g(argc == 2 ? argv[1] : nullptr);
    for(;;)
    {
        if(!g.handle_input())
            break;
        g.update();
        g.render();
    }

    return 0;
}
