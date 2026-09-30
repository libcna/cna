#include "WatchdogGame.hpp"

#include <cstdio>
#include <cstdlib>

// Task 15.12: cna_demo_gamerservices_dispatcher_watchdog. Single process, self-terminating once
// all 3 checks succeed (no --smoke flag needed - the whole point is a bounded, visible sequence).
// Create and GetAchievements need a signed-in gamer, so the local profile "Watchdog" signs in at
// startup unless CNA_GAMER_SERVICES_AUTO_SIGN_IN is already set.
int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
#if defined(_WIN32)
    if (std::getenv("CNA_GAMER_SERVICES_AUTO_SIGN_IN") == nullptr)
    {
        _putenv_s("CNA_GAMER_SERVICES_AUTO_SIGN_IN", "Watchdog");
    }
#else
    setenv("CNA_GAMER_SERVICES_AUTO_SIGN_IN", "Watchdog", 0);
#endif

    auto* game = new WatchdogGame();
    game->Run();
    delete game;
    return 0;
}
