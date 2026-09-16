// SPDX-License-Identifier: MS-PL
//
// plans/plan_wayland.md WAYLAND-0091: the desktop portal, and the parent window a Wayland client
// can give it.
//
// A Wayland client cannot name its window to another process the way an X11 client names an XID:
// the portal is told `wayland:<handle>`, where the handle comes from `zxdg_exporter_v2`. That is
// the one thing this backend does differently from X11 here, so it is what these tests check --
// against the same portal the X11 tests play (FreedesktopFakePortal.hpp), asked the same
// questions, on a private `dbus-daemon` configured with no service it could start.
//
// **No test reaches the session bus of the desktop it runs on.** A file chooser opened there would
// open on that desktop. WaylandTestEnvironment.cpp points the session bus at nothing before any
// test runs; each test here points it at its own private bus and puts it back afterwards.

#include <gtest/gtest.h>

#if defined(CNA_WAYLAND_HAVE_TEST_COMPOSITOR) && defined(CNA_PLATFORM_HAVE_DBUS)

#include "FreedesktopFakePortal.hpp"
#include "FreedesktopPrivateSessionBus.hpp"
#include "WaylandTestCompositor.hpp"

#include "../../../src/Wayland/WaylandPlatform.hpp"

#include "CNA/Platform/PlatformException.hpp"
#include "System/Environment.hpp"

#include <chrono>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace CNA::Platform;
using namespace CNA::Platform::Wayland;
using namespace CNA::Platform::Wayland::Testing;
using CNA::Platform::Freedesktop::GetDBus;
using CNA::Platform::Freedesktop::Testing::FakePortal;
using CNA::Platform::Freedesktop::Testing::PortalAnswer;
using CNA::Platform::Freedesktop::Testing::PortalCall;
using CNA::Platform::Freedesktop::Testing::PrivateBus;
using namespace std::chrono_literals;

/// A private bus with a portal on it, a compositor, and a platform that found both.
class WaylandPortal : public ::testing::Test
{
protected:
    void SetUp() override
    {
        if (!GetDBus().loaded)
        {
            GTEST_SKIP() << "libdbus is not installed";
        }
        bus_ = std::make_unique<PrivateBus>();
        if (bus_->Address().empty())
        {
            GTEST_SKIP() << "dbus-daemon could not be started (is it installed?)";
        }
        saved_ = System::Environment::GetEnvironmentVariable("DBUS_SESSION_BUS_ADDRESS").value_or(std::string());
        System::Environment::SetEnvironmentVariable("DBUS_SESSION_BUS_ADDRESS", bus_->Address());
        portal_ = std::make_unique<FakePortal>(bus_->Address());
        ASSERT_TRUE(portal_->Ready());
    }

    void TearDown() override
    {
        windows_.clear();
        platform_.reset();
        compositor_.reset();
        portal_.reset();
        bus_.reset();
        System::Environment::SetEnvironmentVariable("DBUS_SESSION_BUS_ADDRESS", saved_);
        ::unsetenv("WAYLAND_SOCKET");
    }

    /// Starts the compositor and a platform on it. The platform looks for the portal as it is
    /// created, which is why the bus and the portal exist first.
    void Start(CompositorOptions options = {})
    {
        compositor_ = std::make_unique<TestCompositor>(std::move(options));
        compositor_->ExportSocket();
        platform_ = std::make_unique<WaylandPlatform>();
        ::unsetenv("WAYLAND_SOCKET");
        ASSERT_NE(platform_->GetConnectionForTesting(), nullptr) << platform_->GetConnectionError();
    }

    IPlatformWindow& MakeWindow()
    {
        WindowDescription description;
        description.title = "portal test";
        windows_.push_back(platform_->CreateWindow(description));
        windows_.back()->Show();
        return *windows_.back();
    }

    /// Runs the game's loop until the dialog's answer arrives: the portal's reply is delivered
    /// from `PollEvents`, never from inside the call that asked for it.
    template <typename T>
    bool PumpUntil(const std::optional<T>& answer, const std::chrono::milliseconds budget = 5000ms)
    {
        const auto deadline = std::chrono::steady_clock::now() + budget;
        std::vector<PlatformEvent> events;
        while (!answer.has_value() && std::chrono::steady_clock::now() < deadline)
        {
            platform_->PollEvents(events);
            std::this_thread::sleep_for(2ms);
        }
        return answer.has_value();
    }

    std::unique_ptr<PrivateBus> bus_;
    std::unique_ptr<FakePortal> portal_;
    std::unique_ptr<TestCompositor> compositor_;
    std::unique_ptr<WaylandPlatform> platform_;
    std::vector<std::unique_ptr<IPlatformWindow>> windows_;
    std::string saved_;
};

TEST_F(WaylandPortal, APortalOnTheBusIsWhatMakesFileDialogsAvailable)
{
    Start();
    ASSERT_NE(platform_->GetDialogs(), nullptr) << "the portal was on the bus, yet there is no dialog service";
    EXPECT_TRUE(platform_->GetCapabilities().nativeFileDialog);
}

