// SPDX-License-Identifier: MS-PL
//
// plans/plan_wayland.md WAYLAND-0113: the Wayland backend against GNOME's own compositor, with
// real input.
//
// tools/platform/wayland_test_server.sh --compositor mutter starts a headless gnome-shell on a
// private session bus. Input comes from that compositor's org.gnome.Mutter.RemoteDesktop API on
// that bus: a virtual keyboard and pointer that exist only inside the private compositor. Nothing
// here touches uinput or the desktop the tests run on (D-28).
//
// This is where the backend meets GNOME's rules for real: its focus policy, its keymap handling,
// its pointer locking, its missing xdg-decoration (so CNA's own title bar is what the user
// clicks), and a seat whose pointer appears only when the first pointer event arrives.

#include <gtest/gtest.h>

#if defined(CNA_PLATFORM_HAVE_DBUS)

#include "../../../src/Freedesktop/DBusLibrary.hpp"
#include "../../../src/Wayland/WaylandPlatform.hpp"

#include "CNA/Platform/Input/IPlatformKeyboard.hpp"
#include "CNA/Platform/Input/IPlatformMouse.hpp"
#include "CNA/Platform/Input/IPlatformTextInput.hpp"
#include "CNA/Platform/IPlatformSurfacePresenter.hpp"
#include "CNA/Platform/IPlatformSystemServices.hpp"
#include "CNA/Platform/PlatformException.hpp"

#include <linux/input-event-codes.h>

#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

extern char** environ;

namespace {

using namespace CNA::Platform;
using namespace CNA::Platform::Wayland;
using CNA::Platform::Freedesktop::GetDBus;
using namespace std::chrono_literals;

std::string PrivateBus()
{
    const char* compositor = std::getenv("CNA_WAYLAND_TEST_COMPOSITOR");
    const char* bus = std::getenv("CNA_WAYLAND_TEST_BUS");
    if (compositor == nullptr || std::string(compositor) != "mutter" || bus == nullptr)
    {
        return {};
    }
    return bus;
}

/// A RemoteDesktop session on the private bus: a keyboard and a pointer inside the private
/// compositor. The session lives as long as this connection.
class RemoteInput
{
public:
    explicit RemoteInput(const std::string& address)
    {
        const auto& dbus = GetDBus();
        if (!dbus.loaded)
        {
            error_ = "libdbus is not available";
            return;
        }
        DBusError error;
        dbus.error_init(&error);
        connection_ = dbus.connection_open_private(address.c_str(), &error);
        if (connection_ == nullptr || !dbus.bus_register(connection_, &error))
        {
            error_ = dbus.error_is_set(&error) ? error.message : "cannot connect to the private bus";
            dbus.error_free(&error);
            return;
        }
        dbus.connection_set_exit_on_disconnect(connection_, false);
        DBusMessage* reply = Call("/org/gnome/Mutter/RemoteDesktop", "org.gnome.Mutter.RemoteDesktop", "CreateSession", {});
        if (reply == nullptr)
        {
            return;
        }
        DBusMessageIter iter;
        if (dbus.message_iter_init(reply, &iter) && dbus.message_iter_get_arg_type(&iter) == DBUS_TYPE_OBJECT_PATH)
        {
            const char* path = nullptr;
            dbus.message_iter_get_basic(&iter, &path);
            session_ = path != nullptr ? path : "";
        }
        dbus.message_unref(reply);
        if (session_.empty())
        {
            error_ = "CreateSession returned no session";
            return;
        }
        DBusMessage* started = Call(session_, kSession, "Start", {});
        if (started == nullptr)
        {
            session_.clear();
            return;
        }
        dbus.message_unref(started);
    }

    ~RemoteInput()
    {
        const auto& dbus = GetDBus();
        if (!session_.empty())
        {
            if (DBusMessage* reply = Call(session_, kSession, "Stop", {}))
            {
                dbus.message_unref(reply);
            }
        }
        if (connection_ != nullptr)
        {
            dbus.connection_close(connection_);
            dbus.connection_unref(connection_);
        }
    }

