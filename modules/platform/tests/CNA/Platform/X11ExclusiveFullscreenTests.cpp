// SPDX-License-Identifier: MS-PL
//
// plans/plan_x11.md X11-0153: exclusive fullscreen against a real X server and a real window
// manager.
//
// These tests CHANGE THE DISPLAY MODE, so they run only on the private server
// `tools/platform/x11_test_server.sh` starts for them (CNA_X11_PRIVATE_TEST_SERVER=1) -- never on
// a desktop, whose monitors are its owner's. That server is an Xvfb with one RandR output and one
// 1280x1024 mode; the fixture adds 1024x768, 800x600 and 640x480 to it, as
// `xrandr --newmode/--addmode` would, so there is something to switch to. Everything the tests
// assert about the display is read back over a connection of the test's own, so it is the
// server's account, not the backend's.

#include <gtest/gtest.h>

#include "../../../src/X11/X11Headers.hpp"
#include "../../../src/X11/X11ModeGuardian.hpp"
#include "X11TestWindowManager.hpp"

#include "CNA/Platform/PlatformException.hpp"
#include "CNA/Platform/PlatformFactory.hpp"

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <functional>
#include <memory>
#include <poll.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <variant>
#include <vector>

extern char** environ;

namespace {

using namespace CNA::Platform;
using namespace CNA::Platform::X11::Testing;
using CNA::Platform::X11::kCurrentTime;
using CNA::Platform::X11::kNone;
using CNA::Platform::X11::kXFalse;

bool OnPrivateServer()
{
    const char* value = std::getenv("CNA_X11_PRIVATE_TEST_SERVER");
    return value != nullptr && std::string(value) == "1";
}

/// What the server says the display is doing.
struct DisplayState
{
    RRCrtc crtc = 0;
    RROutput output = 0;
    RRMode mode = 0;
    int crtcWidth = 0;
    int crtcHeight = 0;
    int rootWidth = 0;
    int rootHeight = 0;
};

/// A connection of the test's own, for reading the display and for playing "someone else".
class ServerObserver
{
public:
    ServerObserver() : display_(XOpenDisplay(nullptr)) {}
    ~ServerObserver()
    {
        if (display_ != nullptr) { XCloseDisplay(display_); }
    }
    ServerObserver(const ServerObserver&) = delete;
    ServerObserver& operator=(const ServerObserver&) = delete;

    [[nodiscard]] ::Display* Get() const { return display_; }
    [[nodiscard]] ::Window Root() const { return DefaultRootWindow(display_); }

    [[nodiscard]] bool HasRandr() const
    {
        int event = 0;
        int error = 0;
        int major = 0;
        int minor = 0;
        return XRRQueryExtension(display_, &event, &error) != 0 &&
               XRRQueryVersion(display_, &major, &minor) != 0 &&
               (major > 1 || (major == 1 && minor >= 2));
    }

    [[nodiscard]] DisplayState Read() const
    {
        DisplayState state;
        XRRScreenResources* resources = XRRGetScreenResourcesCurrent(display_, Root());
        if (resources != nullptr)
        {
            for (int index = 0; index < resources->ncrtc && state.crtc == 0; ++index)
            {
                XRRCrtcInfo* info = XRRGetCrtcInfo(display_, resources, resources->crtcs[index]);
                if (info != nullptr && info->mode != 0)
                {
                    state.crtc = resources->crtcs[index];
                    state.mode = info->mode;
                    state.crtcWidth = static_cast<int>(info->width);
                    state.crtcHeight = static_cast<int>(info->height);
                    state.output = info->noutput > 0 ? info->outputs[0] : 0;
                }
                if (info != nullptr) { XRRFreeCrtcInfo(info); }
            }
            XRRFreeScreenResources(resources);
        }
        ::Window rootReturn = kNone;
        int x = 0;
        int y = 0;
        unsigned int width = 0;
        unsigned int height = 0;
        unsigned int border = 0;
        unsigned int depth = 0;
        XGetGeometry(display_, Root(), &rootReturn, &x, &y, &width, &height, &border, &depth);
        state.rootWidth = static_cast<int>(width);
        state.rootHeight = static_cast<int>(height);
        return state;
    }

