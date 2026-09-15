// SPDX-License-Identifier: MS-PL
//
// plans/plan_wayland.md WAYLAND-0110: what every test of a Wayland build may reach.
//
// A developer runs the test binary from a terminal on their own desktop, and that terminal's
// environment names the desktop's compositor (WAYLAND_DISPLAY) and session bus
// (DBUS_SESSION_BUS_ADDRESS). A test that connected to either would open windows on the user's
// screen, take their clipboard, or ask their desktop portal to show a file chooser -- and on a
// machine whose session is locked, act on a desktop nobody is watching.
//
// So before any test runs, whatever started the binary: the session bus is a path that does not
// exist, and WAYLAND_DISPLAY names no compositor -- unless the launcher that started a PRIVATE
// compositor for the live suites (tools/platform/wayland_test_server.sh) names it in
// CNA_WAYLAND_TEST_DISPLAY. The protocol suites never use WAYLAND_DISPLAY at all: their
// compositor is in-process and is reached through WAYLAND_SOCKET.

#include <gtest/gtest.h>

#include <cstdlib>
#include <string>

namespace {

constexpr const char* kNoSessionBus = "unix:path=/nonexistent/cna-test-no-session-bus";
constexpr const char* kNoCompositor = "cna-test-no-compositor";

const bool kWaylandTestEnvironment = [] {
    ::setenv("DBUS_SESSION_BUS_ADDRESS", kNoSessionBus, 1);
    const char* privateDisplay = std::getenv("CNA_WAYLAND_TEST_DISPLAY");
    ::setenv("WAYLAND_DISPLAY", privateDisplay != nullptr && *privateDisplay != '\0' ? privateDisplay : kNoCompositor, 1);
    ::unsetenv("WAYLAND_SOCKET");
    return true;
}();

TEST(WaylandTestEnvironment, NoTestReachesTheDesktopsSessionBus)
{
    ASSERT_TRUE(kWaylandTestEnvironment);
    const char* address = std::getenv("DBUS_SESSION_BUS_ADDRESS");
    ASSERT_NE(address, nullptr);
    // A suite that starts a private bus sets its own address for its own duration and restores
    // this one; between tests it is always this.
    EXPECT_STREQ(address, kNoSessionBus);
}

TEST(WaylandTestEnvironment, NoTestReachesTheDesktopsCompositor)
{
    const char* display = std::getenv("WAYLAND_DISPLAY");
    ASSERT_NE(display, nullptr);
    const char* privateDisplay = std::getenv("CNA_WAYLAND_TEST_DISPLAY");
    if (privateDisplay != nullptr && *privateDisplay != '\0')
    {
        EXPECT_STREQ(display, privateDisplay) << "only the launcher's private compositor";
    }
    else
    {
        EXPECT_STREQ(display, kNoCompositor);
    }
    EXPECT_EQ(std::getenv("WAYLAND_SOCKET"), nullptr) << "a leftover socket would hand the next platform to a dead test";
}

} // namespace