    [[nodiscard]] bool Ok() const { return !session_.empty(); }
    [[nodiscard]] const std::string& Error() const { return error_; }

    void Key(const std::uint32_t evdev, const bool pressed)
    {
        Send("NotifyKeyboardKeycode", {Arg::U(evdev), Arg::B(pressed)});
    }

    void Tap(const std::uint32_t evdev)
    {
        Key(evdev, true);
        Key(evdev, false);
    }

    void Button(const std::int32_t button, const bool pressed)
    {
        Send("NotifyPointerButton", {Arg::I(button), Arg::B(pressed)});
    }

    void Move(const double dx, const double dy) { Send("NotifyPointerMotionRelative", {Arg::D(dx), Arg::D(dy)}); }

    void WheelSteps(const std::uint32_t axis, const std::int32_t steps)
    {
        Send("NotifyPointerAxisDiscrete", {Arg::U(axis), Arg::I(steps)});
    }

private:
    static constexpr const char* kSession = "org.gnome.Mutter.RemoteDesktop.Session";

    struct Arg
    {
        int type = DBUS_TYPE_INVALID;
        std::uint32_t u = 0;
        std::int32_t i = 0;
        dbus_bool_t b = 0;
        double d = 0.0;
        static Arg U(const std::uint32_t value) { Arg arg; arg.type = DBUS_TYPE_UINT32; arg.u = value; return arg; }
        static Arg I(const std::int32_t value) { Arg arg; arg.type = DBUS_TYPE_INT32; arg.i = value; return arg; }
        static Arg B(const bool value) { Arg arg; arg.type = DBUS_TYPE_BOOLEAN; arg.b = value ? 1 : 0; return arg; }
        static Arg D(const double value) { Arg arg; arg.type = DBUS_TYPE_DOUBLE; arg.d = value; return arg; }
    };

    DBusMessage* Call(const std::string& path, const char* interface, const char* method, const std::vector<Arg>& args)
    {
        const auto& dbus = GetDBus();
        DBusMessage* message = dbus.message_new_method_call("org.gnome.Mutter.RemoteDesktop", path.c_str(), interface, method);
        DBusMessageIter iter;
        dbus.message_iter_init_append(message, &iter);
        for (const Arg& arg : args)
        {
            const void* value = arg.type == DBUS_TYPE_UINT32   ? static_cast<const void*>(&arg.u)
                                : arg.type == DBUS_TYPE_INT32  ? static_cast<const void*>(&arg.i)
                                : arg.type == DBUS_TYPE_BOOLEAN ? static_cast<const void*>(&arg.b)
                                                                : static_cast<const void*>(&arg.d);
            dbus.message_iter_append_basic(&iter, arg.type, value);
        }
        DBusError error;
        dbus.error_init(&error);
        DBusMessage* reply = dbus.connection_send_with_reply_and_block(connection_, message, 5000, &error);
        dbus.message_unref(message);
        if (reply == nullptr)
        {
            error_ = std::string(method) + ": " + (dbus.error_is_set(&error) ? error.message : "no reply");
            dbus.error_free(&error);
        }
        return reply;
    }

    void Send(const char* method, const std::vector<Arg>& args)
    {
        if (DBusMessage* reply = Call(session_, kSession, method, args))
        {
            GetDBus().message_unref(reply);
        }
    }

