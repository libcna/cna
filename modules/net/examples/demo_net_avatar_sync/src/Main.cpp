#include "SyncGame.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

// Task 15.21 (bonus, cross-cutting): cna_demo_net_avatar_sync. Launch as `--host` (default,
// creates a real SystemLink session, Male avatar) or `--join` (finds/joins the host, Female
// avatar). `--smoke N` exits cleanly after N Draw frames for automated verification.
int main(int argc, char* argv[])
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    bool isHost = true;
    int smokeFrames = -1;
    bool showHelp = false;
    std::string screenshotPath;

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--host") { isHost = true; }
        else if (arg == "--join") { isHost = false; }
        else if (arg == "--smoke" && i + 1 < argc) { smokeFrames = std::atoi(argv[++i]); }
        else if (arg == "--smoke") { smokeFrames = 180; }
        // Task 8.5 (plans/plan_net.md Phase 8): verifies the overlay renders via a non-interactive
        // smoke/screenshot run, without needing simulated keyboard input.
        else if (arg == "--show-help") { showHelp = true; }
        else if (arg == "--screenshot" && i + 1 < argc) { screenshotPath = argv[++i]; }
    }

    auto* game = new SyncGame(isHost);
    if (smokeFrames >= 0)
    {
        game->SetSmokeFrames(smokeFrames);
    }
    if (showHelp)
    {
        game->SetShowHelpForTestingEXT(true);
    }
    if (!screenshotPath.empty())
    {
        game->SetScreenshotPathEXT(screenshotPath);
    }
    game->Run();
    delete game;
    return 0;
}
