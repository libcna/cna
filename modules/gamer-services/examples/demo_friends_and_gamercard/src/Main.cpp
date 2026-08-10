#include "FriendsGame.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

// Task 15.14: cna_demo_friends_and_gamercard. Single process. `--smoke N` exits cleanly after N
// Draw frames for automated verification.
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

    auto* game = new FriendsGame();
    if (smokeFrames >= 0)
    {
        game->SetSmokeFrames(smokeFrames);
    }
    game->Run();
    delete game;
    return 0;
}