    DBusConnection* connection_ = nullptr;
    std::string session_;
    std::string error_;
};

class WaylandMutter : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const std::string bus = PrivateBus();
        if (bus.empty())
        {
            GTEST_SKIP() << "no private headless gnome-shell: run through wayland_test_server.sh --compositor mutter";
        }
        input_ = std::make_unique<RemoteInput>(bus);
        ASSERT_TRUE(input_->Ok()) << "RemoteDesktop session: " << input_->Error();
        // (The launcher has closed GNOME's startup overview, which would otherwise hold the
        // keyboard and the pointer.) The session's virtual keyboard and pointer are created by
        // their first events, and Mutter delivers only later ones: a Right Ctrl tap -- a key that
        // does nothing on its own, going nowhere with no window focused -- and a pointer moved
        // there and back, before any window exists, so the seat a test's platform binds already
        // has both and a window is entered when it appears under the pointer.
        input_->Tap(KEY_RIGHTCTRL);
        input_->Move(1, 0);
        input_->Move(-1, 0);
        std::this_thread::sleep_for(300ms);
        platform_ = std::make_unique<WaylandPlatform>();
        ASSERT_NE(platform_->GetConnectionForTesting(), nullptr) << platform_->GetConnectionError();
        platform_->AcquireSubsystem(PlatformSubsystem::Video);
    }

    void TearDown() override
    {
        presenters_.clear();
        windows_.clear();
        if (platform_ != nullptr)
        {
            if (WaylandConnection* connection = platform_->GetConnectionForTesting())
            {
                (void) connection->Roundtrip(1s);
                EXPECT_TRUE(connection->IsAlive()) << "GNOME ended the connection: " << connection->GetError();
            }
            platform_->ReleaseSubsystem(PlatformSubsystem::Video);
        }
        platform_.reset();
        input_.reset();
    }

    /// A window filling the virtual monitor, drawn once and focused: wherever the compositor's
    /// pointer is, it is over this window.
    IPlatformWindow& MakeFocusedWindow(const bool fullscreen = true)
    {
        WindowDescription description;
        description.title = "CNA Mutter test";
        if (fullscreen)
        {
            description.fullscreenMode = WindowFullscreenMode::BorderlessFullscreen;
        }
        windows_.push_back(platform_->CreateWindow(description));
        IPlatformWindow& window = *windows_.back();
        presenters_.push_back(platform_->CreateSurfacePresenter(window));
        Draw(window);
        // GNOME's focus-stealing prevention: a window mapped without an activation token, from a
        // client the user has not interacted with, is not given the keyboard (a program launched
        // from a desktop gets the launcher's token, WAYLAND-0093). Here the user clicks it -- the
        // window covers the monitor, so wherever the pointer is, it is over the window.
        if (!PumpUntil([&] { return window.HasFocus(); }, 1000ms))
        {
            input_->Move(1, 1);
            input_->Button(BTN_LEFT, true);
            input_->Button(BTN_LEFT, false);
        }
        EXPECT_TRUE(PumpUntil([&] { return window.HasFocus(); }, 5000ms))
            << "GNOME did not focus the new window (keyboard attached: " << platform_->GetKeyboard()->HasKeyboard()
            << ", window events seen: " << SeenOf<WindowEvent>().size() << ")";
        // What focusing took is not what the test is about.
        (void) PumpUntil([] { return false; }, 100ms);
        seen_.clear();
        return window;
    }

    void Draw(IPlatformWindow& window)
    {
        for (std::size_t index = 0; index < windows_.size(); ++index)
        {
            if (windows_[index].get() != &window)
            {
                continue;
            }
            const WindowSize size = window.GetPixelSize();
            std::vector<std::uint8_t> pixels(static_cast<std::size_t>(size.width) * size.height * 4, 0x30);
            SurfaceFrame frame;
            frame.pixels = pixels.data();
            frame.width = size.width;
            frame.height = size.height;
            frame.strideBytes = size.width * 4;
            presenters_[index]->Present(frame);
            window.Sync();
        }
    }

    void Pump()
    {
        batch_.clear();
        platform_->PollEvents(batch_);
        seen_.insert(seen_.end(), batch_.begin(), batch_.end());
    }

    bool PumpUntil(const std::function<bool()>& condition, const std::chrono::milliseconds budget = 3000ms)
    {
        const auto deadline = std::chrono::steady_clock::now() + budget;
        while (!condition())
        {
            if (std::chrono::steady_clock::now() >= deadline)
            {
                return condition();
            }
            (void) platform_->GetConnectionForTesting()->DispatchFor(10ms);
            Pump();
        }
        return true;
    }

    template <typename T>
    [[nodiscard]] std::vector<T> SeenOf() const
    {
        std::vector<T> result;
        for (const PlatformEvent& event : seen_)
        {
            if (const T* typed = std::get_if<T>(&event))
            {
                result.push_back(*typed);
            }
        }
        return result;
    }

    /// Runs an external program to completion while the game loop keeps pumping -- it may be
    /// reading a selection this process serves -- and returns its exit status (-1 on timeout).
    int RunTool(const std::vector<std::string>& argv, std::string* output)
    {
        int pipes[2] = {-1, -1};
        if (::pipe2(pipes, O_CLOEXEC) != 0)
        {
            return -1;
        }
        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions, pipes[1], STDOUT_FILENO);
        std::vector<char*> args;
        for (const std::string& arg : argv)
        {
            args.push_back(const_cast<char*>(arg.c_str()));
        }
        args.push_back(nullptr);
        pid_t child = -1;
        const int spawned = posix_spawn(&child, args[0], &actions, nullptr, args.data(), environ);
        posix_spawn_file_actions_destroy(&actions);
        ::close(pipes[1]);
        if (spawned != 0)
        {
            ::close(pipes[0]);
            return -1;
        }
        ::fcntl(pipes[0], F_SETFL, O_NONBLOCK);
        int status = -1;
        bool exited = false;
        bool readerOpen = true;
        const auto deadline = std::chrono::steady_clock::now() + 10s;
        while (std::chrono::steady_clock::now() < deadline && (!exited || readerOpen))
        {
            Pump();
            char chunk[65536];
            const ssize_t got = ::read(pipes[0], chunk, sizeof(chunk));
            if (got > 0)
            {
                if (output != nullptr) { output->append(chunk, static_cast<std::size_t>(got)); }
                continue;
            }
            if (got == 0)
            {
                readerOpen = false;
            }
            if (!exited && ::waitpid(child, &status, WNOHANG) == child)
            {
                exited = true;
            }
            std::this_thread::sleep_for(2ms);
        }
        ::close(pipes[0]);
        if (!exited)
        {
            ::kill(child, SIGKILL);
            ::waitpid(child, &status, 0);
            return -1;
        }
        return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }

    [[nodiscard]] std::string TypedText() const
    {
        std::string text;
        for (const TextInputEvent& event : SeenOf<TextInputEvent>())
        {
            text += event.text;
        }
        return text;
    }

    std::unique_ptr<RemoteInput> input_;
    std::unique_ptr<WaylandPlatform> platform_;
    std::vector<std::unique_ptr<IPlatformWindow>> windows_;
    std::vector<std::unique_ptr<IPlatformSurfacePresenter>> presenters_;
    std::vector<PlatformEvent> batch_;
    std::vector<PlatformEvent> seen_;
};

