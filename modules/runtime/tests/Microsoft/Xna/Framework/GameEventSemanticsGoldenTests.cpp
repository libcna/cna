// SPDX-License-Identifier: MS-PL
//
// PLAT-6: baseline behavioural capture of the runtime's event semantics.
//
// This is an oracle, not a specification. Phase 3 replaces `SDL_PollEvent` in `Game::PollEvents`
// with `IPlatform::PollEvents`, and the point of capturing the current behaviour *before* that
// change is that a migration which quietly alters event semantics is otherwise invisible: nothing
// crashes, nothing fails to build, and a game simply stops deactivating on Alt-Tab.
//
// What is captured is deliberately the *observable* behaviour -- the state a game can actually
// read -- rather than which SDL constants appear in a switch. A transcript of "these events went
// in, this state came out" survives the rewrite; an assertion about `SDL_EVENT_WINDOW_FOCUS_LOST`
// does not.
//
// The transcript is checked in at the path below. On mismatch the test prints both versions and
// fails; regenerating it is a deliberate act (set CNA_UPDATE_EVENT_GOLDEN=1), and the diff that
// shows up in review is exactly the behavioural change under discussion.
//
// Four findings this pins down, each of which a plausible Phase 3 rewrite would get wrong:
//
//   1. The resize handler **ignores the event payload** and re-queries the window: a resize
//      carrying 12345x6789 leaves ClientBounds alone. A migration that reads `PlatformEvent`'s
//      width and height would look more natural and would change behaviour.
//   2. The loop **does not filter by window id**. An event carrying a window id the game does
//      not own still deactivates it, which is why [focus-lost] and
//      [focus-lost-from-a-different-window-id] must stay identical. That is arguably a defect,
//      but it is the current behaviour, and Phase 3 is not the place to change it silently.
//   3. Minimize, restore, maximize, display change and display-scale (DPI) change produce **no
//      reaction at all**. Adding one during migration would be a new feature wearing a
//      refactor's clothes.
//   4. Exit() **does not stop the frame's event draining**: the queue is emptied whether or not
//      a quit was seen, and the input bridge sees every event either way. A rewrite that breaks
//      out of the loop on quit -- an easy and superficially tidy change -- would drop input that
//      currently lands, which is why [quit-then-wheel-in-one-frame] still records a wheel delta.
//
// The transcript is renderer-independent by construction, which is also why ClientBounds reads
// 0x0 under the headless renderer: it owns no window, so the resize finding above shows up as
// "the bogus payload did not become the bounds" rather than as a pair of concrete sizes.

#include <gtest/gtest.h>

#include <SDL3/SDL.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "CNA/Platform/PlatformFactory.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameWindow.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Input/Mouse.hpp"
#include "Microsoft/Xna/Framework/Input/MouseState.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"

using namespace Microsoft::Xna::Framework;

namespace
{
    // Matches GameTests.cpp's own probe-then-skip idiom: Game embeds a real GraphicsDevice, so a
    // genuinely display-less environment must skip rather than fail deep inside renderer setup.
    bool VideoSubsystemAvailable()
    {
        if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
        {
            return false;
        }
        SDL_Window* probe = SDL_CreateWindow("cna-event-golden-probe", 64, 64, SDL_WINDOW_HIDDEN);
        if (probe == nullptr)
        {
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
            return false;
        }
        SDL_DestroyWindow(probe);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return true;
    }

    // Draws nothing and never exits on its own, so every observed transition is attributable to
    // the scripted events rather than to the game's own logic.
    class TranscriptGame : public Game
    {
    protected:
        void LoadContent() override {}
        void Update(GameTime& gameTime) override { Game::Update(gameTime); }
        void Draw(const GameTime& gameTime) override { Game::Draw(gameTime); }
    };

    SDL_Event QuitEvent()
    {
        SDL_Event e{};
        e.type = SDL_EVENT_QUIT;
        return e;
    }

    SDL_Event WindowEvent(const SDL_EventType type, const SDL_WindowID windowId,
                          const Sint32 data1 = 0, const Sint32 data2 = 0)
    {
        SDL_Event e{};
        e.type = type;
        e.window.windowID = windowId;
        e.window.data1 = data1;
        e.window.data2 = data2;
        return e;
    }

