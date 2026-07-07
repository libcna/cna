#include <any>
#include <cstdio>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/GamerServices/Guide.hpp"
#include "Microsoft/Xna/Framework/GamerServices/MessageBoxIcon.hpp"
#include "Microsoft/Xna/Framework/GamerServices/NotificationPosition.hpp"
#include "Microsoft/Xna/Framework/PlayerIndex.hpp"
#include "System/AsyncCallback.hpp"
#include "System/NotSupportedException.hpp"
#include "System/TimeSpan.hpp"

// Task 15.11: cna_demo_guide_overlay_console. The full Guide static API surface, exercised
// through a numbered console menu: each entry triggers one real Guide call and prints its
// result/exception. Console-only, single process, no graphics/window needed at all - confirmed
// safe by reading TextInputEXT::StartTextInput/StopTextInput first (both null-window-guarded, so
// calling Guide::BeginShowKeyboardInput/EndShowKeyboardInput here never touches SDL at all).

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::GamerServices;

namespace
{
    const char* NotificationPositionName(NotificationPosition pos)
    {
        switch (pos)
        {
            case NotificationPosition::TopLeft: return "TopLeft";
            case NotificationPosition::TopCenter: return "TopCenter";
            case NotificationPosition::TopRight: return "TopRight";
            case NotificationPosition::CenterLeft: return "CenterLeft";
            case NotificationPosition::Center: return "Center";
            case NotificationPosition::CenterRight: return "CenterRight";
            case NotificationPosition::BottomLeft: return "BottomLeft";
            case NotificationPosition::BottomCenter: return "BottomCenter";
            case NotificationPosition::BottomRight: return "BottomRight";
        }
        return "?";
    }

    void MenuShowSignIn()
    {
        Guide::ShowSignIn(2, false);
        std::printf("  ShowSignIn(2, false) called - no-op in this platform's implementation "
                    "(no crash, no visible effect).\n");
    }

    void MenuKeyboardInput()
    {
        System::IAsyncResult* result = Guide::BeginShowKeyboardInput(
            PlayerIndex::One, "Title", "Description", "default text", System::AsyncCallback{}, std::any{});
        const std::string text = Guide::EndShowKeyboardInput(result);
        delete result;
        std::printf("  BeginShowKeyboardInput()+EndShowKeyboardInput() completed instantly, "
                    "result=\"%s\" (expected: always empty in this platform's implementation).\n",
                    text.c_str());
    }

    void MenuMessageBox()
    {
        try
        {
            System::IAsyncResult* result = Guide::BeginShowMessageBox(
                "Title", "Text", {"OK", "Cancel"}, 0, MessageBoxIcon::Alert,
                System::AsyncCallback{}, std::any{});
            delete result;
            std::printf("  BeginShowMessageBox() unexpectedly did not throw.\n");
        }
        catch (const System::NotSupportedException&)
        {
            std::printf("  BeginShowMessageBox() threw NotSupportedException as expected.\n");
        }
        try
        {
            [[maybe_unused]] auto unused = Guide::EndShowMessageBox(nullptr);
            std::printf("  EndShowMessageBox(nullptr) unexpectedly did not throw.\n");
        }
        catch (const System::NotSupportedException&)
        {
            std::printf("  EndShowMessageBox(nullptr) threw NotSupportedException as expected.\n");
        }
    }

    void MenuTrialMode()
    {
        const bool before = Guide::getIsTrialModeProperty();
        Guide::setIsTrialModeProperty(!before);
        std::printf("  IsTrialMode: %s -> %s\n", before ? "true" : "false",
                    Guide::getIsTrialModeProperty() ? "true" : "false");
    }

    void MenuSimulateTrialMode()
    {
        const bool before = Guide::getSimulateTrialModeProperty();
        Guide::setSimulateTrialModeProperty(!before);
        std::printf("  SimulateTrialMode: %s -> %s\n", before ? "true" : "false",
                    Guide::getSimulateTrialModeProperty() ? "true" : "false");
    }