TEST_F(WaylandMutter, GnomeFocusesANewWindowAndItsKeysArrive)
{
    if (std::getenv("CNA_WAYLAND_TEST_IBUS") != nullptr)
    {
        GTEST_SKIP() << "an input method is running: what is typed becomes a composition, which is "
                        "what the WaylandIme suite is for";
    }
    IPlatformWindow& window = MakeFocusedWindow();
    platform_->GetTextInput()->Start(window.GetId(), TextInputType::Text);
    for (const std::uint32_t key : {KEY_H, KEY_E, KEY_L, KEY_L, KEY_O})
    {
        input_->Tap(key);
    }
    input_->Key(KEY_LEFTSHIFT, true);
    input_->Tap(KEY_W);
    input_->Key(KEY_LEFTSHIFT, false);
    ASSERT_TRUE(PumpUntil([&] { return TypedText().size() >= 6; })) << "typed so far: " << TypedText();
    EXPECT_EQ(TypedText(), "helloW");
    const std::vector<KeyEvent> keys = SeenOf<KeyEvent>();
    ASSERT_GE(keys.size(), 2u);
    EXPECT_EQ(keys.front().scancode, Scancode::H);
    EXPECT_EQ(keys.front().keycode, KeyCode::H);
    EXPECT_TRUE(keys.front().pressed);
    EXPECT_EQ(keys.front().window, window.GetId());
}

