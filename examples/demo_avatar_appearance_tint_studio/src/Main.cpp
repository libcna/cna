#include "TintStudioDemo.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

// Task 15.17: cna_demo_avatar_appearance_tint_studio. Single process. `--smoke N` exits cleanly
// after N Draw frames for automated verification.
int main(int argc, char* argv[])
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    int smokeFrames = -1;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--smoke" && i + 1 < argc) { smokeFrames = std::atoi(argv[++i]); }
        else if (arg == "--smoke") { smokeFrames = 200; }
    }

    auto* game = new TintStudioDemo();
    if (smokeFrames >= 0)
    {
        game->SetSmokeFrames(smokeFrames);
    }
    game->Run();
    delete game;
    return 0;
}
