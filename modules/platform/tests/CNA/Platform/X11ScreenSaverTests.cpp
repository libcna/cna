// SPDX-License-Identifier: MS-PL
//
// plans/plan_x11.md X11-0170: keeping the screen on while a game asks -- the desktop's screen saver
// through org.freedesktop.ScreenSaver, else the X server's, suspended for this client alone.
//
// On the launcher's server only, and never through the desktop's own session bus: the desktop's
// service is played by the test on a private bus (X11PrivateSessionBus.hpp), and the X server is the
// launcher's private Xvfb.

#include <gtest/gtest.h>

#include "../../../src/X11/X11Displays.hpp"
#include "../../../src/X11/X11Headers.hpp"
#include "../../../src/X11/X11ScreenSaver.hpp"

#include "CNA/Platform/PlatformFactory.hpp"
#include "System/Environment.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#if defined(CNA_X11_HAVE_DBUS)
#include "X11PrivateSessionBus.hpp"
#endif

#include <csignal>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace {

using namespace CNA::Platform;
using namespace CNA::Platform::X11;

TEST(X11ScreenSaverName, TheDesktopIsToldTheExecutablesName)
{
    const std::string name = ScreenSaverApplicationName();
    ASSERT_FALSE(name.empty());
    // The kernel's short name of this very process: the executable's, cut to 15 characters.
    const std::string executable = std::filesystem::read_symlink("/proc/self/exe").filename().string();
    EXPECT_EQ(executable.rfind(name, 0), 0u) << name << " is not how " << executable << " begins";
}

class X11ScreenSaverLive : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const char* privateServer = std::getenv("CNA_X11_PRIVATE_TEST_SERVER");
        if (privateServer == nullptr || std::string(privateServer) != "1")
        {
            GTEST_SKIP() << "suspends the server's screen saver; needs tools/platform/x11_test_server.sh";
        }
        observer_ = XOpenDisplay(nullptr);
        ASSERT_NE(observer_, nullptr);
    }

    void TearDown() override
    {
        platform_.reset();
        if (observer_ != nullptr)
        {
            XCloseDisplay(observer_);
        }
    }

    void OpenPlatform()
    {
        platform_ = PlatformFactory::Create("X11");
        platform_->AcquireSubsystem(PlatformSubsystem::Video);
        displays_ = platform_->GetDisplays();
        ASSERT_NE(displays_, nullptr);
    }

    [[nodiscard]] X11ScreenSaverInhibitor::Method Method() const
    {
        return static_cast<const X11Displays*>(displays_)->GetScreenSaverMethod();
    }

    [[nodiscard]] int ServerTimeout() const
    {
        int timeout = 0;
        int interval = 0;
        int preferBlanking = 0;
        int allowExposures = 0;
        XGetScreenSaver(observer_, &timeout, &interval, &preferBlanking, &allowExposures);
        return timeout;
    }

    Display* observer_ = nullptr;
    std::unique_ptr<IPlatform> platform_;
    IPlatformDisplays* displays_ = nullptr;
};

TEST_F(X11ScreenSaverLive, WithoutADesktopServiceTheServersSaverIsSuspendedForThisClientAlone)
{
    // The binary has no session bus (X11DesktopPortalTests.cpp), so no desktop service.
    OpenPlatform();
    const int timeout = ServerTimeout();
    EXPECT_TRUE(displays_->IsScreenSaverEnabled());
    displays_->SetScreenSaverEnabled(false);
    EXPECT_FALSE(displays_->IsScreenSaverEnabled());
    EXPECT_EQ(Method(), X11ScreenSaverInhibitor::Method::ServerSuspend) << "Xvfb has MIT-SCREEN-SAVER 1.1";
    EXPECT_EQ(ServerTimeout(), timeout) << "the server-wide timeout, which outlives a crash, is not touched";
    displays_->SetScreenSaverEnabled(false);
    EXPECT_EQ(Method(), X11ScreenSaverInhibitor::Method::ServerSuspend);
    displays_->SetScreenSaverEnabled(true);
    EXPECT_TRUE(displays_->IsScreenSaverEnabled());
    EXPECT_EQ(Method(), X11ScreenSaverInhibitor::Method::None);
    EXPECT_EQ(ServerTimeout(), timeout);
}

#if defined(CNA_X11_HAVE_XSS)

/// The server's saver, as another client sees it: on or not.
bool SaverIsOn(Display* display)
{
    XScreenSaverInfo* info = XScreenSaverAllocInfo();
    XScreenSaverQueryInfo(display, DefaultRootWindow(display), info);
    const bool on = info->state == ScreenSaverOn;
    XFree(info);
    return on;
}