    void MenuScreenSaver()
    {
        const bool before = Guide::getIsScreenSaverEnabledProperty();
        Guide::setIsScreenSaverEnabledProperty(!before);
        const bool after = Guide::getIsScreenSaverEnabledProperty();
        std::printf("  IsScreenSaverEnabled: %s -> %s", before ? "true" : "false", after ? "true" : "false");
        if (before == after)
        {
            // Confirmed by direct SDL probe before writing this: IsScreenSaverEnabled/
            // SetIsScreenSaverEnabledProperty wrap real SDL_ScreenSaverEnabled()/SDL_Enable/
            // DisableScreenSaver(), which only take effect once SDL's video subsystem is
            // initialized - this demo intentionally has none (matching the plan's own "no
            // graphics needed" scope for this console-only demo), so no change is expected here.
            std::printf(" (unchanged - real SDL screensaver calls need an initialized video "
                        "subsystem, which this console-only demo intentionally has none of)");
        }
        std::printf("\n");
    }

    void MenuNotificationPosition()
    {
        const NotificationPosition before = Guide::getNotificationPositionProperty();
        const auto next = static_cast<NotificationPosition>(
            (static_cast<int>(before) + 1) % 9);
        Guide::setNotificationPositionProperty(next);
        std::printf("  NotificationPosition: %s -> %s\n", NotificationPositionName(before),
                    NotificationPositionName(Guide::getNotificationPositionProperty()));
    }

    void MenuDelayNotifications()
    {
        Guide::DelayNotifications(System::TimeSpan::FromSeconds(5.0));
        std::printf("  DelayNotifications(5s) called - no-op in this platform's implementation "
                    "(no observable effect, no crash).\n");
    }

    struct MenuItem
    {
        const char* label;
        std::function<void()> action;
    };
}

int main(int argc, char* argv[])
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    bool autoRun = false;
    for (int i = 1; i < argc; ++i)
    {
        if (std::string(argv[i]) == "--auto") { autoRun = true; }
    }

    const std::vector<MenuItem> items = {
        {"ShowSignIn", MenuShowSignIn},
        {"BeginShowKeyboardInput/EndShowKeyboardInput", MenuKeyboardInput},
        {"BeginShowMessageBox/EndShowMessageBox (expected to throw)", MenuMessageBox},
        {"Toggle IsTrialMode", MenuTrialMode},
        {"Toggle SimulateTrialMode", MenuSimulateTrialMode},
        {"Toggle IsScreenSaverEnabled", MenuScreenSaver},
        {"Cycle NotificationPosition", MenuNotificationPosition},
        {"DelayNotifications(5s)", MenuDelayNotifications},
    };

    if (autoRun)
    {
        std::printf("[Guide] --auto: running every menu item once in order.\n");
        for (std::size_t i = 0; i < items.size(); ++i)
        {
            std::printf("[Guide] %zu. %s\n", i + 1, items[i].label);
            items[i].action();
        }
        std::printf("[Guide] Auto run complete: %zu/%zu items executed.\n", items.size(), items.size());
        return 0;
    }

    while (true)
    {
        std::printf("\n--- Guide overlay console demo ---\n");
        for (std::size_t i = 0; i < items.size(); ++i)
        {
            std::printf("%zu. %s\n", i + 1, items[i].label);
        }
        std::printf("0. Quit\n> ");

        std::string line;
        if (!std::getline(std::cin, line))
        {
            break;
        }
        int choice = -1;
        try
        {
            choice = std::stoi(line);
        }
        catch (...)
        {
            std::printf("Invalid input.\n");
            continue;
        }
        if (choice == 0)
        {
            break;
        }
        if (choice < 1 || choice > static_cast<int>(items.size()))
        {
            std::printf("Out of range.\n");
            continue;
        }
        items[static_cast<std::size_t>(choice - 1)].action();
    }

    return 0;
}