TEST_F(WaylandMutter, TheKeyboardSnapshotHoldsWhatIsDownAndNothingAfterRelease)
{
    MakeFocusedWindow();
    IPlatformKeyboard* keyboard = platform_->GetKeyboard();
    input_->Key(KEY_LEFTCTRL, true);
    input_->Key(KEY_SPACE, true);
    ASSERT_TRUE(PumpUntil([&] {
        keyboard->Update();
        const auto& held = keyboard->GetSnapshot().pressedKeys;
        return std::find(held.begin(), held.end(), KeyCode::Space) != held.end();
    }));
    EXPECT_NE(keyboard->GetSnapshot().modifiers & static_cast<std::uint16_t>(KeyModifier::Control), 0);
    input_->Key(KEY_SPACE, false);
    input_->Key(KEY_LEFTCTRL, false);
    ASSERT_TRUE(PumpUntil([&] {
        keyboard->Update();
        return keyboard->GetSnapshot().pressedKeys.empty();
    }));
}

TEST_F(WaylandMutter, APointerAppearsWithItsFirstEventAndClicksAndScrolls)
{
    IPlatformWindow& window = MakeFocusedWindow();
    input_->Move(3, 2);
    ASSERT_TRUE(PumpUntil([&] { return !SeenOf<MouseMotionEvent>().empty(); }, 5000ms))
        << "no pointer motion reached the window";
    EXPECT_EQ(SeenOf<MouseMotionEvent>().back().window, window.GetId());
    input_->Button(BTN_LEFT, true);
    input_->Button(BTN_LEFT, false);
    input_->Button(BTN_RIGHT, true);
    input_->Button(BTN_RIGHT, false);
    input_->WheelSteps(0, 1);
    ASSERT_TRUE(PumpUntil([&] { return SeenOf<MouseButtonEvent>().size() >= 4 && !SeenOf<MouseWheelEvent>().empty(); }));
    const std::vector<MouseButtonEvent> buttons = SeenOf<MouseButtonEvent>();
    EXPECT_EQ(buttons[0].button, 1);
    EXPECT_TRUE(buttons[0].pressed);
    EXPECT_EQ(buttons[2].button, 3);
    const MouseWheelEvent wheel = SeenOf<MouseWheelEvent>().front();
    EXPECT_FLOAT_EQ(std::abs(wheel.y), 1.0f) << "one discrete step is one notch";
    IPlatformMouse* mouse = platform_->GetMouse();
    mouse->Update();
    EXPECT_EQ(std::abs(mouse->GetSnapshot().scrollY), 120);
}

TEST_F(WaylandMutter, RelativeModeLocksThePointerAndReadsItsMotion)
{
    IPlatformWindow& window = MakeFocusedWindow();
    input_->Move(1, 1);
    ASSERT_TRUE(PumpUntil([&] { return !SeenOf<MouseMotionEvent>().empty(); }, 5000ms));
    IPlatformMouse* mouse = platform_->GetMouse();
    mouse->SetRelativeMode(window.GetId(), true);
    Draw(window);
    ASSERT_TRUE(PumpUntil([&] { return mouse->IsRelativeMode(); }));
    // GNOME activates a lock on the focused window under the pointer; give it the frame it needs.
    (void) PumpUntil([&] { return false; }, 200ms);
    (void) mouse->ConsumeRelativeDelta();
    for (int step = 0; step < 10; ++step)
    {
        input_->Move(4, -2);
    }
    MouseDelta total;
    ASSERT_TRUE(PumpUntil([&] {
        const MouseDelta delta = mouse->ConsumeRelativeDelta();
        total.x += delta.x;
        total.y += delta.y;
        return total.x >= 40;
    })) << "relative motion so far " << total.x << "," << total.y;
    EXPECT_EQ(total.x, 40) << "unaccelerated: exactly what the device reported";
    EXPECT_EQ(total.y, -20);
    mouse->SetRelativeMode(window.GetId(), false);
    Draw(window);
    EXPECT_FALSE(mouse->IsRelativeMode());
}