/// Whether the saver comes on within a budget, nobody touching anything.
bool SaverComesOnWithin(Display* display, const std::chrono::milliseconds budget)
{
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (SaverIsOn(display))
        {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return SaverIsOn(display);
}

TEST_F(X11ScreenSaverLive, TheSuspendedSaverStaysOffAndComesBackWhenLiftedOrWhenTheGameGoesAway)
{
    // The launcher's own server: its saver is set to start after one idle second, and put back.
    int timeout = 0;
    int interval = 0;
    int preferBlanking = 0;
    int allowExposures = 0;
    XGetScreenSaver(observer_, &timeout, &interval, &preferBlanking, &allowExposures);
    XSetScreenSaver(observer_, 1, interval, preferBlanking, allowExposures);
    XForceScreenSaver(observer_, ScreenSaverReset);
    XSync(observer_, kXFalse);
    ASSERT_TRUE(SaverComesOnWithin(observer_, std::chrono::milliseconds(4000)))
        << "the server's saver never came on by itself, so nothing below would mean anything";

    OpenPlatform();
    XForceScreenSaver(observer_, ScreenSaverReset);
    XSync(observer_, kXFalse);
    displays_->SetScreenSaverEnabled(false);
    ASSERT_EQ(Method(), X11ScreenSaverInhibitor::Method::ServerSuspend);
    EXPECT_FALSE(SaverComesOnWithin(observer_, std::chrono::milliseconds(2500))) << "the saver came on while suspended";

    displays_->SetScreenSaverEnabled(true);
    EXPECT_TRUE(SaverComesOnWithin(observer_, std::chrono::milliseconds(4000))) << "lifting gave nothing back";

    // Suspended again, and the platform destroyed without lifting it: its destructor does.
    XForceScreenSaver(observer_, ScreenSaverReset);
    XSync(observer_, kXFalse);
    displays_->SetScreenSaverEnabled(false);
    EXPECT_FALSE(SaverComesOnWithin(observer_, std::chrono::milliseconds(1500)));
    platform_.reset();
    displays_ = nullptr;
    EXPECT_TRUE(SaverComesOnWithin(observer_, std::chrono::milliseconds(4000)))
        << "a destroyed platform left the saver suspended";

    // And a game killed outright, no destructor run: the server gives the saver back itself.
    const std::string report =
        (std::filesystem::temp_directory_path() / ("cna-saver-peer-" + std::to_string(::getpid()))).string();
    std::filesystem::remove(report);
    XForceScreenSaver(observer_, ScreenSaverReset);
    XSync(observer_, kXFalse);
    const std::string reportVariable = "CNA_X11_SAVER_PEER_REPORT=" + report;
    std::vector<std::string> environment;
    for (char** entry = environ; *entry != nullptr; ++entry) { environment.emplace_back(*entry); }
    environment.push_back(reportVariable);
    std::vector<char*> environmentPointers;
    for (std::string& entry : environment) { environmentPointers.push_back(entry.data()); }
    environmentPointers.push_back(nullptr);
    std::string executable = "/proc/self/exe";
    std::string filter = "--gtest_filter=X11ScreenSaverPeer.DISABLED_HoldTheSaverSuspended";
    std::string disabled = "--gtest_also_run_disabled_tests";
    std::vector<char*> arguments = {executable.data(), filter.data(), disabled.data(), nullptr};
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0);
    pid_t peer = -1;
    const int spawned =
        posix_spawn(&peer, executable.c_str(), &actions, nullptr, arguments.data(), environmentPointers.data());
    posix_spawn_file_actions_destroy(&actions);
    ASSERT_EQ(spawned, 0);
    const auto readyDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    std::string said;
    while (said.find("ready") == std::string::npos && std::chrono::steady_clock::now() < readyDeadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        std::ifstream in(report);
        std::getline(in, said);
    }
    EXPECT_EQ(said, "ready 2") << "the peer never suspended the saver (2 is ServerSuspend)";
    EXPECT_FALSE(SaverComesOnWithin(observer_, std::chrono::milliseconds(2000))) << "the peer's suspension did not hold";
    ::kill(peer, SIGKILL);
    int status = 0;
    ::waitpid(peer, &status, 0);
    EXPECT_TRUE(SaverComesOnWithin(observer_, std::chrono::milliseconds(4000)))
        << "a killed game left the saver suspended";
    std::filesystem::remove(report);

    XSetScreenSaver(observer_, timeout, interval, preferBlanking, allowExposures);
    XForceScreenSaver(observer_, ScreenSaverReset);
    XSync(observer_, kXFalse);
}

