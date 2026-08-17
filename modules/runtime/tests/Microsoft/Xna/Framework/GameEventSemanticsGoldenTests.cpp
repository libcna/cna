// SPDX-License-Identifier: MS-PL
//
// PLAT-6/118: cross-implementation golden capture of the runtime's event semantics.
//
// This is an oracle, not a specification. What is captured is deliberately the observable
// behaviour -- state a game can read -- rather than a native event constant or an implementation
// mapper. The identical CNA PlatformEvent script enters a real Game::RunOneFrame() through every
// platform implementation compiled into the binary. A future implementation joins automatically.
//
// The checked-in transcript makes behavioural change deliberate: set
// CNA_UPDATE_EVENT_GOLDEN=1 to regenerate it, then review the resulting diff.
//
// Four findings remain load-bearing:
//
//   1. Resize payloads are ignored and the window is re-queried. A payload of 12345x6789 must not
//      become ClientBounds.
//   2. The loop does not filter by window id. Focus loss from an unrelated id still deactivates.
//   3. Minimize/maximize/restore/move/display changes do not alter any captured public state.
//   4. Exit() does not stop draining the event batch. A wheel event after quit still lands.

#include <gtest/gtest.h>

#include "CNA/Platform/CannedMouse.hpp"
#include "CNA/Platform/PlatformEvent.hpp"
#include "CNA/Platform/PlatformException.hpp"
#include "CNA/Platform/PlatformFactory.hpp"
#include "CNA/Platform/PlatformTestDecorator.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameWindow.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Input/Mouse.hpp"
#include "Microsoft/Xna/Framework/Input/MouseState.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace Microsoft::Xna::Framework;

namespace
{
    namespace Platform = CNA::Platform;

    /** A real named platform with only its event source and mouse snapshot scripted. */
    class TranscriptPlatform final : public Platform::Testing::PlatformTestDecorator
    {
    public:
        explicit TranscriptPlatform(std::unique_ptr<Platform::IPlatform> inner)
            : PlatformTestDecorator(std::move(inner))
        {
        }

        void Queue(const std::vector<Platform::PlatformEvent>& events)
        {
            for (const Platform::PlatformEvent& event : events)
            {
                if (const auto* wheel = std::get_if<Platform::MouseWheelEvent>(&event))
                {
                    mouseState_.scrollX += static_cast<int>(wheel->x) * 120;
                    mouseState_.scrollY += static_cast<int>(wheel->y) * 120;
                }
                queued_.push_back(event);
            }
            mouse_.SetPending(mouseState_);
        }

        void PollEvents(std::vector<Platform::PlatformEvent>& destination) override
        {
            destination.clear();
            destination.insert(destination.end(), queued_.begin(), queued_.end());
            queued_.clear();
        }

        [[nodiscard]] Platform::IPlatformMouse* GetMouse() override { return &mouse_; }

    private:
        Platform::Testing::CannedMouse mouse_;
        Platform::MouseSnapshot mouseState_;
        std::vector<Platform::PlatformEvent> queued_;
    };

    // Draws nothing and never exits on its own, so every observed transition is attributable to
    // the scripted events rather than to the game's own logic.
    class TranscriptGame final : public Game
    {
    public:
        explicit TranscriptGame(std::unique_ptr<Platform::IPlatform> platform)
            : Game(std::move(platform))
        {
        }

    protected:
        void LoadContent() override {}
        void Update(GameTime& gameTime) override { Game::Update(gameTime); }
        void Draw(const GameTime& gameTime) override { Game::Draw(gameTime); }
    };

    Platform::PlatformEvent QuitEvent()
    {
        return Platform::QuitEvent{};
    }

    Platform::PlatformEvent WindowEvent(
        const Platform::WindowEventKind kind, const Platform::WindowId window,
        const int data1 = 0, const int data2 = 0)
    {
        return Platform::WindowEvent{window, kind, data1, data2};
    }

    Platform::PlatformEvent AppLifecycleEvent(const Platform::AppLifecycleKind kind)
    {
        return Platform::AppLifecycleEvent{kind};
    }

    Platform::PlatformEvent MouseWheelEvent(const float y)
    {
        Platform::MouseWheelEvent event;
        event.y = y;
        return event;
    }

    Platform::PlatformEvent KeyDownEvent(const Platform::KeyCode key, const bool repeat)
    {
        Platform::KeyEvent event;
        event.keycode = key;
        event.pressed = true;
        event.repeat = repeat;
        return event;
    }