TEST_F(WaylandPortal, AFileDialogNamesItsWindowToThePortalThroughXdgForeign)
{
    CompositorOptions options;
    options.exporter = true;
    Start(options);
    IPlatformWindow& window = MakeWindow();
    std::vector<PlatformEvent> events;
    platform_->PollEvents(events);

    portal_->Answer({0, {"file:///tmp/cna-portal-test/chosen.png"}});
    std::optional<std::vector<std::string>> chosen;
    platform_->GetDialogs()->ShowOpenFileDialog([&chosen](const std::vector<std::string>& paths) { chosen = paths; },
                                                {{"Images", {"png"}}}, "/tmp", false, &window);
    ASSERT_TRUE(PumpUntil(chosen)) << "the portal's answer never reached the application";
    ASSERT_EQ(chosen->size(), 1u);
    EXPECT_EQ((*chosen)[0], "/tmp/cna-portal-test/chosen.png") << "a file: URI becomes a local path";

    const std::vector<PortalCall> calls = portal_->Calls();
    ASSERT_FALSE(calls.empty());
    EXPECT_EQ(calls[0].method, "OpenFile");
    // The one thing Wayland does differently from X11 here: the window is named by an exported
    // handle, not by an XID. The test compositor hands out cna-test-handle-N.
    EXPECT_EQ(calls[0].parent, "wayland:cna-test-handle-1")
        << "the portal was not told which window the dialog belongs to";
    EXPECT_EQ(compositor_->GetExportCount(), 1) << "the toplevel was never exported";
}

TEST_F(WaylandPortal, WithoutXdgForeignTheDialogSaysItHasNoParentRatherThanInventOne)
{
    CompositorOptions options;
    options.exporter = false;  // GNOME and KDE offer it; a plainer compositor may not
    Start(options);
    IPlatformWindow& window = MakeWindow();
    std::vector<PlatformEvent> events;
    platform_->PollEvents(events);

    portal_->Answer({0, {"file:///tmp/cna-portal-test/saved.txt"}});
    std::optional<std::vector<std::string>> chosen;
    platform_->GetDialogs()->ShowSaveFileDialog([&chosen](const std::vector<std::string>& paths) { chosen = paths; }, {},
                                                "/tmp", &window);
    ASSERT_TRUE(PumpUntil(chosen));
    const std::vector<PortalCall> calls = portal_->Calls();
    ASSERT_FALSE(calls.empty());
    EXPECT_EQ(calls[0].method, "SaveFile");
    EXPECT_EQ(calls[0].parent, "") << "a handle was invented for a compositor that exports nothing";
}

TEST_F(WaylandPortal, TheSameExportedHandleServesEveryDialogOfOneWindow)
{
    CompositorOptions options;
    options.exporter = true;
    Start(options);
    IPlatformWindow& window = MakeWindow();
    std::vector<PlatformEvent> events;
    platform_->PollEvents(events);

    for (int round = 0; round < 3; ++round)
    {
        portal_->Answer({0, {"file:///tmp/cna-portal-test/one.txt"}});
        std::optional<std::vector<std::string>> chosen;
        platform_->GetDialogs()->ShowOpenFileDialog([&chosen](const std::vector<std::string>& paths) { chosen = paths; },
                                                    {}, "/tmp", false, &window);
        ASSERT_TRUE(PumpUntil(chosen)) << "round " << round;
    }
    const std::vector<PortalCall> calls = portal_->Calls();
    ASSERT_EQ(calls.size(), 3u);
    for (const PortalCall& call : calls)
    {
        EXPECT_EQ(call.parent, "wayland:cna-test-handle-1");
    }
    // One export for the window, not one per dialog: the handle outlives the dialog that needed it.
    EXPECT_EQ(compositor_->GetExportCount(), 1);
}

TEST_F(WaylandPortal, ADialogWithNoWindowNamesNoParent)
{
    CompositorOptions options;
    options.exporter = true;
    Start(options);
    MakeWindow();
    std::vector<PlatformEvent> events;
    platform_->PollEvents(events);

    portal_->Answer({0, {"file:///tmp/cna-portal-test/folder"}});
    std::optional<std::vector<std::string>> chosen;
    platform_->GetDialogs()->ShowOpenFolderDialog([&chosen](const std::vector<std::string>& paths) { chosen = paths; },
                                                  "/tmp", false, nullptr);
    ASSERT_TRUE(PumpUntil(chosen));
    const std::vector<PortalCall> calls = portal_->Calls();
    ASSERT_FALSE(calls.empty());
    EXPECT_EQ(calls[0].parent, "") << "a dialog nobody parented was given a parent";
    EXPECT_EQ(compositor_->GetExportCount(), 0) << "a window was exported for a dialog that has no parent";
}

TEST_F(WaylandPortal, TheGameKeepsRunningWhileADialogIsOpen)
{
    CompositorOptions options;
    options.exporter = true;
    Start(options);
    IPlatformWindow& window = MakeWindow();
    std::vector<PlatformEvent> events;
    platform_->PollEvents(events);

    // A portal that never answers: the game's loop must keep turning, and the compositor must see
    // no protocol error from the waiting.
    portal_->Answer({0, {}, false, false, true});
    std::optional<std::vector<std::string>> chosen;
    platform_->GetDialogs()->ShowOpenFileDialog([&chosen](const std::vector<std::string>& paths) { chosen = paths; }, {},
                                                "/tmp", false, &window);
    const auto started = std::chrono::steady_clock::now();
    for (int frame = 0; frame < 200; ++frame)
    {
        platform_->PollEvents(events);
    }
    const auto elapsed = std::chrono::steady_clock::now() - started;
    EXPECT_FALSE(chosen.has_value()) << "an unanswered dialog answered anyway";
    EXPECT_LT(elapsed, 2s) << "200 frames with a dialog open took " << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()
                           << " ms: the loop is waiting on the portal";
    EXPECT_EQ(compositor_->GetPostedError(), "");
}

} // namespace

#endif