/// The game killed above: suspends the saver, says so, and waits to be killed.
TEST(X11ScreenSaverPeer, DISABLED_HoldTheSaverSuspended)
{
    const char* report = std::getenv("CNA_X11_SAVER_PEER_REPORT");
    ASSERT_NE(report, nullptr);
    std::unique_ptr<IPlatform> platform = PlatformFactory::Create("X11");
    platform->AcquireSubsystem(PlatformSubsystem::Video);
    IPlatformDisplays* displays = platform->GetDisplays();
    ASSERT_NE(displays, nullptr);
    displays->SetScreenSaverEnabled(false);
    {
        std::ofstream out(report);
        out << "ready " << static_cast<int>(static_cast<const X11Displays*>(displays)->GetScreenSaverMethod()) << "\n";
    }
    std::this_thread::sleep_for(std::chrono::seconds(30));
}

#endif // CNA_X11_HAVE_XSS

#if defined(CNA_X11_HAVE_DBUS)

/// org.freedesktop.ScreenSaver as a desktop implements it, played on a private bus.
class FakeScreenSaver
{
public:
    struct Call
    {
        std::string method;
        std::string sender;
        std::string application;
        std::string reason;
        std::uint32_t cookie = 0;
    };

    explicit FakeScreenSaver(const std::string& address)
    {
        const X11DBusApi& dbus = DBusApi();
        DBusError error;
        dbus.error_init(&error);
        connection_ = dbus.connection_open_private(address.c_str(), &error);
        if (connection_ == nullptr)
        {
            dbus.error_free(&error);
            return;
        }
        dbus.connection_set_exit_on_disconnect(connection_, FALSE);
        if (!dbus.bus_register(connection_, &error) ||
            dbus.bus_request_name(connection_, "org.freedesktop.ScreenSaver", DBUS_NAME_FLAG_DO_NOT_QUEUE, &error) !=
                DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER)
        {
            dbus.error_free(&error);
            return;
        }
        ready_ = true;
        running_ = true;
        thread_ = std::thread([this] { Serve(); });
    }

    ~FakeScreenSaver()
    {
        running_ = false;
        if (thread_.joinable()) { thread_.join(); }
        if (connection_ != nullptr)
        {
            DBusApi().connection_close(connection_);
            DBusApi().connection_unref(connection_);
        }
    }

    [[nodiscard]] bool Ready() const { return ready_; }

    [[nodiscard]] std::vector<Call> Calls()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return calls_;
    }

    /// Whether a connection's unique name is still on the bus.
    [[nodiscard]] bool IsOnTheBus(const std::string& name)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const X11DBusApi& dbus = DBusApi();
        DBusError error;
        dbus.error_init(&error);
        const bool present = dbus.bus_name_has_owner(connection_, name.c_str(), &error) != FALSE;
        dbus.error_free(&error);
        return present;
    }

private:
    void Serve()
    {
        const X11DBusApi& dbus = DBusApi();
        while (running_)
        {
            std::unique_lock<std::mutex> lock(mutex_);
            dbus.connection_read_write(connection_, 0);
            while (DBusMessage* message = dbus.connection_pop_message(connection_))
            {
                Handle(message);
                dbus.message_unref(message);
            }
            lock.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }

    void Handle(DBusMessage* message)
    {
        const X11DBusApi& dbus = DBusApi();
        const bool inhibit = dbus.message_is_method_call(message, "org.freedesktop.ScreenSaver", "Inhibit");
        const bool uninhibit = dbus.message_is_method_call(message, "org.freedesktop.ScreenSaver", "UnInhibit");
        if (!inhibit && !uninhibit)
        {
            return;
        }
        Call call;
        call.method = inhibit ? "Inhibit" : "UnInhibit";
        const char* sender = dbus.message_get_sender(message);
        call.sender = sender != nullptr ? sender : "";
        DBusMessageIter arguments;
        dbus.message_iter_init(message, &arguments);
        if (inhibit)
        {
            const char* application = nullptr;
            const char* reason = nullptr;
            dbus.message_iter_get_basic(&arguments, &application);
            dbus.message_iter_next(&arguments);
            dbus.message_iter_get_basic(&arguments, &reason);
            call.application = application != nullptr ? application : "";
            call.reason = reason != nullptr ? reason : "";
            call.cookie = ++nextCookie_;
        }
        else
        {
            dbus_uint32_t cookie = 0;
            dbus.message_iter_get_basic(&arguments, &cookie);
            call.cookie = cookie;
        }
        DBusMessage* reply = dbus.message_new_method_return(message);
        if (inhibit)
        {
            DBusMessageIter replyArguments;
            dbus.message_iter_init_append(reply, &replyArguments);
            const dbus_uint32_t cookie = call.cookie;
            dbus.message_iter_append_basic(&replyArguments, DBUS_TYPE_UINT32, &cookie);
        }
        dbus.connection_send(connection_, reply, nullptr);
        dbus.connection_flush(connection_);
        dbus.message_unref(reply);
        calls_.push_back(std::move(call));
    }

    DBusConnection* connection_ = nullptr;
    bool ready_ = false;
    std::atomic<bool> running_{false};
    std::thread thread_;
    std::mutex mutex_;
    std::vector<Call> calls_;
    std::uint32_t nextCookie_ = 40;
};

