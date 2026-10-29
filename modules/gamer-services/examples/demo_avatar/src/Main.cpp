#include "AvatarDemo.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

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

    // --description takes an avatar's 1021 bytes as 2042 hex digits, or a file holding them raw or
    // as hex (what the CNA avatar editor or the service stores).
    std::vector<SharpRuntime::bytecs> ParseDescription(std::string text)
    {
        if (text.size() != 2042)
        {
            std::ifstream file(text, std::ios::binary);
            text.assign(std::istreambuf_iterator<char>(file), {});
            if (text.size() == 1021)
            {
                return {text.begin(), text.end()};
            }
            while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.pop_back();
        }
        std::vector<SharpRuntime::bytecs> bytes;
        for (std::size_t i = 0; i + 1 < text.size() && text.size() == 2042; i += 2)
        {
            bytes.push_back(static_cast<SharpRuntime::bytecs>(std::stoi(text.substr(i, 2), nullptr, 16)));
        }
        if (bytes.size() != 1021)
        {
            std::fprintf(stderr, "cna_demo_avatar: --description needs 1021 bytes (2042 hex digits or a file)\n");
            std::exit(64);
        }
        return bytes;
    }
}

int main(int argc, char* argv[])
{
    // Net Task 7.1: --smoke N / --yaw <degrees> / --screenshot <path> together
    // give a reproducible, non-interactive way to capture baseline/after documentation
    // screenshots - --yaw fixes the orbiting camera instead of requiring live keyboard input, and
    // --screenshot saves the final smoke frame's backbuffer as a PNG before exiting.
    int smokeFrames = -1;
    float yawDegrees = 0.0f;
    bool haveYaw = false;
    std::string screenshotPath;
    std::string clipName;
    bool showHelp = false;
    int lineup = 1;
    std::string viewName;
    std::vector<SharpRuntime::bytecs> description;
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
        else if (std::strcmp(argv[i], "--lineup") == 0 && i + 1 < argc)
        {
            lineup = std::clamp(std::atoi(argv[++i]), 1, 9);
        }
        else if (std::strcmp(argv[i], "--view") == 0 && i + 1 < argc)
        {
            viewName = argv[++i];
        }
        else if (std::strcmp(argv[i], "--description") == 0 && i + 1 < argc)
        {
            description = ParseDescription(argv[++i]);
        }
        else if (std::strcmp(argv[i], "--show-help") == 0)
        {
            // Net Task 8.5: verifies the overlay actually renders via a
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
    game->SetLineupEXT(lineup);
    if (!description.empty())
    {
        game->SetDescriptionEXT(std::move(description));
    }
    const char* views[] = {"body", "head", "hands", "feet"};
    for (int view = 0; view < 4; ++view)
    {
        if (viewName == views[view])
        {
            game->SetViewEXT(static_cast<AvatarDemo::View>(view));
        }
    }
    game->Run();
    delete game;
    return 0;
}