    /// Gives the output a mode, as `xrandr --newmode` + `--addmode` do. A mode an earlier test
    /// created is found by its name and reused -- the server refuses a second one (BadName).
    RRMode AddMode(const char* name, const int width, const int height, const double megahertz,
                   const int hSyncStart, const int hSyncEnd, const int hTotal, const int vSyncStart,
                   const int vSyncEnd, const int vTotal)
    {
        const DisplayState state = Read();
        if (XRRScreenResources* resources = XRRGetScreenResourcesCurrent(display_, Root()))
        {
            RRMode existing = 0;
            for (int index = 0; index < resources->nmode; ++index)
            {
                if (resources->modes[index].name != nullptr &&
                    std::strcmp(resources->modes[index].name, name) == 0)
                {
                    existing = resources->modes[index].id;
                }
            }
            XRRFreeScreenResources(resources);
            if (existing != 0)
            {
                return existing;
            }
        }
        XRRModeInfo* info = XRRAllocModeInfo(name, static_cast<int>(std::strlen(name)));
        info->width = static_cast<unsigned int>(width);
        info->height = static_cast<unsigned int>(height);
        info->dotClock = static_cast<unsigned long>(megahertz * 1'000'000.0);
        info->hSyncStart = static_cast<unsigned int>(hSyncStart);
        info->hSyncEnd = static_cast<unsigned int>(hSyncEnd);
        info->hTotal = static_cast<unsigned int>(hTotal);
        info->vSyncStart = static_cast<unsigned int>(vSyncStart);
        info->vSyncEnd = static_cast<unsigned int>(vSyncEnd);
        info->vTotal = static_cast<unsigned int>(vTotal);
        info->modeFlags = RR_HSyncNegative | RR_VSyncPositive;
        int (*previous)(::Display*, XErrorEvent*) =
            XSetErrorHandler([](::Display*, XErrorEvent*) { return 0; });
        const RRMode mode = XRRCreateMode(display_, Root(), info);
        if (mode != 0 && state.output != 0)
        {
            XRRAddOutputMode(display_, state.output, mode);
        }
        XSync(display_, kXFalse);
        XSetErrorHandler(previous);
        XRRFreeModeInfo(info);
        return mode;
    }

    /// Switches the monitor as `xrandr --output ... --mode` does: another client's change.
    bool SetMode(const RRMode mode, const int width, const int height)
    {
        const DisplayState state = Read();
        XRRScreenResources* resources = XRRGetScreenResourcesCurrent(display_, Root());
        if (resources == nullptr || state.crtc == 0)
        {
            if (resources != nullptr) { XRRFreeScreenResources(resources); }
            return false;
        }
        const int screen = DefaultScreen(display_);
        const auto millimetres = [this, screen](const int pixels, const bool horizontal) {
            return static_cast<int>(CNA::Platform::X11::MillimetresFor(
                pixels, horizontal ? DisplayWidth(display_, screen) : DisplayHeight(display_, screen),
                static_cast<std::uint32_t>(horizontal ? DisplayWidthMM(display_, screen)
                                                      : DisplayHeightMM(display_, screen))));
        };
        XGrabServer(display_);
        if (width > state.rootWidth || height > state.rootHeight)
        {
            XRRSetScreenSize(display_, Root(), std::max(width, state.rootWidth),
                             std::max(height, state.rootHeight),
                             millimetres(std::max(width, state.rootWidth), true),
                             millimetres(std::max(height, state.rootHeight), false));
        }
        RROutput output = state.output;
        const XStatusCompat status = XRRSetCrtcConfig(display_, resources, state.crtc, kCurrentTime,
                                                      0, 0, mode, RR_Rotate_0, &output, 1);
        XRRSetScreenSize(display_, Root(), width, height, millimetres(width, true),
                         millimetres(height, false));
        XUngrabServer(display_);
        XSync(display_, kXFalse);
        XRRFreeScreenResources(resources);
        return status == RRSetConfigSuccess;
    }

    /// Asks the window manager to change a window's _NET_WM_STATE, as a pager or a key binding
    /// does.
    void SetFullscreen(const ::Window window, const bool enabled)
    {
        XEvent event{};
        event.xclient.type = ClientMessage;
        event.xclient.window = window;
        event.xclient.message_type = XInternAtom(display_, "_NET_WM_STATE", kXFalse);
        event.xclient.format = 32;
        event.xclient.data.l[0] = enabled ? 1 : 0;
        event.xclient.data.l[1] =
            static_cast<long>(XInternAtom(display_, "_NET_WM_STATE_FULLSCREEN", kXFalse));
        event.xclient.data.l[3] = 2; // source indication: a pager, i.e. the user
        XSendEvent(display_, Root(), kXFalse, SubstructureRedirectMask | SubstructureNotifyMask,
                   &event);
        XSync(display_, kXFalse);
    }

    /// Activates a window as a taskbar does -- which is how the user switches to it.
    void Activate(const ::Window window)
    {
        XEvent event{};
        event.xclient.type = ClientMessage;
        event.xclient.window = window;
        event.xclient.message_type = XInternAtom(display_, "_NET_ACTIVE_WINDOW", kXFalse);
        event.xclient.format = 32;
        event.xclient.data.l[0] = 2; // source indication: a pager
        event.xclient.data.l[1] = 0;
        XSendEvent(display_, Root(), kXFalse, SubstructureRedirectMask | SubstructureNotifyMask,
                   &event);
        XSync(display_, kXFalse);
    }

private:
    // XRRSetCrtcConfig returns Xlib's Status, which X11Headers.hpp undefines as a macro.
    using XStatusCompat = int;