TEST_F(WaylandMutter, CnasOwnTitleBarClosesAndMaximizesUnderGnome)
{
    // GNOME draws no decorations for a Wayland client (no xdg-decoration), so the title bar is
    // CNA's: maximized, the window geometry is the whole virtual monitor and the bar is its top
    // 32 units, close button at the right end.
    WindowDescription description;
    description.title = "CNA frame test";
    windows_.push_back(platform_->CreateWindow(description));
    IPlatformWindow& window = *windows_.back();
    presenters_.push_back(platform_->CreateSurfacePresenter(window));
    window.Maximize();
    Draw(window);
    ASSERT_TRUE(PumpUntil([&] { return window.GetClientBounds().width == 1920; }, 5000ms))
        << "not maximized: " << window.GetClientBounds().width;
    Draw(window);
    // Park the pointer at the left edge, then at the top, then under the close button. Relative
    // motion stops at the monitor's edge, so this is exact wherever the pointer began. GNOME's
    // top bar is the monitor's first 32 units -- the window was configured with bounds 1048 high
    // -- so the maximized window, and its title bar, begin at y = 32.
    input_->Move(-5000, 0);
    input_->Move(1904, -5000);
    input_->Move(0, 32 + 16);
    (void) PumpUntil([&] { return false; }, 300ms);
    seen_.clear();
    input_->Button(BTN_LEFT, true);
    input_->Button(BTN_LEFT, false);
    ASSERT_TRUE(PumpUntil([&] {
        for (const WindowEvent& event : SeenOf<WindowEvent>())
        {
            if (event.kind == WindowEventKind::CloseRequested)
            {
                return true;
            }
        }
        return false;
    })) << "the close button did not close";
    EXPECT_FALSE(SeenOf<QuitEvent>().empty()) << "the only window's close is the application's quit request";
    EXPECT_TRUE(SeenOf<MouseButtonEvent>().empty()) << "a click on the frame is not the game's click";
}

TEST_F(WaylandMutter, FullscreenCoversTheMonitorWithoutATitleBar)
{
    IPlatformWindow& window = MakeFocusedWindow(true);
    ASSERT_TRUE(PumpUntil([&] { return window.GetFullscreenMode() == WindowFullscreenMode::BorderlessFullscreen; }));
    EXPECT_EQ(window.GetClientBounds().width, 1920);
    EXPECT_EQ(window.GetClientBounds().height, 1080);
    window.SetFullscreenMode(WindowFullscreenMode::Windowed);
    Draw(window);
    ASSERT_TRUE(PumpUntil([&] { return window.GetFullscreenMode() == WindowFullscreenMode::Windowed; }));
    EXPECT_EQ(window.GetClientBounds().width, 800);
}

// --- the clipboard, against an independent client ------------------------------------------------

/// wl-clipboard, from PATH or the unpacked copy beside the test Weston.
std::string WlClipboardTool(const char* name)
{
    for (const std::string& directory : {std::string(std::getenv("HOME") != nullptr ? std::getenv("HOME") : "") +
                                             "/deps/weston/bin",
                                         std::string("/usr/bin"), std::string("/usr/local/bin")})
    {
        const std::string candidate = directory + "/" + name;
        if (::access(candidate.c_str(), X_OK) == 0)
        {
            return candidate;
        }
    }
    return {};
}

