#include "BoundaryDemo.hpp"

#include <cstdio>
#include <string>

// Task 15.20: cna_demo_avatar_bone_state_boundary. Single process, minimal window - all real
// content is printed to console during Initialize(); exits itself after a few frames.
int main(int argc, char* argv[])
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    bool showHelp = false;
    std::string screenshotPath;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        // Task 8.5 (plans/plan_net.md Phase 8): verifies the overlay renders via a non-interactive
        // screenshot run, without needing simulated keyboard input.
        if (arg == "--show-help") { showHelp = true; }
        else if (arg == "--screenshot" && i + 1 < argc) { screenshotPath = argv[++i]; }
    }

    auto* game = new BoundaryDemo();
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