    ::Display* display_ = nullptr;
};

/// The mode guardians (X11ModeGuardian) this process has running: children named cna-x11-mode.
std::vector<pid_t> GuardiansOf(const pid_t parent)
{
    std::vector<pid_t> guardians;
    DIR* proc = opendir("/proc");
    if (proc == nullptr)
    {
        return guardians;
    }
    while (const dirent* entry = readdir(proc))
    {
        if (entry->d_name[0] < '0' || entry->d_name[0] > '9')
        {
            continue;
        }
        std::ifstream stat(std::string("/proc/") + entry->d_name + "/stat");
        std::string line;
        if (!std::getline(stat, line))
        {
            continue;
        }
        const std::size_t open = line.find('(');
        const std::size_t close = line.rfind(')');
        if (open == std::string::npos || close == std::string::npos)
        {
            continue;
        }
        const std::string name = line.substr(open + 1, close - open - 1);
        std::istringstream rest(line.substr(close + 2));
        char state = 0;
        pid_t ppid = 0;
        rest >> state >> ppid;
        // A zombie has exited; what is being asked is whether one is still running.
        if (name == "cna-x11-mode" && ppid == parent && state != 'Z')
        {
            guardians.push_back(static_cast<pid_t>(std::atoi(entry->d_name)));
        }
    }
    closedir(proc);
    return guardians;
}

class X11ExclusiveFullscreen : public ::testing::Test
{
protected:
    void SetUp() override
    {
        if (!HasDisplay())
        {
            GTEST_SKIP() << "no DISPLAY";
        }
        if (!OnPrivateServer())
        {
            GTEST_SKIP() << "these tests change the display mode, so they run only on the private "
                            "server tools/platform/x11_test_server.sh starts for them";
        }
        observer_ = std::make_unique<ServerObserver>();
        if (observer_->Get() == nullptr)
        {
            GTEST_SKIP() << "cannot reach the X server";
        }
        if (!observer_->HasRandr())
        {
            GTEST_SKIP() << "this X server has no RandR 1.2; there are no display modes to switch";
        }
        desktop_ = observer_->Read();
        ASSERT_NE(desktop_.crtc, 0u) << "the private server has no lit CRTC";
        mode1024_ = observer_->AddMode("cna-1024x768", 1024, 768, 63.50, 1072, 1176, 1328, 771,
                                       775, 798);
        mode800_ = observer_->AddMode("cna-800x600", 800, 600, 38.25, 832, 912, 1024, 603, 607, 624);
        mode640_ = observer_->AddMode("cna-640x480", 640, 480, 23.75, 664, 720, 800, 483, 487, 500);
        ASSERT_NE(mode800_, 0u) << "the server refused a mode to switch to";

        if (!ReadEwmhState().windowManager && !HasWindowManagerBinary())
        {
            GTEST_SKIP() << "no window manager runs on this display and openbox is not installed; "
                            "fullscreen is the window manager's to perform";
        }
        ASSERT_TRUE(SharedWindowManager::Instance().Available());

        platform_ = PlatformFactory::Create("X11");
        platform_->AcquireSubsystem(PlatformSubsystem::Video);
        acquired_ = true;
        ASSERT_TRUE(WaitForWindowManager()) << "no EWMH window manager advertising fullscreen";
    }

    void TearDown() override
    {
        window_.reset();
        if (acquired_)
        {
            platform_->ReleaseSubsystem(PlatformSubsystem::Video);
        }
        platform_.reset();
        // Whatever a failing test left behind, the next one starts from the desktop's mode.
        if (observer_ != nullptr && observer_->Get() != nullptr && desktop_.mode != 0 &&
            observer_->Read().mode != desktop_.mode)
        {
            observer_->SetMode(desktop_.mode, desktop_.crtcWidth, desktop_.crtcHeight);
        }
    }

    bool WaitForWindowManager()
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (platform_->GetCapabilities().borderlessFullscreen)
            {
                return true;
            }
            platform_->ReleaseSubsystem(PlatformSubsystem::Video);
            platform_ = PlatformFactory::Create("X11");
            platform_->AcquireSubsystem(PlatformSubsystem::Video);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        return false;
    }

