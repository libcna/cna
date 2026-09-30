#include "AchievementGame.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

// Task 15.9: cna_demo_achievement_showcase. Single process. `--smoke N` exits cleanly after N
// Draw frames for automated verification; it signs in the local profile "Smoke" at startup unless
// CNA_GAMER_SERVICES_AUTO_SIGN_IN is already set.
int main(int argc, char* argv[])
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    int smokeFrames = -1;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--smoke" && i + 1 < argc) { smokeFrames = std::atoi(argv[++i]); }
        else if (arg == "--smoke") { smokeFrames = 180; }
    }

    auto* game = new AchievementGame();
    if (smokeFrames >= 0)
    {
#if defined(_WIN32)
        if (std::getenv("CNA_GAMER_SERVICES_AUTO_SIGN_IN") == nullptr)
        {
            _putenv_s("CNA_GAMER_SERVICES_AUTO_SIGN_IN", "Smoke");
        }
#else
        setenv("CNA_GAMER_SERVICES_AUTO_SIGN_IN", "Smoke", 0);
#endif
        game->SetSmokeFrames(smokeFrames);
    }
    game->Run();
    delete game;
    return 0;
}