    SDL_Event AppLifecycleEvent(const SDL_EventType type)
    {
        SDL_Event e{};
        e.type = type;
        return e;
    }

    /// The one observable that proves an event reached the input bridge at all: the wheel value
    /// is cumulative, so it doubles as a counter of how many wheel events a frame delivered.
    SDL_Event MouseWheelEvent(const float y)
    {
        SDL_Event e{};
        e.type = SDL_EVENT_MOUSE_WHEEL;
        e.wheel.windowID = 0; // no window association needed; raw coordinates pass through
        e.wheel.x = 0.0f;
        e.wheel.y = y;
        return e;
    }

    SDL_Event KeyDownEvent(const SDL_Keycode key, const bool repeat)
    {
        SDL_Event e{};
        e.type = SDL_EVENT_KEY_DOWN;
        e.key.key = key;
        e.key.scancode = SDL_SCANCODE_UNKNOWN;
        e.key.repeat = repeat;
        return e;
    }

    struct Scenario
    {
        std::string name;
        std::vector<SDL_Event> script;
        /// State the game is put into before the script runs, so a scenario reads as a transition
        /// from a stated starting point rather than from whatever the previous one left behind.
        /// It is reached by *replaying a focus event in an earlier frame*, not by poking a
        /// private field -- the setter is private, and using only the public surface keeps the
        /// harness honest about what a game can actually cause.
        bool startActive = true;
    };

    /// Everything a game can observe about the outcome, rendered as one transcript line.
    ///
    /// `Wheel` is relative to the value at the start of the scenario rather than absolute: the
    /// counter is cumulative and process-wide, so an absolute number would encode the order the
    /// scenarios happen to run in and every insertion would rewrite the whole file.
    std::string Observe(Game& game, const int wheelBaseline)
    {
        const Rectangle bounds = game.getWindowProperty().getClientBoundsProperty();
        const int wheel =
            Input::Mouse::GetState().getScrollWheelValueProperty() - wheelBaseline;
        std::ostringstream out;
        out << "IsActive=" << (game.getIsActiveProperty() ? "true" : "false")
            << " RunApplication=" << (game.RunApplication ? "true" : "false")
            << " ClientBounds=" << bounds.Width << "x" << bounds.Height
            << " WheelDelta=" << wheel;
        return out.str();
    }

    /// Runs one frame with exactly the given events in the queue and nothing else.
    void RunFrameWith(Game& game, const std::vector<SDL_Event>& script)
    {
        // Discard anything the windowing system queued on its own (a real driver emits shown,
        // exposed and enter events after window creation). Only the script should be in the
        // queue when the frame runs.
        SDL_PumpEvents();
        SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);

        for (SDL_Event event : script)
        {
            SDL_PushEvent(&event);
        }

        game.RunOneFrame();
    }

    std::string BuildTranscript(Game& game, const SDL_WindowID ownWindow,
                                const std::vector<Scenario>& scenarios)
    {
        std::ostringstream out;
        out << "# PLAT-6 baseline: observable runtime event semantics.\n"
               "# Generated by GameEventSemanticsGoldenTests; regenerate with "
               "CNA_UPDATE_EVENT_GOLDEN=1.\n"
               "# Each block: the events pushed in one frame, then the state after that frame.\n";

        for (const Scenario& scenario : scenarios)
        {
            // A scenario starts from a stated state, so the transcript records a transition and
            // not an accumulation. Without this, a single early Exit() would make every later
            // line read RunApplication=false for no visible reason.
            game.RunApplication = true;
            RunFrameWith(game, {WindowEvent(scenario.startActive ? SDL_EVENT_WINDOW_FOCUS_GAINED
                                                                 : SDL_EVENT_WINDOW_FOCUS_LOST,
                                            ownWindow)});
            game.RunApplication = true;
            const int wheelBaseline = Input::Mouse::GetState().getScrollWheelValueProperty();
            const std::string before = Observe(game, wheelBaseline);

            RunFrameWith(game, scenario.script);

            out << "\n[" << scenario.name << "]\n"
                << "  before: " << before << "\n"
                << "  after:  " << Observe(game, wheelBaseline) << "\n";
        }

        return out.str();
    }

    /// The transcript lives beside the tests, and ctest runs from the repo root. Probing a couple
    /// of parents as well keeps a hand-run binary working from a build directory.
    std::filesystem::path FindGoldenPath()
    {
        const std::filesystem::path relative =
            "modules/runtime/tests/golden/platform-event-semantics.txt";
        std::filesystem::path base = std::filesystem::current_path();
        for (int depth = 0; depth < 4; ++depth)
        {
            if (std::filesystem::exists(base / relative))
            {
                return base / relative;
            }
            if (!base.has_parent_path() || base.parent_path() == base)
            {
                break;
            }
            base = base.parent_path();
        }
        return std::filesystem::current_path() / relative;
    }
}

