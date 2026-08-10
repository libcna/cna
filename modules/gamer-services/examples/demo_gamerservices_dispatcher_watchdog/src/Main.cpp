#include "WatchdogGame.hpp"

#include <cstdio>

// Task 15.12: cna_demo_gamerservices_dispatcher_watchdog. Single process, self-terminating once
// all 3 checks succeed (no --smoke flag needed - the whole point is a bounded, visible sequence).
int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    auto* game = new WatchdogGame();
    game->Run();
    delete game;
    return 0;
}
