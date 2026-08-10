#include "BrowserGame.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

// Task 15.5: cna_demo_session_browser. Launch one or more processes with `--host [--max-gamers N]`
// to advertise a session, and exactly one process with `--browse` to search, scroll (Up/Down), and
// join (Enter) one of them. `--smoke N` exits cleanly after N Draw frames for automated
// verification, auto-selecting entry `--select I` (default 0) once it has been discovered.
int main(int argc, char* argv[])
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    bool isHost = false;
    int maxGamersForHost = 8;
    int smokeFrames = -1;
    int smokeSelectIndex = 0;

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--host") { isHost = true; }
        else if (arg == "--browse") { isHost = false; }
        else if (arg == "--max-gamers" && i + 1 < argc) { maxGamersForHost = std::atoi(argv[++i]); }
        else if (arg == "--select" && i + 1 < argc) { smokeSelectIndex = std::atoi(argv[++i]); }
        else if (arg == "--smoke" && i + 1 < argc) { smokeFrames = std::atoi(argv[++i]); }
        else if (arg == "--smoke") { smokeFrames = 180; }
    }

    auto* game = new BrowserGame(isHost, maxGamersForHost, smokeSelectIndex);
    if (smokeFrames >= 0)
    {
        game->SetSmokeFrames(smokeFrames);
    }
    game->Run();
    delete game;
    return 0;
}
