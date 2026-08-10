#include <SDL3/SDL_main.h>

#include "DevicesDemo.hpp"

// plan_devices.md Task DEVICES-0125/0126: on Android, SDL's SDLActivity.java
// looks up a symbol literally named SDL_main via dlsym() -- <SDL3/SDL_main.h>
// #defines main to SDL_main when SDL_PLATFORM_ANDROID is set (SDL_MAIN_NEEDED),
// so this plain main() below becomes the exported SDL_main() Android needs.
// A no-op on desktop platforms, where this redirection doesn't apply.
int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;
    auto* game = new DevicesDemo();
    game->Run();
    delete game;
    return 0;
}