    struct Scenario
    {
        std::string name;
        std::vector<Platform::PlatformEvent> script;
        // Reached by replaying a focus event in an earlier frame rather than poking a private
        // field, so each scenario is a public transition from a stated starting point.
        bool startActive = true;
    };

    /// Everything a game can observe about the outcome, rendered as one transcript line.
    std::string Observe(Game& game, const Rectangle& boundsBaseline,
                        const int wheelBaseline)
    {
        const Rectangle bounds = game.getWindowProperty().getClientBoundsProperty();
        const bool boundsChanged =
            bounds.X != boundsBaseline.X || bounds.Y != boundsBaseline.Y
            || bounds.Width != boundsBaseline.Width || bounds.Height != boundsBaseline.Height;
        const int wheel =
            Input::Mouse::GetState().getScrollWheelValueProperty() - wheelBaseline;
        std::ostringstream out;
        out << "IsActive=" << (game.getIsActiveProperty() ? "true" : "false")
            << " RunApplication=" << (game.RunApplication ? "true" : "false")
            << " ClientBoundsChanged=" << (boundsChanged ? "true" : "false")
            << " WheelDelta=" << wheel;
        return out.str();
    }

    void RunFrameWith(TranscriptGame& game, TranscriptPlatform& platform,
                      const std::vector<Platform::PlatformEvent>& script)
    {
        platform.Queue(script);
        game.RunOneFrame();
    }

    std::string BuildTranscript(TranscriptGame& game, TranscriptPlatform& platform,
                                const Platform::WindowId ownWindow,
                                const std::vector<Scenario>& scenarios)
    {
        std::ostringstream out;
        out << "# PLAT-6/118 golden: observable runtime event semantics.\n"
               "# Generated by GameEventSemanticsGoldenTests; regenerate with "
               "CNA_UPDATE_EVENT_GOLDEN=1.\n"
               "# Each block: one platform-neutral event batch, then the state after its frame.\n";

        for (const Scenario& scenario : scenarios)
        {
            game.RunApplication = true;
            RunFrameWith(game, platform,
                         {WindowEvent(scenario.startActive
                                          ? Platform::WindowEventKind::FocusGained
                                          : Platform::WindowEventKind::FocusLost,
                                      ownWindow)});
            game.RunApplication = true;
            const Rectangle boundsBaseline =
                game.getWindowProperty().getClientBoundsProperty();
            const int wheelBaseline = Input::Mouse::GetState().getScrollWheelValueProperty();
            const std::string before = Observe(game, boundsBaseline, wheelBaseline);

            RunFrameWith(game, platform, scenario.script);

            out << "\n[" << scenario.name << "]\n"
                << "  before: " << before << "\n"
                << "  after:  " << Observe(game, boundsBaseline, wheelBaseline) << "\n";
        }

        return out.str();
    }

    std::filesystem::path FindGoldenPath()
    {
        const std::filesystem::path relative =
            "modules/runtime/tests/golden/platform-event-semantics.txt";
        std::filesystem::path base = std::filesystem::current_path();
        for (int depth = 0; depth < 4; ++depth)
        {
            if (std::filesystem::exists(base / relative)) return base / relative;
            if (!base.has_parent_path() || base.parent_path() == base) break;
            base = base.parent_path();
        }
        return std::filesystem::current_path() / relative;
    }

    class GameEventSemanticsGoldenTest : public ::testing::TestWithParam<std::string>
    {
    };
}