TEST(GameEventSemanticsGoldenTest, ObservableEventSemanticsMatchTheCapturedBaseline)
{
    // This script enters through SDL_PushEvent, so only the SDL3 implementation can observe it.
    // HEADLESS and TERMINAL exercise their event pumps in the platform conformance suite; treating
    // their intentionally empty native queues as a runtime semantic mismatch would be a false
    // failure after PLAT-47 moved Game onto the selected platform's PollEvents implementation.
    if (CNA::Platform::PlatformFactory::GetDefaultName() != "SDL3")
    {
        GTEST_SKIP() << "The native SDL event transcript applies to the SDL3 platform build.";
    }

    if (!VideoSubsystemAvailable())
    {
        GTEST_SKIP() << "No usable SDL video subsystem in this environment.";
    }

    TranscriptGame game;
    GraphicsDeviceManager gdm(&game);

    // One settling frame before anything is recorded: window creation queues its own events, and
    // attributing those to the first scenario would make the transcript a record of SDL's startup
    // rather than of CNA's event handling.
    ASSERT_NO_THROW(game.RunOneFrame());

    // Two window ids that are deliberately *not* looked up from the game's own window. The loop
    // ignores `event.window.windowID` entirely, and the pair below is how the transcript
    // demonstrates that: identical scripts under different ids must produce identical lines. It
    // also keeps the baseline renderer-independent -- the headless renderer owns no SDL window at
    // all, so a real id is not available under every configuration anyway.
    constexpr SDL_WindowID ownWindow = 1;
    constexpr SDL_WindowID foreignWindow = 1000;

    const std::vector<Scenario> scenarios = {
        {"quit", {QuitEvent()}},
        {"focus-lost", {WindowEvent(SDL_EVENT_WINDOW_FOCUS_LOST, ownWindow)}},
        {"focus-gained-from-inactive",
         {WindowEvent(SDL_EVENT_WINDOW_FOCUS_GAINED, ownWindow)},
         false},
        {"background", {AppLifecycleEvent(SDL_EVENT_WILL_ENTER_BACKGROUND)}},
        {"foreground-from-inactive", {AppLifecycleEvent(SDL_EVENT_DID_ENTER_FOREGROUND)}, false},

        // Ordering, not coalescing: SDL delivers every pushed event, and one frame drains the
        // whole queue, so the last transition in a frame is the one a game sees.
        {"focus-lost-then-gained-in-one-frame",
         {WindowEvent(SDL_EVENT_WINDOW_FOCUS_LOST, ownWindow),
          WindowEvent(SDL_EVENT_WINDOW_FOCUS_GAINED, ownWindow)}},
        {"focus-gained-then-lost-in-one-frame",
         {WindowEvent(SDL_EVENT_WINDOW_FOCUS_GAINED, ownWindow),
          WindowEvent(SDL_EVENT_WINDOW_FOCUS_LOST, ownWindow)}},
        {"quit-then-focus-lost-in-one-frame",
         {QuitEvent(), WindowEvent(SDL_EVENT_WINDOW_FOCUS_LOST, ownWindow)}},

        // Finding 1: the payload is ignored and the window re-queried, so these absurd numbers
        // must not reach ClientBounds.
        {"resize-with-bogus-payload",
         {WindowEvent(SDL_EVENT_WINDOW_RESIZED, ownWindow, 12345, 6789)}},
        {"pixel-size-changed-with-bogus-payload",
         {WindowEvent(SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED, ownWindow, 12345, 6789)}},

        // Finding 2: no window-id filtering. This line must match [focus-lost] exactly; if a
        // Phase 3 rewrite starts filtering by window, they diverge and the diff says so.
        {"focus-lost-from-a-different-window-id",
         {WindowEvent(SDL_EVENT_WINDOW_FOCUS_LOST, foreignWindow)}},

        // Finding 3: no reaction at all.
        {"minimize", {WindowEvent(SDL_EVENT_WINDOW_MINIMIZED, ownWindow)}},
        {"restore", {WindowEvent(SDL_EVENT_WINDOW_RESTORED, ownWindow)}},
        {"maximize", {WindowEvent(SDL_EVENT_WINDOW_MAXIMIZED, ownWindow)}},
        {"display-scale-changed",
         {WindowEvent(SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED, ownWindow)}},
        {"display-changed", {WindowEvent(SDL_EVENT_WINDOW_DISPLAY_CHANGED, ownWindow)}},
        {"window-shown", {WindowEvent(SDL_EVENT_WINDOW_SHOWN, ownWindow)}},
        {"window-hidden", {WindowEvent(SDL_EVENT_WINDOW_HIDDEN, ownWindow)}},
        {"window-close-requested",
         {WindowEvent(SDL_EVENT_WINDOW_CLOSE_REQUESTED, ownWindow)}},

        // A repeat keypress is filtered before the debug hotkeys are considered; an ordinary key
        // reaches the input bridge but changes nothing the game loop observes.
        {"debug-hotkey-f9", {KeyDownEvent(SDLK_F9, false)}},
        {"debug-hotkey-f9-repeat", {KeyDownEvent(SDLK_F9, true)}},
        {"ordinary-keypress", {KeyDownEvent(SDLK_A, false)}},

        // Every event reaches the input bridge *before* the loop's own switch considers it, and
        // the whole queue is drained in one frame. These three lines are what pins that down: the
        // wheel delta counts events the bridge processed, and it is unaffected by the frame also
        // being the one that quits or deactivates.
        {"wheel-once", {MouseWheelEvent(1.0f)}},
        {"wheel-three-times-in-one-frame",
         {MouseWheelEvent(1.0f), MouseWheelEvent(1.0f), MouseWheelEvent(1.0f)}},
        {"wheel-then-quit-in-one-frame", {MouseWheelEvent(1.0f), QuitEvent()}},
        {"quit-then-wheel-in-one-frame", {QuitEvent(), MouseWheelEvent(1.0f)}},
    };

    const std::string transcript = BuildTranscript(game, ownWindow, scenarios);
    const std::filesystem::path goldenPath = FindGoldenPath();

    const char* update = std::getenv("CNA_UPDATE_EVENT_GOLDEN");
    if (update != nullptr && std::string(update) == "1")
    {
        std::filesystem::create_directories(goldenPath.parent_path());
        std::ofstream out(goldenPath, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(out.is_open()) << "cannot write " << goldenPath;
        out << transcript;
        out.close();
        GTEST_SKIP() << "regenerated " << goldenPath << " (CNA_UPDATE_EVENT_GOLDEN=1)";
    }

    std::ifstream in(goldenPath, std::ios::binary);
    ASSERT_TRUE(in.is_open())
        << "the PLAT-6 baseline transcript is missing at " << goldenPath
        << "\nRegenerate it with CNA_UPDATE_EVENT_GOLDEN=1 and review the result before "
           "committing.";
    std::ostringstream buffer;
    buffer << in.rdbuf();

    EXPECT_EQ(transcript, buffer.str())
        << "The runtime's observable event semantics changed.\n"
           "If that change is intended, regenerate the baseline with CNA_UPDATE_EVENT_GOLDEN=1 "
           "and explain the diff in the commit; if it is not, Phase 3 has altered behaviour "
           "it was only meant to re-plumb.";

    game.RunApplication = false;
}
