#include "AvatarDemo.hpp"

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;
    auto* game = new AvatarDemo();
    game->Run();
    delete game;
    return 0;
}