TEST_P(GameEventSemanticsGoldenTest, ObservableEventSemanticsMatchTheCapturedBaseline)
{
    auto platform = std::make_unique<TranscriptPlatform>(
        Platform::PlatformFactory::Create(GetParam()));
    TranscriptPlatform* scriptedPlatform = platform.get();

    // This suite is parameterised over every platform compiled into the binary, but a Game also
    // constructs a GraphicsDevice, and the renderer this build selected may need a service the
    // platform under test does not offer -- an OpenGL context, most commonly. That pairing is a
    // legitimate refusal, not an event-semantics difference, so there is nothing here to compare
    // against the golden transcript.
    std::unique_ptr<TranscriptGame> gameOwner;
    try
    {
        gameOwner = std::make_unique<TranscriptGame>(std::move(platform));
    }
    catch (const Platform::PlatformException& refusal)
    {
        GTEST_SKIP() << "the " << GetParam()
                     << " platform cannot back this build's renderer: " << refusal.what();
    }
    TranscriptGame& game = *gameOwner;
    GraphicsDeviceManager gdm(&game);

    // Settle construction and publish the scripted mouse's initial empty snapshot.
    ASSERT_NO_THROW(game.RunOneFrame());

    constexpr Platform::WindowId ownWindow = 1;
    constexpr Platform::WindowId foreignWindow = 1000;

    const std::vector<Scenario> scenarios = {
        {"quit", {QuitEvent()}},
        {"focus-lost", {WindowEvent(Platform::WindowEventKind::FocusLost, ownWindow)}},
        {"focus-gained-from-inactive",
         {WindowEvent(Platform::WindowEventKind::FocusGained, ownWindow)}, false},
        {"background", {AppLifecycleEvent(Platform::AppLifecycleKind::WillEnterBackground)}},
        {"foreground-from-inactive",
         {AppLifecycleEvent(Platform::AppLifecycleKind::DidEnterForeground)}, false},

        {"focus-lost-then-gained-in-one-frame",
         {WindowEvent(Platform::WindowEventKind::FocusLost, ownWindow),
          WindowEvent(Platform::WindowEventKind::FocusGained, ownWindow)}},
        {"focus-gained-then-lost-in-one-frame",
         {WindowEvent(Platform::WindowEventKind::FocusGained, ownWindow),
          WindowEvent(Platform::WindowEventKind::FocusLost, ownWindow)}},
        {"quit-then-focus-lost-in-one-frame",
         {QuitEvent(), WindowEvent(Platform::WindowEventKind::FocusLost, ownWindow)}},

        {"resize-with-bogus-payload",
         {WindowEvent(Platform::WindowEventKind::Resized, ownWindow, 12345, 6789)}},
        {"pixel-size-changed-with-bogus-payload",
         {WindowEvent(Platform::WindowEventKind::PixelSizeChanged,
                      ownWindow, 12345, 6789)}},
        {"focus-lost-from-a-different-window-id",
         {WindowEvent(Platform::WindowEventKind::FocusLost, foreignWindow)}},

        {"minimize", {WindowEvent(Platform::WindowEventKind::Minimized, ownWindow)}},
        {"restore", {WindowEvent(Platform::WindowEventKind::Restored, ownWindow)}},
        {"maximize", {WindowEvent(Platform::WindowEventKind::Maximized, ownWindow)}},
        {"display-scale-changed",
         {WindowEvent(Platform::WindowEventKind::DisplayScaleChanged, ownWindow)}},
        {"display-changed",
         {WindowEvent(Platform::WindowEventKind::DisplayChanged, ownWindow)}},
        {"window-exposed", {WindowEvent(Platform::WindowEventKind::Exposed, ownWindow)}},
        {"window-moved", {WindowEvent(Platform::WindowEventKind::Moved, ownWindow)}},
        {"window-close-requested",
         {WindowEvent(Platform::WindowEventKind::CloseRequested, ownWindow)}},

        {"debug-hotkey-f9", {KeyDownEvent(Platform::KeyCode::F9, false)}},
        {"debug-hotkey-f9-repeat", {KeyDownEvent(Platform::KeyCode::F9, true)}},
        {"ordinary-keypress", {KeyDownEvent(Platform::KeyCode::A, false)}},

        {"wheel-once", {MouseWheelEvent(1.0f)}},
        {"wheel-three-times-in-one-frame",
         {MouseWheelEvent(1.0f), MouseWheelEvent(1.0f), MouseWheelEvent(1.0f)}},
        {"wheel-then-quit-in-one-frame", {MouseWheelEvent(1.0f), QuitEvent()}},
        {"quit-then-wheel-in-one-frame", {QuitEvent(), MouseWheelEvent(1.0f)}},
    };

    const std::string transcript =
        BuildTranscript(game, *scriptedPlatform, ownWindow, scenarios);
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
        << "the event-semantics transcript is missing at " << goldenPath
        << "\nRegenerate it with CNA_UPDATE_EVENT_GOLDEN=1 and review the result.";
    std::ostringstream buffer;
    buffer << in.rdbuf();

    EXPECT_EQ(transcript, buffer.str())
        << "The runtime's observable event semantics differ on platform " << GetParam()
        << ". If intended, regenerate and explain the golden diff; otherwise the "
           "implementation has drifted.";

    game.RunApplication = false;
}

INSTANTIATE_TEST_SUITE_P(
    EveryImplementation,
    GameEventSemanticsGoldenTest,
    ::testing::ValuesIn(Platform::PlatformFactory::GetAvailable()),
    [](const ::testing::TestParamInfo<std::string>& info) { return info.param; });
