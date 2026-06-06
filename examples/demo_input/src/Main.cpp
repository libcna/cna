#include "InputDemo.hpp"

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;
    auto* game = new InputDemo();
    game->Run();
    delete game;
    return 0;
}
