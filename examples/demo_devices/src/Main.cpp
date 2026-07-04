#include "DevicesDemo.hpp"

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;
    auto* game = new DevicesDemo();
    game->Run();
    delete game;
    return 0;
}
