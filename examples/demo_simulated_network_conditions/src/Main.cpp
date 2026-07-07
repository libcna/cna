#include "SimGame.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

// Task 15.4: cna_demo_simulated_network_conditions. Launch as `--host` (default, creates a real
// SystemLink session) or `--join` (finds and joins the host via NetworkSession::Find/Join).
// `--smoke N` exits cleanly after N Draw frames for automated verification.
int main(int argc, char* argv[])
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    bool isHost = true;
    int smokeFrames = -1;

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--host") { isHost = true; }
        else if (arg == "--join") { isHost = false; }
        else if (arg == "--smoke" && i + 1 < argc) { smokeFrames = std::atoi(argv[++i]); }
        else if (arg == "--smoke") { smokeFrames = 180; }
    }

    auto* game = new SimGame(isHost);
    if (smokeFrames >= 0)
    {
        game->SetSmokeFrames(smokeFrames);
    }
    game->Run();
    delete game;
    return 0;
}
