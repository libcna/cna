#include "AvatarDemo.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace
{
    // --gender male|female picks the body type of the random avatar (default male); anything else
    // is a usage error.
    Microsoft::Xna::Framework::GamerServices::AvatarBodyType ParseGenderArg(int argc, char* argv[])
    {
        using Microsoft::Xna::Framework::GamerServices::AvatarBodyType;
        for (int i = 1; i + 1 < argc; ++i)
        {
            if (std::strcmp(argv[i], "--gender") == 0)
            {
                if (std::strcmp(argv[i + 1], "female") == 0) { return AvatarBodyType::Female; }
                if (std::strcmp(argv[i + 1], "male") == 0) { return AvatarBodyType::Male; }
                std::fprintf(stderr, "cna_demo_avatar: unrecognized --gender value \"%s\" "
                                      "(expected \"male\" or \"female\")\n", argv[i + 1]);
                std::exit(64);
            }
        }
        return AvatarBodyType::Male;
    }

}

int main(int argc, char* argv[])
{
    // Task 7.1 (plans/plan_net.md Phase 7): --smoke N / --yaw <degrees> / --screenshot <path> together
    // give a reproducible, non-interactive way to capture baseline/after documentation
    // screenshots - --yaw fixes the orbiting camera instead of requiring live keyboard input, and
    // --screenshot saves the final smoke frame's backbuffer as a PNG before exiting.
    int smokeFrames = -1;
    float yawDegrees = 0.0f;
    bool haveYaw = false;
    std::string screenshotPath;
    std::string clipName;
    bool showHelp = false;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--smoke") == 0)
        {
            smokeFrames = (i + 1 < argc) ? std::atoi(argv[++i]) : 180;
        }
        else if (std::strcmp(argv[i], "--yaw") == 0 && i + 1 < argc)
        {
            yawDegrees = static_cast<float>(std::atof(argv[++i]));
            haveYaw = true;
        }
        else if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc)
        {
            screenshotPath = argv[++i];
        }
        else if (std::strcmp(argv[i], "--clip") == 0 && i + 1 < argc)
        {
            clipName = argv[++i];
        }
        else if (std::strcmp(argv[i], "--show-help") == 0)
        {
            // Task 8.5 (plans/plan_net.md Phase 8): verifies the overlay actually renders via a
            // non-interactive smoke/screenshot run, without needing simulated keyboard input.
            showHelp = true;
        }
    }

    auto* game = new AvatarDemo(ParseGenderArg(argc, argv));
    if (smokeFrames >= 0)
    {
        game->SetSmokeFrames(smokeFrames);
    }
    if (haveYaw)
    {
        constexpr float kPi = 3.14159265358979323846f;
        game->SetFixedCameraYawEXT(yawDegrees * kPi / 180.0f);
    }
    if (!screenshotPath.empty())
    {
        game->SetScreenshotPathEXT(screenshotPath);
    }
    if (!clipName.empty())
    {
        game->SetInitialPresetEXT(clipName);
    }
    if (showHelp)
    {
        game->SetShowHelpForTestingEXT(true);
    }
    game->Run();
    delete game;
    return 0;
}
