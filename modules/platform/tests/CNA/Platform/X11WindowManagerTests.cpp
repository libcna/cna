// SPDX-License-Identifier: MS-PL
//
// plans/plan_x11.md X11-0102: the EWMH/ICCCM half of the backend, against a real window manager.
//
// These behaviours are separated from X11PlatformIntegrationTests.cpp because they are not the
// X server's to perform. Maximise, minimise, restore and fullscreen are window-manager services:
// under a bare `Xvfb` there is no window manager, the client messages go to a root window nobody
// is listening on, and nothing happens -- correctly. Asserting them there would test the
// environment, not the backend.
//
// So this suite starts one. `openbox` is a small, fast, standards-compliant EWMH window manager;
// when it is absent the suite skips with that recorded, and the plan records the gap rather than
// the absence being silently equivalent to a pass.
//
// A display that ALREADY has an EWMH window manager -- a developer's desktop session, where
// mutter, KWin or xfwm4 manages the windows -- is used as it is, and nothing is started at all
// (plans/plan_native_platform_validation.md NPV-0107). The first version always ran
// `openbox --replace`, which on a real desktop would evict the session's own window manager: the
// gtest-discovered copies of this suite inherit the shell's DISPLAY, so a plain `ctest` from a
// desktop terminal did exactly that. Running against the session's window manager is also the
// stronger test -- it is the one the application will actually meet.

#include <gtest/gtest.h>

#include "../../../src/X11/X11Headers.hpp"
#include "X11TestWindowManager.hpp"

#include "CNA/Platform/PlatformException.hpp"
#include "CNA/Platform/PlatformFactory.hpp"

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <variant>
#include <vector>

namespace {

using namespace CNA::Platform;

// `<X11/X.h>` declares `typedef unsigned char KeyCode` at global scope; these declarations hide it
// behind CNA's own types, exactly as X11PlatformIntegrationTests.cpp does.
using KeyCode = CNA::Platform::KeyCode;
using Scancode = CNA::Platform::Scancode;

using namespace CNA::Platform::X11::Testing;

class X11WithWindowManager : public ::testing::Test
{
protected:
    void SetUp() override
    {
        if (!HasDisplay())
        {
            GTEST_SKIP() << "no DISPLAY";
        }
        if (!ReadEwmhState().windowManager && !HasWindowManagerBinary())
        {
            GTEST_SKIP() << "no window manager runs on this display and openbox is not installed; "
                            "EWMH state transitions have nothing to perform them and asserting "
                            "them here would test the environment";
        }

        ASSERT_TRUE(SharedWindowManager::Instance().Available());

        platform_ = PlatformFactory::Create("X11");
        try
        {
            platform_->AcquireSubsystem(PlatformSubsystem::Video);
        }
        catch (const PlatformException& error)
        {
            GTEST_SKIP() << "cannot reach the X server: " << error.what();
        }
        acquired_ = true;

        // The window manager takes a moment to claim _NET_SUPPORTING_WM_CHECK, and CNA reads that
        // when the connection opens. Waiting for the capability rather than for a duration is
        // what makes this deterministic on a slow machine.
        if (!WaitForWindowManager())
        {
            // Three different situations, and only one of them is the environment's fault. A
            // skip that hid the third is how this suite once reported green while 14 of its 15
            // tests had run nothing (a broken openbox install exited at start-up).
            const std::string exitReason = SharedWindowManager::Instance().ExitReason();
            if (!exitReason.empty())
            {
                GTEST_SKIP() << exitReason
                             << " before becoming the window manager; check its installation "
                                "(config and theme files)";
            }
            const EwmhState server = ReadEwmhState();
            if (!server.windowManager || !server.advertisesFullscreen)
            {
                GTEST_SKIP() << (server.windowManager
                                     ? "the window manager does not advertise "
                                       "_NET_WM_STATE_FULLSCREEN"
                                     : "no EWMH window manager became ready in time");
            }
            FAIL() << "the X server shows an EWMH window manager advertising "
                      "_NET_WM_STATE_FULLSCREEN, but the backend did not report "
                      "borderlessFullscreen";
        }
    }

    void TearDown() override
    {
        window_.reset();
        if (acquired_)
        {
            platform_->ReleaseSubsystem(PlatformSubsystem::Video);
        }
        platform_.reset();
        // The window manager is deliberately NOT stopped here: it is shared by the whole suite.
    }

