#include <any>
#include <cstdio>
#include <functional>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/GamerServices/Guide.hpp"
#include "Microsoft/Xna/Framework/GamerServices/MessageBoxIcon.hpp"
#include "Microsoft/Xna/Framework/GamerServices/NotificationPosition.hpp"
#include "Microsoft/Xna/Framework/Input/TextInputEXT.hpp"
#include "Microsoft/Xna/Framework/PlayerIndex.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "System/ArgumentException.hpp"
#include "System/AsyncCallback.hpp"
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

    // Task 3.2: BeginShowKeyboardInput no longer completes synchronously - it accumulates real
    // Microsoft::Xna::Framework::Input::TextInputEXT::TextInput code units and completes on Enter,
    // matching a real on-screen keyboard. This console-only demo has no real window to type into
    // (see this file's own top-of-file comment), so it drives the exact same event stream
    // TextInputEXT::INTERNAL_OnTextInput would receive from a real keystroke, one code unit at a
    // time, ending with Enter (\r) - a headless stand-in for real typing, the same idea as
    // MenuMessageBox's SimulateMessageBoxClickEXT just below.
    void MenuKeyboardInput()
    {
        using Microsoft::Xna::Framework::Input::TextInputEXT;

        System::IAsyncResult* result = Guide::BeginShowKeyboardInput(
            PlayerIndex::One, "Title", "Description", "default text", System::AsyncCallback{}, std::any{});
        std::printf("  BeginShowKeyboardInput() returned a real, not-yet-completed IAsyncResult "
                    "(IsCompleted=%s).\n", result->getIsCompletedProperty() ? "true" : "false");

        const std::string typed = " overridden";
        for (const char c : typed)
        {
            TextInputEXT::INTERNAL_OnTextInput(static_cast<SharpRuntime::charcs>(c));
        }
        TextInputEXT::INTERNAL_OnTextInput(static_cast<SharpRuntime::charcs>(u'\r')); // Enter confirms

        const std::string text = Guide::EndShowKeyboardInput(result);
        delete result;
        std::printf("  Simulated typing \"%s\" + Enter -> EndShowKeyboardInput() returned "
                    "\"%s\" (default text + what was typed), as expected for a real implementation.\n",
                    typed.c_str(), text.c_str());
    }

    // Task 3.1: BeginShowMessageBox/EndShowMessageBox are now a real (not NotSupportedException)
    // implementation - but the real, mouse-driven completion path (RenderPendingMessageBoxEXT)
    // needs a SpriteBatch/GraphicsDevice this console-only demo intentionally has none of (see
    // this file's own top-of-file comment). SimulateMessageBoxClickEXT is the dedicated headless
    // entry point for exactly this case - exercises the full real Begin -> completion -> End
    // cycle without needing a window.
    void MenuMessageBox()
    {
        System::IAsyncResult* result = Guide::BeginShowMessageBox(
            "Title", "Text", {"OK", "Cancel"}, 0, MessageBoxIcon::Alert,
            System::AsyncCallback{}, std::any{});
        std::printf("  BeginShowMessageBox() returned a real, not-yet-completed IAsyncResult "
                    "(IsCompleted=%s).\n", result->getIsCompletedProperty() ? "true" : "false");

        Guide::SimulateMessageBoxClickEXT(1); // headless stand-in for a real "Cancel" click
        std::optional<int> selected = Guide::EndShowMessageBox(result);
        std::printf("  SimulateMessageBoxClickEXT(1) + EndShowMessageBox() -> selected button %d "
                    "(\"%s\"), as expected for a real implementation.\n",
                    selected.value_or(-1), selected.value_or(-1) == 1 ? "Cancel" : "?");
        delete result;

        try
        {
            [[maybe_unused]] auto unused = Guide::EndShowMessageBox(nullptr);
            std::printf("  EndShowMessageBox(nullptr) unexpectedly did not throw.\n");
        }
        catch (const System::ArgumentException&)
        {
            std::printf("  EndShowMessageBox(nullptr) threw ArgumentException as expected "
                        "(not a result BeginShowMessageBox returned).\n");
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
            // SetIsScreenSaverEnabledProperty wraps the real platform screen-saver controls.
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
        {"BeginShowMessageBox/SimulateMessageBoxClickEXT/EndShowMessageBox", MenuMessageBox},
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