TEST_F(WaylandMutter, TheClipboardIsSharedWithAnIndependentWaylandClient)
{
    const std::string copy = WlClipboardTool("wl-copy");
    const std::string paste = WlClipboardTool("wl-paste");
    if (copy.empty() || paste.empty())
    {
        GTEST_SKIP() << "wl-clipboard is not installed";
    }
    MakeFocusedWindow();
    IPlatformClipboard* clipboard = platform_->GetClipboard();
    ASSERT_NE(clipboard, nullptr);

    // Theirs to ours: wl-copy serves the selection from a background process of its own.
    const std::string theirs = "P\xc5\x99\xc3\xadli\xc5\xa1 \xc5\xbelu\xc5\xa5ou\xc4\x8dk\xc3\xbd k\xc5\xaf\xc5\x88";
    ASSERT_EQ(RunTool({copy, "--", theirs}, nullptr), 0);
    ASSERT_TRUE(PumpUntil([&] { return clipboard->GetText() == theirs; }, 5000ms))
        << "read: '" << clipboard->GetText() << "'";

    // Ours to theirs, with the game loop serving the transfer while wl-paste reads.
    const std::string ours = "from CNA \xe2\x80\x94 " + std::string(200000, 'x');
    clipboard->SetText(ours);
    Pump();
    std::string pasted;
    ASSERT_EQ(RunTool({paste, "--no-newline"}, &pasted), 0);
    EXPECT_EQ(pasted.size(), ours.size());
    EXPECT_TRUE(pasted == ours);
    // Clear the independent client's background server, whatever it still holds.
    (void) RunTool({copy, "--clear"}, nullptr);
}

// --- a Czech keyboard (the launcher's --layout cz) ---------------------------------------------------

TEST_F(WaylandMutter, CzechLayoutTypesCzech)
{
    const char* layout = std::getenv("CNA_WAYLAND_TEST_LAYOUT");
    if (layout == nullptr || std::string(layout) != "cz")
    {
        GTEST_SKIP() << "the compositor's layout is not Czech (run with --layout cz)";
    }
    IPlatformWindow& window = MakeFocusedWindow();
    platform_->GetTextInput()->Start(window.GetId(), TextInputType::Text);
    for (const std::uint32_t key : {KEY_2, KEY_3, KEY_4, KEY_5, KEY_Y, KEY_Z})
    {
        input_->Tap(key);
    }
    // AltGr+Q is a backslash on the Czech layout.
    input_->Key(KEY_RIGHTALT, true);
    input_->Tap(KEY_Q);
    input_->Key(KEY_RIGHTALT, false);
    ASSERT_TRUE(PumpUntil([&] { return TypedText().size() >= 11; })) << "typed so far: " << TypedText();
    EXPECT_EQ(TypedText(), "\xc4\x9b\xc5\xa1\xc4\x8d\xc5\x99zy\\");
    const std::vector<KeyEvent> keys = SeenOf<KeyEvent>();
    ASSERT_FALSE(keys.empty());
    EXPECT_EQ(keys.front().scancode, Scancode::D2);
    EXPECT_EQ(keys.front().keycode, KeyCode::D2);
}

// --- the input method (WAYLAND-0054) ------------------------------------------------------------
//
// Under Wayland an input method belongs to the compositor: the client speaks `text-input-v3` to
// gnome-shell, which speaks to an ibus daemon on its own session bus. These tests run against a
// real one -- `wayland_test_server.sh --compositor mutter --with-ibus hangul` starts gnome-shell
// with the Korean engine as its input source -- so what is checked is the whole path, not a
// protocol double: keys typed on the session's virtual keyboard, an engine that turns them into
// something else, and what a CNA game reads back. Hangul was chosen because its composition is
// deterministic (jamo combine into one syllable by rule, with no dictionary and no candidate
// list to choose from) and its engine is a native binary rather than a Python one.

/// The same session, with an input method in it. Its own suite so that a filter can ask for these
/// tests without also running the ones that type plain text -- which an input method would compose
/// into something else.
class WaylandIme : public WaylandMutter
{
};

/// Skips unless the launcher started an input method.
#define CNA_REQUIRE_IBUS(engine)                                                                              do                                                                                                        {                                                                                                             const char* configured = std::getenv("CNA_WAYLAND_TEST_IBUS");                                            if (configured == nullptr || std::string(configured) != (engine))                                         {                                                                                                             GTEST_SKIP() << "no " << (engine) << " input method (run with --with-ibus " << (engine) << ")";        }                                                                                                     } while (false)