    std::unique_ptr<IPlatformWindow> MakeWindow(const int width, const int height,
                                                const bool visible = true)
    {
        WindowDescription description;
        description.title = "CNA exclusive fullscreen test";
        description.width = width;
        description.height = height;
        description.visible = visible;
        auto window = platform_->CreateWindow(description);
        if (visible)
        {
            const WindowId id = window->GetId();
            // Managed before anything is asked of the window manager: a state request sent while
            // it is still reparenting the window is legitimately ignored.
            PumpUntil([id](const std::vector<PlatformEvent>& events) {
                return std::any_of(events.begin(), events.end(), [id](const PlatformEvent& event) {
                    const auto* windowEvent = std::get_if<WindowEvent>(&event);
                    return windowEvent != nullptr && windowEvent->window == id &&
                           (windowEvent->kind == WindowEventKind::Restored ||
                            windowEvent->kind == WindowEventKind::Exposed);
                });
            }, std::chrono::milliseconds(2000));
        }
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

    /// Pumps the application's events until the server shows the display in a state.
    bool DisplayBecomes(const std::function<bool(const DisplayState&)>& predicate,
                        const std::chrono::milliseconds budget = std::chrono::milliseconds(3000))
    {
        return PumpUntil([this, &predicate](const std::vector<PlatformEvent>&) {
            return predicate(observer_->Read());
        }, budget);
    }

    [[nodiscard]] bool AtDesktopMode(const DisplayState& state) const
    {
        return state.mode == desktop_.mode && state.rootWidth == desktop_.rootWidth &&
               state.rootHeight == desktop_.rootHeight;
    }

    static bool InMode(const DisplayState& state, const int width, const int height)
    {
        // The screen follows a single monitor's mode, so the pointer cannot leave what is shown.
        return state.crtcWidth == width && state.crtcHeight == height &&
               state.rootWidth == width && state.rootHeight == height;
    }

    /// Enters exclusive fullscreen and waits for the window manager to fit the window to the
    /// monitor.
    void EnterExclusive(const int width, const int height)
    {
        window_->SetFullscreenMode(WindowFullscreenMode::ExclusiveFullscreen);
        ASSERT_TRUE(PumpUntil([this, width, height](const std::vector<PlatformEvent>&) {
            const WindowBounds bounds = window_->GetClientBounds();
            return window_->GetFullscreenMode() == WindowFullscreenMode::ExclusiveFullscreen &&
                   bounds.x == 0 && bounds.y == 0 && bounds.width == width &&
                   bounds.height == height;
        })) << "the window never became an exclusive-fullscreen " << width << "x" << height
            << " window; it is " << window_->GetClientBounds().width << "x"
            << window_->GetClientBounds().height;
    }

    std::unique_ptr<ServerObserver> observer_;
    DisplayState desktop_;
    RRMode mode1024_ = 0;
    RRMode mode800_ = 0;
    RRMode mode640_ = 0;
    std::unique_ptr<IPlatform> platform_;
    std::unique_ptr<IPlatformWindow> window_;
    std::vector<PlatformEvent> seen_;
    bool acquired_ = false;
};

TEST_F(X11ExclusiveFullscreen, EnteringSwitchesTheMonitorToTheWindowsModeAndLeavingRestoresIt)
{
    window_ = MakeWindow(800, 600);
    ASSERT_TRUE(AtDesktopMode(observer_->Read()));

    EnterExclusive(800, 600);
    const DisplayState switched = observer_->Read();
    EXPECT_EQ(switched.mode, mode800_);
    EXPECT_TRUE(InMode(switched, 800, 600))
        << "the monitor is " << switched.crtcWidth << "x" << switched.crtcHeight
        << " on a screen of " << switched.rootWidth << "x" << switched.rootHeight;

    // The display service tells the two apart: the monitor shows 800x600 now, and the desktop
    // mode is still what the desktop gets back.
    IPlatformDisplays* displays = platform_->GetDisplays();
    ASSERT_NE(displays, nullptr);
    const std::vector<DisplayInfo> monitors = displays->GetDisplays();
    ASSERT_FALSE(monitors.empty());
    DisplayMode current;
    ASSERT_TRUE(displays->TryGetCurrentDisplayMode(monitors.front().id, current));
    EXPECT_EQ(current.width, 800);
    EXPECT_EQ(current.height, 600);
    EXPECT_EQ(monitors.front().desktopMode.width, desktop_.crtcWidth);
    EXPECT_EQ(monitors.front().desktopMode.height, desktop_.crtcHeight);

    window_->SetFullscreenMode(WindowFullscreenMode::Windowed);
    EXPECT_TRUE(AtDesktopMode(observer_->Read())) << "leaving did not restore the mode at once";
    EXPECT_TRUE(PumpUntil([this](const std::vector<PlatformEvent>&) {
        return window_->GetFullscreenMode() == WindowFullscreenMode::Windowed;
    }));
    ASSERT_TRUE(displays->TryGetCurrentDisplayMode(displays->GetDisplays().front().id, current));
    EXPECT_EQ(current.width, desktop_.crtcWidth);
}

TEST_F(X11ExclusiveFullscreen, AskedForBeforeTheFirstEventPumpItStillCoversTheMonitor)
{
    // The XNA start-up order: the window is shown, and IsFullScreen is applied while the game is
    // still setting up, before it has pumped an event. The mode switched and the window manager
    // never made the window fullscreen -- a decorated 800x600 window on an 800x600 monitor -- until
    // SetNetWmState stopped keying on "MapNotify seen" (X11WithWindowManager's test of the same).
    WindowDescription description;
    description.title = "CNA exclusive fullscreen test (before the first pump)";
    description.width = 800;
    description.height = 600;
    window_ = platform_->CreateWindow(description);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    window_->SetFullscreenMode(WindowFullscreenMode::ExclusiveFullscreen);
    EXPECT_EQ(observer_->Read().mode, mode800_);
    EXPECT_TRUE(PumpUntil([this](const std::vector<PlatformEvent>&) {
        const WindowBounds bounds = window_->GetClientBounds();
        return bounds.x == 0 && bounds.y == 0 && bounds.width == 800 && bounds.height == 600;
    })) << "the window does not cover the monitor; it is " << window_->GetClientBounds().width
        << "x" << window_->GetClientBounds().height << "+" << window_->GetClientBounds().x << "+"
        << window_->GetClientBounds().y;
}

TEST_F(X11ExclusiveFullscreen, ASizeBetweenModesGetsTheSmallestModeThatHoldsIt)
{
    window_ = MakeWindow(700, 500);
    EnterExclusive(800, 600);
    EXPECT_EQ(observer_->Read().mode, mode800_);
}

TEST_F(X11ExclusiveFullscreen, WithNoModeLargeEnoughItIsBorderlessAndSaysSo)
{
    // A back buffer larger than every mode the monitor has -- what GraphicsDevice asks for with
    // SetSize after IsFullScreen. (A window cannot simply be created that large: the window
    // manager fits it to the screen before fullscreen is ever asked for.) SDL's substitution:
    // fullscreen on the desktop's own mode -- and GetFullscreenMode reports that, not "exclusive".
    window_ = MakeWindow(800, 600);
    EnterExclusive(800, 600);
    window_->SetSize(1400, 1100);
    EXPECT_EQ(window_->GetFullscreenMode(), WindowFullscreenMode::BorderlessFullscreen);
    EXPECT_TRUE(AtDesktopMode(observer_->Read()));
    EXPECT_TRUE(GuardiansOf(getpid()).empty()) << "a guardian for a mode no longer changed";

    // And a size that fits again is exclusive again.
    window_->SetFullscreenMode(WindowFullscreenMode::ExclusiveFullscreen);
    window_->SetSize(1024, 768);
    EXPECT_EQ(observer_->Read().mode, mode1024_);
    EXPECT_EQ(window_->GetFullscreenMode(), WindowFullscreenMode::ExclusiveFullscreen);
}

TEST_F(X11ExclusiveFullscreen, ANewSizeWhileExclusiveIsANewDisplayMode)
{
    // What GraphicsDevice does after IsFullScreen: SetFullscreenMode, then SetSize with the back
    // buffer's size.
    window_ = MakeWindow(800, 600);
    EnterExclusive(800, 600);

    window_->SetSize(1024, 768);
    window_->Sync();
    EXPECT_EQ(observer_->Read().mode, mode1024_);
    EXPECT_TRUE(PumpUntil([this](const std::vector<PlatformEvent>&) {
        const WindowBounds bounds = window_->GetClientBounds();
        return bounds.width == 1024 && bounds.height == 768;
    })) << "the window manager did not fit the window to the new mode";
    EXPECT_EQ(window_->GetFullscreenMode(), WindowFullscreenMode::ExclusiveFullscreen);

    window_->SetSize(640, 480);
    EXPECT_EQ(observer_->Read().mode, mode640_);

    // And back out: the desktop's mode, not any of the ones in between.
    window_->SetFullscreenMode(WindowFullscreenMode::Windowed);
    EXPECT_TRUE(AtDesktopMode(observer_->Read()));
}

TEST_F(X11ExclusiveFullscreen, BorderlessAndExclusiveTradePlacesWithTheModeFollowing)
{
    window_ = MakeWindow(800, 600);
    window_->SetFullscreenMode(WindowFullscreenMode::BorderlessFullscreen);
    ASSERT_TRUE(PumpUntil([this](const std::vector<PlatformEvent>&) {
        return window_->GetFullscreenMode() == WindowFullscreenMode::BorderlessFullscreen;
    }));
    EXPECT_TRUE(AtDesktopMode(observer_->Read()));

    // The window is the monitor's size by now, so that is the size exclusive chooses for.
    window_->SetFullscreenMode(WindowFullscreenMode::ExclusiveFullscreen);
    EXPECT_EQ(window_->GetFullscreenMode(), WindowFullscreenMode::ExclusiveFullscreen);
    EXPECT_EQ(observer_->Read().mode, desktop_.mode);

    window_->SetSize(800, 600);
    EXPECT_EQ(observer_->Read().mode, mode800_);

    window_->SetFullscreenMode(WindowFullscreenMode::BorderlessFullscreen);
    EXPECT_TRUE(AtDesktopMode(observer_->Read()));
    EXPECT_EQ(window_->GetFullscreenMode(), WindowFullscreenMode::BorderlessFullscreen);
}

TEST_F(X11ExclusiveFullscreen, DestroyingTheWindowGivesTheModeBack)
{
    window_ = MakeWindow(800, 600);
    EnterExclusive(800, 600);
    ASSERT_EQ(observer_->Read().mode, mode800_);

    window_.reset();
    EXPECT_TRUE(AtDesktopMode(observer_->Read()));
}

TEST_F(X11ExclusiveFullscreen, AWindowAnotherClientDestroysGivesTheModeBack)
{
    window_ = MakeWindow(800, 600);
    EnterExclusive(800, 600);
    const auto xid = static_cast<::Window>(window_->GetWindowHandle());

    int (*previous)(::Display*, XErrorEvent*) =
        XSetErrorHandler([](::Display*, XErrorEvent*) { return 0; });
    XDestroyWindow(observer_->Get(), xid);
    XSync(observer_->Get(), kXFalse);
    XSetErrorHandler(previous);

    EXPECT_TRUE(DisplayBecomes([this](const DisplayState& state) { return AtDesktopMode(state); }))
        << "the mode stayed switched for a window that no longer exists";
}

TEST_F(X11ExclusiveFullscreen, TearingThePlatformDownGivesTheModeBack)
{
    window_ = MakeWindow(800, 600);
    EnterExclusive(800, 600);
    ASSERT_EQ(observer_->Read().mode, mode800_);

    window_.reset();
    platform_->ReleaseSubsystem(PlatformSubsystem::Video);
    acquired_ = false;
    platform_.reset();
    EXPECT_TRUE(AtDesktopMode(observer_->Read()));
    EXPECT_TRUE(GuardiansOf(getpid()).empty()) << "the guardian outlived the mode it guarded";
}

TEST_F(X11ExclusiveFullscreen, AHiddenWindowHoldsTheModeOnlyWhileShown)
{
    window_ = MakeWindow(800, 600, false);
    window_->SetFullscreenMode(WindowFullscreenMode::ExclusiveFullscreen);
    // Nothing on screen yet, so nothing changed -- but the window's mode is exclusive.
    EXPECT_TRUE(AtDesktopMode(observer_->Read()));
    EXPECT_EQ(window_->GetFullscreenMode(), WindowFullscreenMode::ExclusiveFullscreen);

    window_->Show();
    EXPECT_EQ(observer_->Read().mode, mode800_);
    EXPECT_TRUE(PumpUntil([this](const std::vector<PlatformEvent>&) {
        const WindowBounds bounds = window_->GetClientBounds();
        return bounds.width == 800 && bounds.height == 600;
    }));

    window_->Hide();
    EXPECT_TRUE(AtDesktopMode(observer_->Read()));
    window_->Show();
    EXPECT_EQ(observer_->Read().mode, mode800_);
}

TEST_F(X11ExclusiveFullscreen, LosingFocusMinimisesAndGivesTheModeBackAndFocusTakesItAgain)
{
    window_ = MakeWindow(800, 600);
    EnterExclusive(800, 600);
    ASSERT_TRUE(PumpUntil([this](const std::vector<PlatformEvent>&) { return window_->HasFocus(); }))
        << "the window manager never focused the window";

    // Another application, which the user switches to.
    ::Display* other = observer_->Get();
    XSetWindowAttributes attributes{};
    const ::Window otherWindow =
        XCreateWindow(other, observer_->Root(), 0, 0, 200, 150, 0, CopyFromParent, InputOutput,
                      nullptr, 0, &attributes);
    XMapWindow(other, otherWindow);
    XSync(other, kXFalse);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    observer_->Activate(otherWindow);

    EXPECT_TRUE(DisplayBecomes([this](const DisplayState& state) { return AtDesktopMode(state); }))
        << "the desktop did not get its mode back when the game lost focus";
    EXPECT_TRUE(PumpUntil([this](const std::vector<PlatformEvent>&) {
        return window_->IsMinimized();
    })) << "the game did not minimise";
    EXPECT_EQ(window_->GetFullscreenMode(), WindowFullscreenMode::ExclusiveFullscreen)
        << "a minimised exclusive window is still an exclusive window";

    // The user switches back.
    observer_->Activate(static_cast<::Window>(window_->GetWindowHandle()));
    EXPECT_TRUE(DisplayBecomes([this](const DisplayState& state) { return state.mode == mode800_; }))
        << "the game did not take its mode again with focus";
    EXPECT_TRUE(PumpUntil([this](const std::vector<PlatformEvent>&) {
        return !window_->IsMinimized() && window_->HasFocus();
    }));

    XDestroyWindow(other, otherWindow);
    XSync(other, kXFalse);
}

TEST_F(X11ExclusiveFullscreen, TakenOutOfFullscreenFromOutsideItGivesTheModeBack)
{
    window_ = MakeWindow(800, 600);
    EnterExclusive(800, 600);

    // A window-manager key binding or a pager.
    observer_->SetFullscreen(static_cast<::Window>(window_->GetWindowHandle()), false);
    EXPECT_TRUE(DisplayBecomes([this](const DisplayState& state) { return AtDesktopMode(state); }))
        << "the mode stayed switched for a window that is not fullscreen any more";
    EXPECT_EQ(window_->GetFullscreenMode(), WindowFullscreenMode::Windowed);
}

TEST_F(X11ExclusiveFullscreen, AModeSomeoneElseSetIsNotOverruledOnLeaving)
{
    window_ = MakeWindow(800, 600);
    EnterExclusive(800, 600);

    // The user changes the resolution while the game runs.
    ASSERT_TRUE(observer_->SetMode(mode1024_, 1024, 768));
    window_->SetFullscreenMode(WindowFullscreenMode::Windowed);
    const DisplayState after = observer_->Read();
    EXPECT_EQ(after.mode, mode1024_) << "leaving fullscreen overruled the user's own choice";
}

TEST_F(X11ExclusiveFullscreen, AGuardianRunsExactlyWhileTheModeIsChanged)
{
    window_ = MakeWindow(800, 600);
    EXPECT_TRUE(GuardiansOf(getpid()).empty());
    EnterExclusive(800, 600);
    EXPECT_EQ(GuardiansOf(getpid()).size(), 1u) << "no guardian for a changed mode";

    // A second mode change is the same guardian, told the new mode.
    window_->SetSize(1024, 768);
    EXPECT_EQ(GuardiansOf(getpid()).size(), 1u);

    window_->SetFullscreenMode(WindowFullscreenMode::Windowed);
    EXPECT_TRUE(GuardiansOf(getpid()).empty()) << "the guardian outlived the mode it guarded";
}

// --- the guardian, across a process that dies ------------------------------------------------------

/// Starts this test binary again, running only the helper below, and returns the descriptor it
/// reports on.
pid_t SpawnHelper(int& report)
{
    int pipeFds[2] = {-1, -1};
    if (::pipe2(pipeFds, O_CLOEXEC) != 0)
    {
        return -1;
    }
    std::vector<std::string> environment;
    for (char** entry = environ; *entry != nullptr; ++entry)
    {
        environment.emplace_back(*entry);
    }
    environment.emplace_back("CNA_X11_EXCLUSIVE_HELPER_FD=99");
    std::vector<char*> environmentPointers;
    for (std::string& entry : environment)
    {
        environmentPointers.push_back(entry.data());
    }
    environmentPointers.push_back(nullptr);
    std::string executable = "/proc/self/exe";
    std::string filter =
        "--gtest_filter=X11ExclusiveFullscreenHelper.DISABLED_HoldAnExclusiveModeUntilKilled";
    std::string disabled = "--gtest_also_run_disabled_tests";
    std::vector<char*> arguments = {executable.data(), filter.data(), disabled.data(), nullptr};

    const pid_t pid = ::fork();
    if (pid == 0)
    {
        ::dup2(pipeFds[1], 99);
        const int devNull = ::open("/dev/null", O_WRONLY);
        if (devNull >= 0)
        {
            ::dup2(devNull, 1);
            ::dup2(devNull, 2);
        }
        ::execve(executable.c_str(), arguments.data(), environmentPointers.data());
        ::_exit(127);
    }
    ::close(pipeFds[1]);
    report = pipeFds[0];
    return pid;
}

bool AwaitReport(const int report, const std::chrono::milliseconds budget)
{
    pollfd readable{report, POLLIN, 0};
    if (::poll(&readable, 1, static_cast<int>(budget.count())) <= 0)
    {
        return false;
    }
    char byte = 0;
    return ::read(report, &byte, 1) == 1 && byte == '1';
}

TEST_F(X11ExclusiveFullscreen, AKilledProcessHasItsModeRestoredByTheGuardian)
{
    int report = -1;
    const pid_t helper = SpawnHelper(report);
    ASSERT_GT(helper, 0);
    const bool switched = AwaitReport(report, std::chrono::seconds(20));
    ::close(report);
    if (!switched)
    {
        ::kill(helper, SIGKILL);
        ::waitpid(helper, nullptr, 0);
        FAIL() << "the helper process never reached exclusive fullscreen";
    }
    ASSERT_EQ(observer_->Read().mode, mode800_);

    // SIGKILL: no destructor, no atexit, no signal handler -- nothing in the process runs.
    ::kill(helper, SIGKILL);
    ::waitpid(helper, nullptr, 0);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline && !AtDesktopMode(observer_->Read()))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    const DisplayState after = observer_->Read();
    EXPECT_TRUE(AtDesktopMode(after))
        << "the display was left at " << after.crtcWidth << "x" << after.crtcHeight
        << " by a process that died in exclusive fullscreen";
}