class X11ScreenSaverDesktopLive : public X11ScreenSaverLive
{
protected:
    void SetUp() override
    {
        X11ScreenSaverLive::SetUp();
        if (IsSkipped() || HasFatalFailure())
        {
            return;
        }
        if (!DBusApi().loaded)
        {
            GTEST_SKIP() << "libdbus is not installed";
        }
        bus_ = std::make_unique<Testing::PrivateBus>();
        if (bus_->Address().empty())
        {
            GTEST_SKIP() << "dbus-daemon could not be started (is it installed?)";
        }
        saved_ = System::Environment::GetEnvironmentVariable("DBUS_SESSION_BUS_ADDRESS");
        System::Environment::SetEnvironmentVariable("DBUS_SESSION_BUS_ADDRESS", bus_->Address());
        overridden_ = true;
        service_ = std::make_unique<FakeScreenSaver>(bus_->Address());
        ASSERT_TRUE(service_->Ready());
    }

    void TearDown() override
    {
        platform_.reset();
        service_.reset();
        bus_.reset();
        if (overridden_)
        {
            System::Environment::SetEnvironmentVariable("DBUS_SESSION_BUS_ADDRESS", saved_);
        }
        X11ScreenSaverLive::TearDown();
    }

    /// Waits until the service has seen a number of calls.
    std::vector<FakeScreenSaver::Call> WaitForCalls(const std::size_t count)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        std::vector<FakeScreenSaver::Call> calls = service_->Calls();
        while (calls.size() < count && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            calls = service_->Calls();
        }
        return calls;
    }

    bool WaitUntilGone(const std::string& name)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (service_->IsOnTheBus(name) && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return !service_->IsOnTheBus(name);
    }

    std::unique_ptr<Testing::PrivateBus> bus_;
    std::unique_ptr<FakeScreenSaver> service_;
    std::optional<std::string> saved_;
    bool overridden_ = false;
};

TEST_F(X11ScreenSaverDesktopLive, TheDesktopsScreenSaverIsAskedFirstAndToldWhenItMayRunAgain)
{
    OpenPlatform();
    const int timeout = ServerTimeout();
    EXPECT_TRUE(displays_->IsScreenSaverEnabled());
    displays_->SetScreenSaverEnabled(false);
    EXPECT_EQ(Method(), X11ScreenSaverInhibitor::Method::DesktopService);
    EXPECT_FALSE(displays_->IsScreenSaverEnabled());
    displays_->SetScreenSaverEnabled(false);  // Once is enough.
    std::vector<FakeScreenSaver::Call> calls = WaitForCalls(1);
    ASSERT_EQ(calls.size(), 1u);
    EXPECT_EQ(calls[0].method, "Inhibit");
    EXPECT_EQ(calls[0].application, ScreenSaverApplicationName());
    EXPECT_EQ(calls[0].reason, "Playing a game");
    EXPECT_EQ(ServerTimeout(), timeout) << "the X server is left alone when the desktop is asked";

    displays_->SetScreenSaverEnabled(true);
    calls = WaitForCalls(2);
    ASSERT_EQ(calls.size(), 2u);
    EXPECT_EQ(calls[1].method, "UnInhibit");
    EXPECT_EQ(calls[1].cookie, calls[0].cookie) << "the request it was given back";
    EXPECT_EQ(calls[1].sender, calls[0].sender);
    EXPECT_TRUE(displays_->IsScreenSaverEnabled());
    EXPECT_TRUE(WaitUntilGone(calls[0].sender)) << "the connection that held the request is closed";
}

TEST_F(X11ScreenSaverDesktopLive, AGameThatGoesAwayTakesItsRequestWithIt)
{
    OpenPlatform();
    displays_->SetScreenSaverEnabled(false);
    const std::vector<FakeScreenSaver::Call> calls = WaitForCalls(1);
    ASSERT_EQ(calls.size(), 1u);
    EXPECT_TRUE(service_->IsOnTheBus(calls[0].sender)) << "held while the game runs";
    platform_.reset();
    EXPECT_TRUE(WaitUntilGone(calls[0].sender))
        << "the request's connection outlived the game, so a desktop would keep the screen on";
}

#endif // CNA_X11_HAVE_DBUS

} // namespace