TEST_F(WaylandIme, AnInputMethodComposesKoreanAndCommitsIt)
{
    CNA_REQUIRE_IBUS("hangul");
    IPlatformWindow& window = MakeFocusedWindow();
    platform_->GetTextInput()->Start(window.GetId(), TextInputType::Text);
    // d, k, s on a US keyboard are the jamo ㅇ, ㅏ, ㄴ, which the engine combines into 안 as they
    // are typed -- the preedit changing under the cursor with every key.
    for (const std::uint32_t key : {KEY_D, KEY_K, KEY_S})
    {
        input_->Tap(key);
        (void) PumpUntil([] { return false; }, 120ms);
    }
    const bool composed = PumpUntil([&] { return !SeenOf<TextEditingEvent>().empty(); }, 5000ms);
    const std::vector<TextEditingEvent> editing = SeenOf<TextEditingEvent>();
    ASSERT_TRUE(composed) << "the engine sent no preedit; typed text so far: " << TypedText();
    // Preedit is composition, not input: nothing is committed while it is being composed.
    EXPECT_EQ(editing.back().text, "\xec\x95\x88") << "the preedit is not the syllable the jamo compose into";
    EXPECT_EQ(editing.back().window, window.GetId());
    EXPECT_TRUE(TypedText().empty()) << "text was committed while the composition was still open: " << TypedText();

    // Enter commits what is being composed, and the commit is what a game reads as input.
    input_->Tap(KEY_ENTER);
    ASSERT_TRUE(PumpUntil([&] { return !TypedText().empty(); }, 5000ms)) << "the composition was never committed";
    EXPECT_EQ(TypedText(), "\xec\x95\x88") << "the committed text is not what was composed";
    // The composition is over: the last editing event clears the preedit.
    EXPECT_TRUE(SeenOf<TextEditingEvent>().back().text.empty())
        << "the preedit was left on screen after the commit: " << SeenOf<TextEditingEvent>().back().text;
}

TEST_F(WaylandIme, AGameThatAsksForNoTextStillGetsItsKeysWhileAnInputMethodRuns)
{
    CNA_REQUIRE_IBUS("hangul");
    IPlatformWindow& window = MakeFocusedWindow();
    // No Start(): a game reading WASD is not composing text, and the keys must reach it as keys
    // whatever input method the desktop has configured.
    input_->Tap(KEY_W);
    input_->Tap(KEY_A);
    ASSERT_TRUE(PumpUntil([&] { return SeenOf<KeyEvent>().size() >= 4; }, 5000ms));
    const std::vector<KeyEvent> keys = SeenOf<KeyEvent>();
    EXPECT_EQ(keys[0].scancode, Scancode::W);
    EXPECT_EQ(keys[0].keycode, KeyCode::W);
    EXPECT_TRUE(SeenOf<TextEditingEvent>().empty()) << "a game that asked for no text got a composition";
    EXPECT_EQ(window.GetId(), keys[0].window);
}

TEST_F(WaylandIme, StoppingTextInputEndsTheCompositionRatherThanLeavingItOpen)
{
    CNA_REQUIRE_IBUS("hangul");
    IPlatformWindow& window = MakeFocusedWindow();
    platform_->GetTextInput()->Start(window.GetId(), TextInputType::Text);
    for (const std::uint32_t key : {KEY_D, KEY_K})
    {
        input_->Tap(key);
        (void) PumpUntil([] { return false; }, 120ms);
    }
    ASSERT_TRUE(PumpUntil([&] { return !SeenOf<TextEditingEvent>().empty(); }, 5000ms))
        << "the engine sent no preedit";
    platform_->GetTextInput()->Stop(window.GetId());
    (void) PumpUntil([] { return false; }, 500ms);
    // Whatever the compositor does with the half-composed text, the game is not left showing a
    // preedit for an input it no longer asked for.
    const std::vector<TextEditingEvent> editing = SeenOf<TextEditingEvent>();
    EXPECT_TRUE(editing.back().text.empty())
        << "a preedit outlived the text input that asked for it: " << editing.back().text;
    EXPECT_FALSE(platform_->GetTextInput()->IsActive(window.GetId()));
}

} // namespace

#endif