    bool WaitForWindowManager()
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        std::vector<PlatformEvent> batch;
        while (std::chrono::steady_clock::now() < deadline)
        {
            platform_->PollEvents(batch);
            // The capability set is settled at construction, so a platform created before the
            // window manager was up will not see it. Re-creating is the honest way to observe
            // the state the backend would have in a normal session.
            if (platform_->GetCapabilities().borderlessFullscreen)
            {
                return true;
            }
            platform_->ReleaseSubsystem(PlatformSubsystem::Video);
            platform_ = PlatformFactory::Create("X11");
            try
            {
                platform_->AcquireSubsystem(PlatformSubsystem::Video);
            }
            catch (const PlatformException&)
            {
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        return false;
    }

    std::unique_ptr<IPlatformWindow> MakeVisibleWindow(const std::string& title = "CNA WM test")
    {
        WindowDescription description;
        description.title = title;
        description.width = 320;
        description.height = 240;
        description.centered = true;
        auto window = platform_->CreateWindow(description);
        const WindowId id = window->GetId();
        window->Show();
        // Wait for the window manager to finish reparenting and mapping before asking it for a
        // state change; a request sent during that window is legitimately ignored. The signal is
        // the window's own Restored event, which MapNotify produces.
        PumpUntil([id](const std::vector<PlatformEvent>& events) {
            return std::any_of(events.begin(), events.end(), [id](const PlatformEvent& event) {
                const auto* windowEvent = std::get_if<WindowEvent>(&event);
                return windowEvent != nullptr && windowEvent->window == id &&
                       (windowEvent->kind == WindowEventKind::Restored ||
                        windowEvent->kind == WindowEventKind::Exposed);
            });
        }, std::chrono::milliseconds(2000));
        // `seen_` is deliberately NOT cleared here. A window manager focuses a window as it maps
        // it, so the FocusGained event arrives during this wait -- and a test asserting on it
        // would never see it again.
        return window;
    }

    bool PumpUntil(const std::function<bool(const std::vector<PlatformEvent>&)>& predicate,
                   const std::chrono::milliseconds budget = std::chrono::milliseconds(3000))
    {
        const auto deadline = std::chrono::steady_clock::now() + budget;
        std::vector<PlatformEvent> batch;
        while (std::chrono::steady_clock::now() < deadline)
        {
            platform_->PollEvents(batch);
            seen_.insert(seen_.end(), batch.begin(), batch.end());
            if (predicate(seen_))
            {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return false;
    }

    std::unique_ptr<IPlatform> platform_;
    std::unique_ptr<IPlatformWindow> window_;
    std::vector<PlatformEvent> seen_;
    bool acquired_ = false;
};

TEST_F(X11WithWindowManager, AnEwmhWindowManagerIsDetectedAndUnlocksBorderlessFullscreen)
{
    // The capability is not "X11 can do fullscreen" -- it is "the window manager running right
    // now advertises _NET_WM_STATE_FULLSCREEN". Under bare Xvfb it is false, and that difference
    // is the whole reason this suite exists.
    EXPECT_TRUE(platform_->GetCapabilities().borderlessFullscreen);
}

TEST_F(X11WithWindowManager, BorderlessFullscreenIsEnteredAndLeft)
{
    window_ = MakeVisibleWindow();
    ASSERT_EQ(window_->GetFullscreenMode(), WindowFullscreenMode::Windowed);

    window_->SetFullscreenMode(WindowFullscreenMode::BorderlessFullscreen);
    const bool entered = PumpUntil([this](const std::vector<PlatformEvent>&) {
        return window_->GetFullscreenMode() == WindowFullscreenMode::BorderlessFullscreen;
    });
    EXPECT_TRUE(entered) << "the window manager did not apply _NET_WM_STATE_FULLSCREEN";

    window_->SetFullscreenMode(WindowFullscreenMode::Windowed);
    const bool left = PumpUntil([this](const std::vector<PlatformEvent>&) {
        return window_->GetFullscreenMode() == WindowFullscreenMode::Windowed;
    });
    EXPECT_TRUE(left) << "the window did not return to windowed mode";
}

TEST_F(X11WithWindowManager, FullscreenAskedForBeforeTheFirstEventPumpIsApplied)
{
    // What a game does: its window is created visible and fullscreen is asked for while the game
    // is still setting up -- before it has pumped a single event, so before the backend has seen
    // the window's MapNotify. The window manager has mapped the window by then, and a
    // _NET_WM_STATE written straight onto a managed window is ignored; only the client message is
    // honoured (EWMH). The backend chose between the two on "MapNotify seen" and wrote the
    // property: the window stayed windowed. Found by the XNA end-to-end test of IsFullScreen
    // (plans/plan_x11.md X11-0153).
    WindowDescription description;
    description.title = "CNA WM test (fullscreen before the first pump)";
    description.width = 320;
    description.height = 240;
    window_ = platform_->CreateWindow(description);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Asserted on the geometry, not on GetFullscreenMode(): that reads _NET_WM_STATE back, and a
    // property the backend wrote itself reads back fullscreen whether or not the window manager
    // ever acted on it -- which is how this passed unnoticed.
    ::Display* observer = XOpenDisplay(nullptr);
    ASSERT_NE(observer, nullptr);
    const int screenWidth = DisplayWidth(observer, DefaultScreen(observer));
    const int screenHeight = DisplayHeight(observer, DefaultScreen(observer));
    XCloseDisplay(observer);

    window_->SetFullscreenMode(WindowFullscreenMode::BorderlessFullscreen);
    EXPECT_TRUE(PumpUntil([this, screenWidth, screenHeight](const std::vector<PlatformEvent>&) {
        const WindowBounds bounds = window_->GetClientBounds();
        return bounds.x == 0 && bounds.y == 0 && bounds.width == screenWidth &&
               bounds.height == screenHeight;
    })) << "the window manager never made the window fullscreen; it is "
        << window_->GetClientBounds().width << "x" << window_->GetClientBounds().height << "+"
        << window_->GetClientBounds().x << "+" << window_->GetClientBounds().y;
}

TEST_F(X11WithWindowManager, RepeatedFullscreenTransitionsAreStable)
{
    // A single transition can pass by accident; the failure mode this catches is state that
    // accumulates -- a property written twice, an un-maximise that does not undo a maximise.
    window_ = MakeVisibleWindow();
    for (int round = 0; round < 3; ++round)
    {
        window_->SetFullscreenMode(WindowFullscreenMode::BorderlessFullscreen);
        ASSERT_TRUE(PumpUntil([this](const std::vector<PlatformEvent>&) {
            return window_->GetFullscreenMode() == WindowFullscreenMode::BorderlessFullscreen;
        })) << "round " << round;

        window_->SetFullscreenMode(WindowFullscreenMode::Windowed);
        ASSERT_TRUE(PumpUntil([this](const std::vector<PlatformEvent>&) {
            return window_->GetFullscreenMode() == WindowFullscreenMode::Windowed;
        })) << "round " << round;
    }
}

TEST_F(X11WithWindowManager, MinimizeAndRestoreRoundTrip)
{
    window_ = MakeVisibleWindow();
    ASSERT_FALSE(window_->IsMinimized());

    window_->Minimize();
    const bool minimized = PumpUntil([this](const std::vector<PlatformEvent>&) {
        return window_->IsMinimized();
    });
    EXPECT_TRUE(minimized) << "XIconifyWindow did not reach WM_STATE=IconicState";

    window_->Restore();
    const bool restored = PumpUntil([this](const std::vector<PlatformEvent>&) {
        return !window_->IsMinimized();
    });
    EXPECT_TRUE(restored) << "the window did not come back from iconic state";
}

TEST_F(X11WithWindowManager, MinimizingReportsAMinimizedEventAndRestoringReportsARestoredOne)
{
    window_ = MakeVisibleWindow();
    const WindowId id = window_->GetId();
    seen_.clear();

    window_->Minimize();
    const bool sawMinimized = PumpUntil([id](const std::vector<PlatformEvent>& events) {
        return std::any_of(events.begin(), events.end(), [id](const PlatformEvent& event) {
            const auto* window = std::get_if<WindowEvent>(&event);
            return window != nullptr && window->window == id &&
                   window->kind == WindowEventKind::Minimized;
        });
    });
    EXPECT_TRUE(sawMinimized);

    seen_.clear();
    window_->Restore();
    const bool sawRestored = PumpUntil([id](const std::vector<PlatformEvent>& events) {
        return std::any_of(events.begin(), events.end(), [id](const PlatformEvent& event) {
            const auto* window = std::get_if<WindowEvent>(&event);
            return window != nullptr && window->window == id &&
                   window->kind == WindowEventKind::Restored;
        });
    });
    EXPECT_TRUE(sawRestored);
}

TEST_F(X11WithWindowManager, MaximizeAndRestoreChangeTheWindowSize)
{
    window_ = MakeVisibleWindow();
    const WindowBounds before = window_->GetClientBounds();

    window_->Maximize();
    const bool grew = PumpUntil([this, before](const std::vector<PlatformEvent>&) {
        const WindowBounds now = window_->GetClientBounds();
        return now.width > before.width || now.height > before.height;
    });
    EXPECT_TRUE(grew) << "maximising did not enlarge the window";

    window_->Restore();
    const bool shrank = PumpUntil([this, before](const std::vector<PlatformEvent>&) {
        const WindowBounds now = window_->GetClientBounds();
        return now.width == before.width && now.height == before.height;
    });
    EXPECT_TRUE(shrank) << "restoring did not return the window to its former size";
}

TEST_F(X11WithWindowManager, MaximizingReportsAMaximizedEventAndRestoringReportsARestoredOne)
{
    // The event half of maximise, which is separate from the size half above and was genuinely
    // missing: the first implementation computed a Maximized event and then only ever pushed the
    // Minimized one, so a game reacting to maximise never heard. A test that checked only the
    // resulting window size would have passed throughout.
    window_ = MakeVisibleWindow();
    const WindowId id = window_->GetId();
    seen_.clear();

    window_->Maximize();
    const bool sawMaximized = PumpUntil([id](const std::vector<PlatformEvent>& events) {
        return std::any_of(events.begin(), events.end(), [id](const PlatformEvent& event) {
            const auto* window = std::get_if<WindowEvent>(&event);
            return window != nullptr && window->window == id &&
                   window->kind == WindowEventKind::Maximized;
        });
    });
    EXPECT_TRUE(sawMaximized);

    seen_.clear();
    window_->Restore();
    const bool sawRestored = PumpUntil([id](const std::vector<PlatformEvent>& events) {
        return std::any_of(events.begin(), events.end(), [id](const PlatformEvent& event) {
            const auto* window = std::get_if<WindowEvent>(&event);
            return window != nullptr && window->window == id &&
                   window->kind == WindowEventKind::Restored;
        });
    });
    EXPECT_TRUE(sawRestored);
}

TEST_F(X11WithWindowManager, AStateChangeThatChangesNothingProducesNoEvent)
{
    // `_NET_WM_STATE` changes for reasons a game has no interest in, and the same user action
    // also arrives as MapNotify/UnmapNotify. Deriving events from the DIFFERENCE rather than from
    // the property change is what keeps one user action from producing three events.
    window_ = MakeVisibleWindow();
    const WindowId id = window_->GetId();

    window_->Maximize();
    PumpUntil([id](const std::vector<PlatformEvent>& events) {
        return std::any_of(events.begin(), events.end(), [id](const PlatformEvent& event) {
            const auto* window = std::get_if<WindowEvent>(&event);
            return window != nullptr && window->window == id &&
                   window->kind == WindowEventKind::Maximized;
        });
    });

    seen_.clear();
    // Asking for a state the window is already in.
    window_->Maximize();
    PumpUntil([](const std::vector<PlatformEvent>&) { return false; },
              std::chrono::milliseconds(600));

    const int stateEvents =
        static_cast<int>(std::count_if(seen_.begin(), seen_.end(), [id](const PlatformEvent& e) {
            const auto* window = std::get_if<WindowEvent>(&e);
            return window != nullptr && window->window == id &&
                   (window->kind == WindowEventKind::Maximized ||
                    window->kind == WindowEventKind::Minimized ||
                    window->kind == WindowEventKind::Restored);
        }));
    EXPECT_EQ(stateEvents, 0) << "re-requesting a state the window already has must be silent";
}

TEST_F(X11WithWindowManager, AFocusedWindowReportsFocusAndTheEventThatMatchesIt)
{
    window_ = MakeVisibleWindow();
    const WindowId id = window_->GetId();

    const bool focused = PumpUntil([this, id](const std::vector<PlatformEvent>& events) {
        const bool sawEvent =
            std::any_of(events.begin(), events.end(), [id](const PlatformEvent& event) {
                const auto* window = std::get_if<WindowEvent>(&event);
                return window != nullptr && window->window == id &&
                       window->kind == WindowEventKind::FocusGained;
            });
        return sawEvent && window_->HasFocus();
    });
    EXPECT_TRUE(focused) << "a newly mapped window under a window manager should take focus";
}

TEST_F(X11WithWindowManager, ASecondWindowTakesFocusFromTheFirstAndBothStayAlive)
{
    auto first = MakeVisibleWindow("first");
    const WindowId firstId = first->GetId();
    PumpUntil([&first](const std::vector<PlatformEvent>&) { return first->HasFocus(); });

    auto second = MakeVisibleWindow("second");
    const bool switched = PumpUntil([this, firstId, &first, &second](
                                        const std::vector<PlatformEvent>& events) {
        const bool sawLost =
            std::any_of(events.begin(), events.end(), [firstId](const PlatformEvent& event) {
                const auto* window = std::get_if<WindowEvent>(&event);
                return window != nullptr && window->window == firstId &&
                       window->kind == WindowEventKind::FocusLost;
            });
        return sawLost || (second->HasFocus() && !first->HasFocus());
    });
    EXPECT_TRUE(switched) << "focus did not move to the second window";

    // Both windows are still usable: focus moving is not a lifetime event.
    EXPECT_EQ(first->GetTitle(), "first");
    EXPECT_EQ(second->GetTitle(), "second");
}

/// Asks the window manager to close a window the way its title-bar button does: EWMH
/// `_NET_CLOSE_WINDOW` to the root, after which the window manager sends the window
/// `WM_DELETE_WINDOW`. Source indication 2 ("pager") is the one a window manager honours
/// unconditionally, which is what a test needs.
bool AskWindowManagerToClose(const std::uintptr_t xid)
{
    using CNA::Platform::X11::kCurrentTime;
    using CNA::Platform::X11::kXFalse;

    ::Display* display = XOpenDisplay(nullptr);
    if (display == nullptr)
    {
        return false;
    }
    XEvent event{};
    event.type = ClientMessage;
    event.xclient.window = static_cast<::Window>(xid);
    event.xclient.message_type = XInternAtom(display, "_NET_CLOSE_WINDOW", kXFalse);
    event.xclient.format = 32;
    event.xclient.data.l[0] = static_cast<long>(kCurrentTime);
    event.xclient.data.l[1] = 2;
    const bool sent = XSendEvent(display, DefaultRootWindow(display), kXFalse,
                                 SubstructureRedirectMask | SubstructureNotifyMask, &event) != 0;
    XSync(display, kXFalse);
    XCloseDisplay(display);
    return sent;
}

TEST_F(X11WithWindowManager, ClosingASecondaryWindowIsNotApplicationTermination)
{
    // The behaviour that separates a multi-window application from a single-window one. A window
    // manager's close button sends WM_DELETE_WINDOW to one window, and the application decides.
    auto first = MakeVisibleWindow("first");
    auto second = MakeVisibleWindow("second");
    const WindowId secondId = second->GetId();
    seen_.clear();

    // Through the window manager, as its title-bar button does. The first version of this test
    // used `xdotool windowclose`, which is XDestroyWindow from outside -- a different path, and
    // not the protocol the comment above describes.
    ASSERT_TRUE(AskWindowManagerToClose(second->GetWindowHandle()));
    const bool requested = PumpUntil([secondId](const std::vector<PlatformEvent>& events) {
        return std::any_of(events.begin(), events.end(), [secondId](const PlatformEvent& event) {
            const auto* window = std::get_if<WindowEvent>(&event);
            return window != nullptr && window->window == secondId &&
                   window->kind == WindowEventKind::CloseRequested;
        });
    });
    EXPECT_TRUE(requested) << "the window manager's close request never reached the window";

    // The window is still there -- the application answers -- and there is no QuitEvent: a
    // second window is still open.
    PumpUntil([](const std::vector<PlatformEvent>&) { return false; },
              std::chrono::milliseconds(300));
    const bool quit = std::any_of(seen_.begin(), seen_.end(), [](const PlatformEvent& event) {
        return std::holds_alternative<QuitEvent>(event);
    });
    EXPECT_FALSE(quit) << "closing one of two windows must not ask the application to quit";
    EXPECT_EQ(first->GetTitle(), "first");
    EXPECT_EQ(second->GetTitle(), "second");
}

TEST_F(X11WithWindowManager, AWindowDestroyedByAnotherClientIsNotApplicationTermination)
{
    // The other way a window disappears: another client destroys it outright (`xdotool
    // windowclose` is XDestroyWindow). The application did not ask, so nothing may end it, and
    // the wrapper it still holds must survive being destroyed afterwards.
    if (std::system("command -v xdotool >/dev/null 2>&1") != 0)
    {
        GTEST_SKIP() << "xdotool is not installed, so there is no other client to destroy a window";
    }
    auto first = MakeVisibleWindow("first");
    auto second = MakeVisibleWindow("second");
    seen_.clear();

    const std::string command =
        "xdotool windowclose " + std::to_string(second->GetWindowHandle()) + " >/dev/null 2>&1";
    (void) std::system(command.c_str());

    PumpUntil([](const std::vector<PlatformEvent>&) { return false; },
              std::chrono::milliseconds(1000));
    const bool quit = std::any_of(seen_.begin(), seen_.end(), [](const PlatformEvent& event) {
        return std::holds_alternative<QuitEvent>(event);
    });
    EXPECT_FALSE(quit) << "a window destroyed from outside must not ask the application to quit";
    second.reset();
    EXPECT_EQ(first->GetTitle(), "first");
}

// --- injected input ---------------------------------------------------------------------------
//
// These need the window manager, not because input does, but because they need the window to have
// keyboard FOCUS -- and on a bare Xvfb nothing gives it any. `xdotool key --window` sends a
// synthetic event to a specific window and bypasses focus, which is convenient and proves less:
// it does not exercise the focus tracking, the XIM context, or the state the real path carries.
// So focus comes from the window manager and the key goes to whatever has it.

TEST_F(X11WithWindowManager, AnInjectedKeyArrivesWithBothItsPhysicalAndItsLogicalIdentity)
{
    if (std::system("command -v xdotool >/dev/null 2>&1") != 0)
    {
        GTEST_SKIP() << "xdotool is not installed, so there is no way to inject a key";
    }
    window_ = MakeVisibleWindow("CNA key input");
    const WindowId id = window_->GetId();
    ASSERT_TRUE(PumpUntil([this](const std::vector<PlatformEvent>&) { return window_->HasFocus(); }))
        << "the window never took focus, so an injected key would go elsewhere";
    seen_.clear();

    (void) std::system("xdotool key --clearmodifiers a >/dev/null 2>&1");

    const bool sawPress = PumpUntil([id](const std::vector<PlatformEvent>& events) {
        return std::any_of(events.begin(), events.end(), [id](const PlatformEvent& event) {
            const auto* key = std::get_if<KeyEvent>(&event);
            return key != nullptr && key->window == id && key->pressed;
        });
    });
    ASSERT_TRUE(sawPress) << "no key press arrived";

    const KeyEvent* press = nullptr;
    const KeyEvent* release = nullptr;
    for (const PlatformEvent& event : seen_)
    {
        if (const auto* key = std::get_if<KeyEvent>(&event))
        {
            if (key->pressed && press == nullptr) { press = key; }
            if (!key->pressed) { release = key; }
        }
    }
    ASSERT_NE(press, nullptr);

    // The two identities the contract keeps separate, both present and both correct. On a US
    // layout the A position produces `a`; on another layout the scancode would be the same and
    // the key code would differ, which is exactly the distinction being asserted.
    EXPECT_EQ(press->scancode, Scancode::A) << "the physical key is the A position";
    EXPECT_EQ(press->keycode, KeyCode::A) << "the layout maps that position to A";
    EXPECT_FALSE(press->repeat) << "a single keystroke is not auto-repeat";

    // And a real release, which is what `exactKeyboardState` promises. A backend that synthesised
    // releases from a timeout would still pass the press assertions above.
    const bool sawRelease = release != nullptr || PumpUntil([id](const std::vector<PlatformEvent>& events) {
        return std::any_of(events.begin(), events.end(), [id](const PlatformEvent& event) {
            const auto* key = std::get_if<KeyEvent>(&event);
            return key != nullptr && key->window == id && !key->pressed;
        });
    });
    EXPECT_TRUE(sawRelease) << "every press must have a matching release";
}

TEST_F(X11WithWindowManager, AnInjectedKeyProducesCommittedTextOnlyWhileTextInputIsActive)
{
    if (std::system("command -v xdotool >/dev/null 2>&1") != 0)
    {
        GTEST_SKIP() << "xdotool is not installed";
    }
    window_ = MakeVisibleWindow("CNA text input");
    const WindowId id = window_->GetId();
    ASSERT_TRUE(PumpUntil([this](const std::vector<PlatformEvent>&) { return window_->HasFocus(); }));

    IPlatformTextInput* text = platform_->GetTextInput();
    ASSERT_NE(text, nullptr);

    // Text input off: the key event arrives, the text does not. A backend that always produced
    // text would type into a game that never asked for it.
    seen_.clear();
    (void) std::system("xdotool key --clearmodifiers x >/dev/null 2>&1");
    PumpUntil([id](const std::vector<PlatformEvent>& events) {
        return std::any_of(events.begin(), events.end(), [id](const PlatformEvent& event) {
            const auto* key = std::get_if<KeyEvent>(&event);
            return key != nullptr && key->window == id && key->pressed;
        });
    });
    const bool textWhileOff = std::any_of(seen_.begin(), seen_.end(), [](const PlatformEvent& e) {
        return std::holds_alternative<TextInputEvent>(e);
    });
    EXPECT_FALSE(textWhileOff) << "text arrived while text input was not started";

    // Text input on: the committed characters arrive as well.
    text->Start(id, TextInputType::Text);
    seen_.clear();
    (void) std::system("xdotool key --clearmodifiers x >/dev/null 2>&1");
    const bool textWhileOn = PumpUntil([](const std::vector<PlatformEvent>& events) {
        return std::any_of(events.begin(), events.end(), [](const PlatformEvent& event) {
            const auto* input = std::get_if<TextInputEvent>(&event);
            return input != nullptr && input->text == "x";
        });
    });
    EXPECT_TRUE(textWhileOn) << "no committed text arrived while text input was active";
    text->Stop(id);
}

TEST_F(X11WithWindowManager, AnInjectedClickArrivesAsAButtonEventWithCoordinates)
{
    if (std::system("command -v xdotool >/dev/null 2>&1") != 0)
    {
        GTEST_SKIP() << "xdotool is not installed";
    }
    window_ = MakeVisibleWindow("CNA mouse input");
    const WindowId id = window_->GetId();
    PumpUntil([this](const std::vector<PlatformEvent>&) { return window_->HasFocus(); });
    window_->Sync();

    const WindowBounds bounds = window_->GetClientBounds();
    const int targetX = bounds.x + bounds.width / 2;
    const int targetY = bounds.y + bounds.height / 2;
    seen_.clear();

    const std::string move =
        "xdotool mousemove " + std::to_string(targetX) + " " + std::to_string(targetY) +
        " >/dev/null 2>&1";
    (void) std::system(move.c_str());
    (void) std::system("xdotool click 1 >/dev/null 2>&1");

    const bool sawPress = PumpUntil([id](const std::vector<PlatformEvent>& events) {
        return std::any_of(events.begin(), events.end(), [id](const PlatformEvent& event) {
            const auto* button = std::get_if<MouseButtonEvent>(&event);
            return button != nullptr && button->window == id && button->pressed;
        });
    });
    ASSERT_TRUE(sawPress) << "no button press arrived at the window under the pointer";

    const MouseButtonEvent* press = nullptr;
    bool sawRelease = false;
    for (const PlatformEvent& event : seen_)
    {
        if (const auto* button = std::get_if<MouseButtonEvent>(&event))
        {
            if (button->pressed && press == nullptr) { press = button; }
            if (!button->pressed) { sawRelease = true; }
        }
    }
    ASSERT_NE(press, nullptr);
    EXPECT_EQ(press->button, 1) << "the left button is CNA button 1";
    EXPECT_EQ(press->clicks, 1) << "one click is not a double click";
    // Client coordinates, roughly the window's centre. Exactness is the window manager's to
    // decide -- it owns the frame -- so this asserts the coordinates are inside the window rather
    // than an arbitrary pixel.
    EXPECT_GE(press->x, 0.0f);
    EXPECT_GE(press->y, 0.0f);
    EXPECT_LE(press->x, static_cast<float>(bounds.width));
    EXPECT_LE(press->y, static_cast<float>(bounds.height));
    EXPECT_TRUE(sawRelease) << "every button press must have a matching release";
}

// --- relative mouse and focus -----------------------------------------------------------------
//
// plans/plan_native_platform_validation.md NPV-0109 and NPV-0110. Relative mode is a state the
// application wants; the confining grab behind it may be held only while the window has focus.
// Both halves need a window manager: without one nothing is focused or refocused.

/// A window belonging to a different X client, which a window manager can give focus to.
class OtherClientWindow
{
public:
    OtherClientWindow()
    {
        display_ = XOpenDisplay(nullptr);
        if (display_ == nullptr)
        {
            return;
        }
        const int screen = DefaultScreen(display_);
        window_ = XCreateSimpleWindow(display_, DefaultRootWindow(display_), 400, 60, 200, 150, 0,
                                      BlackPixel(display_, screen), WhitePixel(display_, screen));
        XMapWindow(display_, window_);
        XSync(display_, CNA::Platform::X11::kXFalse);
    }

    ~OtherClientWindow()
    {
        if (display_ != nullptr)
        {
            XCloseDisplay(display_);
        }
    }

    OtherClientWindow(const OtherClientWindow&) = delete;
    OtherClientWindow& operator=(const OtherClientWindow&) = delete;

    [[nodiscard]] bool Ok() const { return display_ != nullptr; }

    /// Asks the window manager to activate a window, as a pager or a click would.
    void Activate(const ::Window window) const
    {
        XEvent event{};
        event.type = ClientMessage;
        event.xclient.window = window;
        event.xclient.message_type =
            XInternAtom(display_, "_NET_ACTIVE_WINDOW", CNA::Platform::X11::kXFalse);
        event.xclient.format = 32;
        event.xclient.data.l[0] = 2;
        XSendEvent(display_, DefaultRootWindow(display_), CNA::Platform::X11::kXFalse,
                   SubstructureRedirectMask | SubstructureNotifyMask, &event);
        XSync(display_, CNA::Platform::X11::kXFalse);
    }

    void ActivateSelf() const { Activate(window_); }

    /// True when some OTHER client holds the pointer grab: this client's own grab is refused.
    [[nodiscard]] bool PointerGrabbedElsewhere() const
    {
        const int status = XGrabPointer(display_, DefaultRootWindow(display_),
                                        CNA::Platform::X11::kXFalse, ButtonPressMask,
                                        GrabModeAsync, GrabModeAsync, CNA::Platform::X11::kNone,
                                        CNA::Platform::X11::kNone,
                                        CNA::Platform::X11::kCurrentTime);
        if (status == GrabSuccess)
        {
            XUngrabPointer(display_, CNA::Platform::X11::kCurrentTime);
        }
        XSync(display_, CNA::Platform::X11::kXFalse);
        return status == AlreadyGrabbed;
    }

private:
    ::Display* display_ = nullptr;
    ::Window window_ = CNA::Platform::X11::kNone;
};

TEST_F(X11WithWindowManager, RelativeModeRequestedRightAfterShowEngagesOnceTheWindowHasFocus)
{
    // The first thing a game does: show its window and lock the pointer. At that instant the
    // window manager has not yet made the window viewable, so an immediate grab fails with
    // GrabNotViewable -- which the first version reported as "another client holds the pointer
    // grab" and threw.
    if (!platform_->GetCapabilities().relativeMouse)
    {
        GTEST_SKIP() << "no XInput2 on this server";
    }
    OtherClientWindow other;
    ASSERT_TRUE(other.Ok());
    IPlatformMouse* mouse = platform_->GetMouse();
    ASSERT_NE(mouse, nullptr);

    WindowDescription description;
    description.title = "CNA relative right after Show";
    description.width = 320;
    description.height = 240;
    window_ = platform_->CreateWindow(description);
    window_->Show();
    ASSERT_NO_THROW(mouse->SetRelativeMode(window_->GetId(), true));
    EXPECT_TRUE(mouse->IsRelativeMode());

    const bool engaged = PumpUntil([this, &other](const std::vector<PlatformEvent>&) {
        return window_->HasFocus() && other.PointerGrabbedElsewhere();
    });
    EXPECT_TRUE(engaged) << "the grab never engaged once the window was mapped and focused";

    mouse->SetRelativeMode(window_->GetId(), false);
    EXPECT_FALSE(mouse->IsRelativeMode());
    EXPECT_FALSE(other.PointerGrabbedElsewhere()) << "disabling relative mode must free the pointer";
}

TEST_F(X11WithWindowManager, RelativeModeLetsGoOfThePointerWhileAnotherWindowHasFocus)
{
    // Alt-Tab while a game has the pointer locked. Keeping the confining grab would leave the
    // user's pointer locked to a window they are no longer using -- on a native X server, their
    // whole desktop.
    if (!platform_->GetCapabilities().relativeMouse)
    {
        GTEST_SKIP() << "no XInput2 on this server";
    }
    OtherClientWindow other;
    ASSERT_TRUE(other.Ok());
    IPlatformMouse* mouse = platform_->GetMouse();
    ASSERT_NE(mouse, nullptr);

    window_ = MakeVisibleWindow("CNA relative focus");
    ASSERT_TRUE(PumpUntil([this, &other](const std::vector<PlatformEvent>&) {
        other.Activate(static_cast<::Window>(window_->GetWindowHandle()));
        return window_->HasFocus();
    }));
    mouse->SetRelativeMode(window_->GetId(), true);
    ASSERT_TRUE(PumpUntil([&other](const std::vector<PlatformEvent>&) {
        return other.PointerGrabbedElsewhere();
    })) << "relative mode on a focused window must hold the pointer";

    other.ActivateSelf();
    const bool released = PumpUntil([this, &other](const std::vector<PlatformEvent>&) {
        return !window_->HasFocus() && !other.PointerGrabbedElsewhere();
    });
    EXPECT_TRUE(released) << "the pointer stayed grabbed while another window had focus";
    EXPECT_TRUE(mouse->IsRelativeMode()) << "relative mode stays enabled while suspended";

    other.Activate(static_cast<::Window>(window_->GetWindowHandle()));
    const bool retaken = PumpUntil([this, &other](const std::vector<PlatformEvent>&) {
        return window_->HasFocus() && other.PointerGrabbedElsewhere();
    });
    EXPECT_TRUE(retaken) << "focus came back but the pointer was not locked again";

    mouse->SetRelativeMode(window_->GetId(), false);
}

TEST_F(X11WithWindowManager, TheWindowManagerSeesTheTitleCnaSet)
{
    if (std::system("command -v xdotool >/dev/null 2>&1") != 0)
    {
        GTEST_SKIP() << "xdotool is not installed";
    }
    window_ = MakeVisibleWindow("CNA title for the WM");
    PumpUntil([](const std::vector<PlatformEvent>&) { return false; },
              std::chrono::milliseconds(300));

    // Read back through an external tool rather than through CNA, so this asserts what another
    // program sees rather than what CNA remembers writing.
    const std::string command =
        "xdotool getwindowname " + std::to_string(window_->GetWindowHandle()) + " 2>/dev/null";
    std::string name;
    {
        std::unique_ptr<FILE, int (*)(FILE*)> pipe(popen(command.c_str(), "r"), pclose);
        if (pipe != nullptr)
        {
            char buffer[512] = {};
            if (std::fgets(buffer, sizeof(buffer), pipe.get()) != nullptr)
            {
                name = buffer;
            }
        }
    }
    while (!name.empty() && (name.back() == '\n' || name.back() == '\r'))
    {
        name.pop_back();
    }
    EXPECT_EQ(name, "CNA title for the WM");
}

} // namespace