TEST_F(X11ExclusiveFullscreen, TheGuardianLeavesAModeSomeoneElseSetAlone)
{
    int report = -1;
    const pid_t helper = SpawnHelper(report);
    ASSERT_GT(helper, 0);
    const bool switched = AwaitReport(report, std::chrono::seconds(20));
    ::close(report);
    if (!switched)
    {
        ::kill(helper, SIGKILL);
        ::waitpid(helper, nullptr, 0);
        FAIL() << "the helper process never reached exclusive fullscreen";
    }

    // The user changed the resolution while the game ran; then the game died.
    ASSERT_TRUE(observer_->SetMode(mode1024_, 1024, 768));
    ::kill(helper, SIGKILL);
    ::waitpid(helper, nullptr, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    EXPECT_EQ(observer_->Read().mode, mode1024_)
        << "the guardian overruled a mode the game had not set";
}

TEST(X11ExclusiveFullscreenHelper, DISABLED_HoldAnExclusiveModeUntilKilled)
{
    // Run only by SpawnHelper, in a process of its own, which the test then kills.
    const char* reportText = std::getenv("CNA_X11_EXCLUSIVE_HELPER_FD");
    if (reportText == nullptr)
    {
        GTEST_SKIP() << "run by X11ExclusiveFullscreen's guardian tests";
    }
    const int report = std::atoi(reportText);
    auto platform = PlatformFactory::Create("X11");
    platform->AcquireSubsystem(PlatformSubsystem::Video);
    WindowDescription description;
    description.title = "CNA exclusive fullscreen helper";
    description.width = 800;
    description.height = 600;
    auto window = platform->CreateWindow(description);

    std::vector<PlatformEvent> batch;
    const auto pumpFor = [&](const std::chrono::milliseconds period,
                             const std::function<bool()>& done) {
        const auto deadline = std::chrono::steady_clock::now() + period;
        while (std::chrono::steady_clock::now() < deadline)
        {
            platform->PollEvents(batch);
            if (done()) { return true; }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return false;
    };
    (void) pumpFor(std::chrono::milliseconds(500), [] { return false; });
    window->SetFullscreenMode(WindowFullscreenMode::ExclusiveFullscreen);
    ASSERT_TRUE(pumpFor(std::chrono::seconds(5), [&] {
        return window->GetFullscreenMode() == WindowFullscreenMode::ExclusiveFullscreen;
    }));
    const char switched = '1';
    ASSERT_EQ(::write(report, &switched, 1), 1);
    for (;;)
    {
        platform->PollEvents(batch);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

// --- the guardian's own protocol, in-process ------------------------------------------------------

TEST_F(X11ExclusiveFullscreen, TheGuardiansRawRestoreUndoesASwitchAndLeavesOthersAlone)
{
    using namespace CNA::Platform::X11;
    ::Display* display = observer_->Get();

    X11ModeRestorePlan plan;
    socklen_t length = sizeof(plan.address);
    ASSERT_EQ(::getpeername(ConnectionNumber(display), reinterpret_cast<sockaddr*>(&plan.address),
                            &length),
              0);
    plan.addressLength = length;
    // The launcher's Xvfb asks for no cookie.
    plan.setupLength = EncodeConnectionSetup(plan.setup, sizeof(plan.setup), nullptr, 0, nullptr, 0);
    int opcode = 0;
    int event = 0;
    int error = 0;
    ASSERT_NE(XQueryExtension(display, "RANDR", &opcode, &event, &error), 0);
    plan.randrOpcode = static_cast<std::uint8_t>(opcode);
    plan.root = static_cast<std::uint32_t>(observer_->Root());
    plan.crtc = static_cast<std::uint32_t>(desktop_.crtc);
    plan.appliedMode = static_cast<std::uint32_t>(mode800_);
    plan.originalMode = static_cast<std::uint32_t>(desktop_.mode);
    plan.originalWidth = static_cast<std::uint16_t>(desktop_.crtcWidth);
    plan.originalHeight = static_cast<std::uint16_t>(desktop_.crtcHeight);
    plan.originalRotation = RR_Rotate_0;
    plan.outputCount = 1;
    plan.outputs[0] = static_cast<std::uint32_t>(desktop_.output);
    plan.screenWidth = static_cast<std::uint16_t>(desktop_.rootWidth);
    plan.screenHeight = static_cast<std::uint16_t>(desktop_.rootHeight);
    plan.screenWidthMm = static_cast<std::uint32_t>(DisplayWidthMM(display, DefaultScreen(display)));
    plan.screenHeightMm =
        static_cast<std::uint32_t>(DisplayHeightMM(display, DefaultScreen(display)));
    int minimumWidth = 0;
    int minimumHeight = 0;
    int maximumWidth = 0;
    int maximumHeight = 0;
    XRRGetScreenSizeRange(display, observer_->Root(), &minimumWidth, &minimumHeight, &maximumWidth,
                          &maximumHeight);
    plan.minimumWidth = static_cast<std::uint16_t>(minimumWidth);
    plan.minimumHeight = static_cast<std::uint16_t>(minimumHeight);
    plan.maximumWidth = static_cast<std::uint16_t>(maximumWidth);
    plan.maximumHeight = static_cast<std::uint16_t>(maximumHeight);

    std::vector<unsigned char> scratch(64 * 1024);
    ASSERT_TRUE(observer_->SetMode(mode800_, 800, 600));
    EXPECT_EQ(RestoreDisplayModeRaw(plan, scratch.data(), scratch.size()),
              X11ModeRestoreResult::Restored);
    const DisplayState restored = observer_->Read();
    EXPECT_TRUE(AtDesktopMode(restored))
        << "restored to " << restored.crtcWidth << "x" << restored.crtcHeight << " on "
        << restored.rootWidth << "x" << restored.rootHeight;

    ASSERT_TRUE(observer_->SetMode(mode1024_, 1024, 768));
    EXPECT_EQ(RestoreDisplayModeRaw(plan, scratch.data(), scratch.size()),
              X11ModeRestoreResult::LeftAlone);
    EXPECT_EQ(observer_->Read().mode, mode1024_);
}

} // namespace
