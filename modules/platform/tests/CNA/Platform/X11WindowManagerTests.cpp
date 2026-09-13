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

#include <gtest/gtest.h>

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

bool HasDisplay()
{
    const char* display = std::getenv("DISPLAY");
    return display != nullptr && display[0] != '\0';
}

bool HasWindowManagerBinary()
{
    return std::system("command -v openbox >/dev/null 2>&1") == 0;
}

/// Starts a window manager on the current display and stops it in the destructor.
///
/// Owning it here rather than expecting the harness to provide one keeps the test honest about
/// what it needs: a run with no window manager skips instead of quietly measuring nothing.
class ScopedWindowManager
{
public:
    ScopedWindowManager()
    {
        pid_ = fork();
        if (pid_ == 0)
        {
            // --replace so a previous run's leftover cannot make this one fail, and stdio closed
            // so its startup chatter does not interleave with the test output.
            ::freopen("/dev/null", "w", stdout);
            ::freopen("/dev/null", "w", stderr);
            ::execlp("openbox", "openbox", "--replace", static_cast<char*>(nullptr));
            ::_exit(127);
        }
    }

    ~ScopedWindowManager()
    {
        if (pid_ > 0)
        {
            ::kill(pid_, SIGTERM);
            int status = 0;
            ::waitpid(pid_, &status, 0);
        }
    }

    ScopedWindowManager(const ScopedWindowManager&) = delete;
    ScopedWindowManager& operator=(const ScopedWindowManager&) = delete;

    [[nodiscard]] bool Started() const { return pid_ > 0; }

private:
    ::pid_t pid_ = -1;
};

class X11WithWindowManager : public ::testing::Test
{
protected:
    void SetUp() override
    {
        if (!HasDisplay())
        {
            GTEST_SKIP() << "no DISPLAY";
        }
        if (!HasWindowManagerBinary())
        {
            GTEST_SKIP() << "openbox is not installed; EWMH state transitions have nothing to "
                            "perform them and asserting them here would test the environment";
        }

        windowManager_ = std::make_unique<ScopedWindowManager>();
        ASSERT_TRUE(windowManager_->Started());

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
            GTEST_SKIP() << "openbox did not become the EWMH window manager in time";
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
        windowManager_.reset();
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

    std::unique_ptr<ScopedWindowManager> windowManager_;
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

TEST_F(X11WithWindowManager, ClosingASecondaryWindowIsNotApplicationTermination)
{
    // The behaviour that separates a multi-window application from a single-window one. A window
    // manager's close button sends WM_DELETE_WINDOW to one window, and the application decides.
    auto first = MakeVisibleWindow("first");
    auto second = MakeVisibleWindow("second");
    seen_.clear();

    // Ask the window manager to close the second window the way a user would, through the same
    // protocol its title-bar button uses.
    const std::string command =
        "xdotool windowclose " + std::to_string(second->GetWindowHandle()) + " >/dev/null 2>&1";
    if (std::system("command -v xdotool >/dev/null 2>&1") != 0)
    {
        GTEST_SKIP() << "xdotool is not installed, so there is no way to ask the window manager "
                        "to close a window the way a user would";
    }
    (void) std::system(command.c_str());

    // Whatever arrives, it must not be a QuitEvent: a second window is still open.
    PumpUntil([](const std::vector<PlatformEvent>&) { return false; },
              std::chrono::milliseconds(1000));
    const bool quit = std::any_of(seen_.begin(), seen_.end(), [](const PlatformEvent& event) {
        return std::holds_alternative<QuitEvent>(event);
    });
    EXPECT_FALSE(quit) << "closing one of two windows must not ask the application to quit";
    EXPECT_EQ(first->GetTitle(), "first");
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
