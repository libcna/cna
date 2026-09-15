// SPDX-License-Identifier: MS-PL
//
// plans/plan_x11.md X11-0155: touchscreen contacts through XInput 2.2.
//
// Three layers. The arithmetic -- finger ids, normalisation, deltas -- needs no server. The
// selection needs any XInput 2.2 server, which the launcher's Xvfb is. And the contacts need a
// real touch device, which Xvfb has none of: X11Touchscreen creates one through uinput and a
// private, rootless Xorg (the dummy video driver, the evdev input driver) that reads it.
//
// ### Why the touchscreen suite is safe to run on a desktop -- and opt-in all the same
//
// A uinput touchscreen is a device of the whole machine: the desktop's own compositor sees it
// appear as well, and a touch it received would reach the desktop -- a locked screen included.
// So the private Xorg is told to take the device EXCLUSIVELY (evdev's GrabDevice, EVIOCGRAB,
// which the kernel enforces: while it holds the grab, no other reader gets a single event), and
// the suite injects nothing until it has verified that someone holds that grab: its own
// EVIOCGRAB attempt must fail with EBUSY. It re-verifies before every event it writes. Where the
// grab is not held, it skips, having injected nothing. It still runs only where
// CNA_X11_TEST_TOUCHSCREEN=1 (the CnaX11TouchscreenTests entry), because it adds a device the
// desktop can see and a server process to the machine.

#include <gtest/gtest.h>

#include "../../../src/X11/X11Headers.hpp"
#include "../../../src/X11/X11Touch.hpp"

#include "CNA/Platform/Input/IPlatformInputDevices.hpp"
#include "CNA/Platform/Input/IPlatformMouse.hpp"
#include "CNA/Platform/PlatformException.hpp"
#include "CNA/Platform/PlatformFactory.hpp"
#include "System/Environment.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <functional>
#include <linux/uinput.h>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <variant>
#include <vector>

namespace {

using namespace CNA::Platform;
using CNA::Platform::X11::MakeFingerId;
using CNA::Platform::X11::MakeTouchEvent;
using CNA::Platform::X11::kXFalse;

// --- the arithmetic -------------------------------------------------------------------------------------

TEST(X11TouchMath, FingerIdsKeepDevicesApart)
{
    // XI2 numbers touches per device: two touchscreens' first touches are both touch 1.
    EXPECT_NE(MakeFingerId(6, 1), MakeFingerId(7, 1));
    EXPECT_NE(MakeFingerId(6, 1), MakeFingerId(6, 2));
    EXPECT_EQ(MakeFingerId(6, 1), MakeFingerId(6, 1));
}

TEST(X11TouchMath, PositionsAreNormalisedAgainstTheClientSizeAndComeBackExactly)
{
    const TouchEvent touch = MakeTouchEvent(3, 9, TouchEventKind::Down, 160.0, 60.0, 320, 240,
                                            std::nullopt);
    EXPECT_EQ(touch.window, 3u);
    EXPECT_EQ(touch.fingerId, 9u);
    EXPECT_FLOAT_EQ(touch.x, 0.5f);
    EXPECT_FLOAT_EQ(touch.y, 0.25f);
    EXPECT_EQ(touch.clientWidth, 320);
    EXPECT_EQ(touch.clientHeight, 240);
    // The input bridge multiplies by the client size: that must land on the pixel touched.
    EXPECT_FLOAT_EQ(touch.x * static_cast<float>(touch.clientWidth), 160.0f);
    EXPECT_FLOAT_EQ(touch.pressure, 1.0f);
}

TEST(X11TouchMath, OnlyAMotionCarriesADelta)
{
    const auto previous = std::make_optional(std::make_pair(0.5f, 0.5f));
    const TouchEvent moved =
        MakeTouchEvent(1, 1, TouchEventKind::Motion, 192.0, 120.0, 320, 240, previous);
    EXPECT_NEAR(moved.deltaX, 0.1f, 1e-6f);
    EXPECT_NEAR(moved.deltaY, 0.0f, 1e-6f);
    const TouchEvent lifted =
        MakeTouchEvent(1, 1, TouchEventKind::Up, 192.0, 120.0, 320, 240, previous);
    EXPECT_EQ(lifted.deltaX, 0.0f);
    EXPECT_EQ(lifted.deltaY, 0.0f);
}

TEST(X11TouchMath, ADegenerateClientSizeDividesByOne)
{
    const TouchEvent touch = MakeTouchEvent(1, 1, TouchEventKind::Down, 0.0, 0.0, 0, -5,
                                            std::nullopt);
    EXPECT_EQ(touch.clientWidth, 1);
    EXPECT_EQ(touch.clientHeight, 1);
    EXPECT_EQ(touch.x, 0.0f);
}

// --- the selection, on any XInput 2.2 server ------------------------------------------------------------

TEST(X11TouchSelection, EveryWindowSelectsTouchEventsWhereTheServerSpeaksXInputTwoPointTwo)
{
    const char* privateServer = std::getenv("CNA_X11_PRIVATE_TEST_SERVER");
    if (privateServer == nullptr || std::string(privateServer) != "1")
    {
        GTEST_SKIP() << "needs tools/platform/x11_test_server.sh";
    }
    auto platform = PlatformFactory::Create("X11");
    platform->AcquireSubsystem(PlatformSubsystem::Video);
    WindowDescription description;
    description.title = "CNA touch selection";
    description.width = 64;
    description.height = 64;
    auto window = platform->CreateWindow(description);

    ::Display* display = XOpenDisplay(nullptr);
    ASSERT_NE(display, nullptr);
    int opcode = 0;
    int event = 0;
    int error = 0;
    ASSERT_TRUE(XQueryExtension(display, "XInputExtension", &opcode, &event, &error));
    int major = 2;
    int minor = 2;
    ASSERT_EQ(XIQueryVersion(display, &major, &minor), Success);
    if (major == 2 && minor < 2)
    {
        XCloseDisplay(display);
        GTEST_SKIP() << "this server speaks XInput " << major << "." << minor << " only";
    }
    XCloseDisplay(display);
    // A selection is per client, and XIGetSelectedEvents answers for the client that asks: the
    // question has to go over the platform's own connection, which the native handle carries.
    display = static_cast<::Display*>(window->GetNativeHandle().display);
    ASSERT_NE(display, nullptr);
    int count = 0;
    XIEventMask* masks =
        XIGetSelectedEvents(display, static_cast<::Window>(window->GetWindowHandle()), &count);
    bool touch = false;
    for (int index = 0; index < count; ++index)
    {
        if (masks[index].deviceid == XIAllMasterDevices &&
            masks[index].mask_len >= XIMaskLen(XI_TouchEnd) &&
            XIMaskIsSet(masks[index].mask, XI_TouchBegin) &&
            XIMaskIsSet(masks[index].mask, XI_TouchUpdate) &&
            XIMaskIsSet(masks[index].mask, XI_TouchEnd))
        {
            touch = true;
        }
    }
    if (masks != nullptr) { XFree(masks); }
    EXPECT_TRUE(touch) << "the window does not select XI_TouchBegin/Update/End";
    window.reset();
    platform->ReleaseSubsystem(PlatformSubsystem::Video);
}

// --- real contacts: a uinput touchscreen and a private Xorg -------------------------------------------

/// Device coordinates: 0..4095 on both axes, which the evdev driver maps over the whole screen.
constexpr int kAxisMaximum = 4095;
constexpr int kScreenWidth = 1280;
constexpr int kScreenHeight = 1024;

int DeviceX(const int screenX) { return screenX * (kAxisMaximum + 1) / kScreenWidth; }
int DeviceY(const int screenY) { return screenY * (kAxisMaximum + 1) / kScreenHeight; }

/// True only while another open file holds the device exclusively -- the one condition under
/// which an injected event cannot reach anything but its holder.
bool HeldExclusivelyByAnother(const std::string& node)
{
    const int fd = ::open(node.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0)
    {
        return false;
    }
    if (::ioctl(fd, EVIOCGRAB, 1) == 0)
    {
        ::ioctl(fd, EVIOCGRAB, 0);
        ::close(fd);
        return false;
    }
    const int error = errno;
    ::close(fd);
    return error == EBUSY;
}

class VirtualTouchscreen
{
public:
    bool Create()
    {
        fd_ = ::open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd_ < 0)
        {
            return false;
        }
        ::ioctl(fd_, UI_SET_EVBIT, EV_KEY);
        ::ioctl(fd_, UI_SET_KEYBIT, BTN_TOUCH);
        ::ioctl(fd_, UI_SET_EVBIT, EV_ABS);
        for (const int code : {ABS_X, ABS_Y, ABS_MT_SLOT, ABS_MT_TRACKING_ID, ABS_MT_POSITION_X,
                               ABS_MT_POSITION_Y})
        {
            ::ioctl(fd_, UI_SET_ABSBIT, code);
        }
        ::ioctl(fd_, UI_SET_PROPBIT, INPUT_PROP_DIRECT);
        const auto axis = [this](const int code, const int maximum) {
            uinput_abs_setup setup{};
            setup.code = static_cast<__u16>(code);
            setup.absinfo.minimum = 0;
            setup.absinfo.maximum = maximum;
            setup.absinfo.resolution = 10;
            ::ioctl(fd_, UI_ABS_SETUP, &setup);
        };
        axis(ABS_X, kAxisMaximum);
        axis(ABS_Y, kAxisMaximum);
        axis(ABS_MT_POSITION_X, kAxisMaximum);
        axis(ABS_MT_POSITION_Y, kAxisMaximum);
        axis(ABS_MT_SLOT, 9);
        axis(ABS_MT_TRACKING_ID, 65535);
        uinput_setup setup{};
        setup.id.bustype = BUS_VIRTUAL;
        setup.id.vendor = 0x1d6b;
        setup.id.product = 0xc0de;
        std::strncpy(setup.name, "CNA Test Touchscreen", sizeof(setup.name) - 1);
        if (::ioctl(fd_, UI_DEV_SETUP, &setup) != 0 || ::ioctl(fd_, UI_DEV_CREATE) != 0)
        {
            return false;
        }
        char sysname[64] = {};
        if (::ioctl(fd_, UI_GET_SYSNAME(sizeof(sysname)), sysname) < 0)
        {
            return false;
        }
        const std::string directory = std::string("/sys/devices/virtual/input/") + sysname;
        if (DIR* entries = ::opendir(directory.c_str()))
        {
            while (const dirent* entry = ::readdir(entries))
            {
                if (std::strncmp(entry->d_name, "event", 5) == 0)
                {
                    node_ = std::string("/dev/input/") + entry->d_name;
                }
            }
            ::closedir(entries);
        }
        return !node_.empty();
    }

    ~VirtualTouchscreen()
    {
        if (fd_ >= 0)
        {
            ::ioctl(fd_, UI_DEV_DESTROY);
            ::close(fd_);
        }
    }

    [[nodiscard]] const std::string& Node() const { return node_; }

    // Multitouch protocol B. Each step writes nothing unless the private server still holds the
    // device exclusively; a test that finds it does not fails without having injected.
    [[nodiscard]] bool Down(const int slot, const int id, const int screenX, const int screenY)
    {
        return Frame({{EV_ABS, ABS_MT_SLOT, slot},
                      {EV_ABS, ABS_MT_TRACKING_ID, id},
                      {EV_ABS, ABS_MT_POSITION_X, DeviceX(screenX)},
                      {EV_ABS, ABS_MT_POSITION_Y, DeviceY(screenY)},
                      {EV_ABS, ABS_X, DeviceX(screenX)},
                      {EV_ABS, ABS_Y, DeviceY(screenY)},
                      {EV_KEY, BTN_TOUCH, 1}});
    }

    [[nodiscard]] bool Move(const int slot, const int screenX, const int screenY)
    {
        return Frame({{EV_ABS, ABS_MT_SLOT, slot},
                      {EV_ABS, ABS_MT_POSITION_X, DeviceX(screenX)},
                      {EV_ABS, ABS_MT_POSITION_Y, DeviceY(screenY)},
                      {EV_ABS, ABS_X, DeviceX(screenX)},
                      {EV_ABS, ABS_Y, DeviceY(screenY)}});
    }

    [[nodiscard]] bool Up(const int slot, const bool last)
    {
        std::vector<std::array<int, 3>> events = {{EV_ABS, ABS_MT_SLOT, slot},
                                                  {EV_ABS, ABS_MT_TRACKING_ID, -1}};
        if (last)
        {
            events.push_back({EV_KEY, BTN_TOUCH, 0});
        }
        return Frame(events);
    }

private:
    bool Frame(const std::vector<std::array<int, 3>>& events)
    {
        if (!HeldExclusivelyByAnother(node_))
        {
            return false;
        }
        for (const auto& [type, code, value] : events)
        {
            Emit(type, code, value);
        }
        Emit(EV_SYN, SYN_REPORT, 0);
        return true;
    }

    void Emit(const int type, const int code, const int value) const
    {
        input_event event{};
        event.type = static_cast<__u16>(type);
        event.code = static_cast<__u16>(code);
        event.value = value;
        (void) ::write(fd_, &event, sizeof(event));
    }

    int fd_ = -1;
    std::string node_;
};

/// A display tablet's stylus: position, pressure (0..1023), the tip and proximity.
class VirtualPen
{
public:
    static constexpr int kPressureMaximum = 1023;

    bool Create()
    {
        fd_ = ::open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd_ < 0)
        {
            return false;
        }
        ::ioctl(fd_, UI_SET_EVBIT, EV_KEY);
        ::ioctl(fd_, UI_SET_KEYBIT, BTN_TOOL_PEN);
        ::ioctl(fd_, UI_SET_KEYBIT, BTN_TOUCH);
        ::ioctl(fd_, UI_SET_KEYBIT, BTN_STYLUS);
        ::ioctl(fd_, UI_SET_EVBIT, EV_ABS);
        for (const int code : {ABS_X, ABS_Y, ABS_PRESSURE})
        {
            ::ioctl(fd_, UI_SET_ABSBIT, code);
        }
        ::ioctl(fd_, UI_SET_PROPBIT, INPUT_PROP_DIRECT);
        const auto axis = [this](const int code, const int maximum) {
            uinput_abs_setup setup{};
            setup.code = static_cast<__u16>(code);
            setup.absinfo.maximum = maximum;
            setup.absinfo.resolution = 10;
            ::ioctl(fd_, UI_ABS_SETUP, &setup);
        };
        axis(ABS_X, kAxisMaximum);
        axis(ABS_Y, kAxisMaximum);
        axis(ABS_PRESSURE, kPressureMaximum);
        uinput_setup setup{};
        setup.id.bustype = BUS_VIRTUAL;
        setup.id.vendor = 0x1d6b;
        setup.id.product = 0xc0df;
        std::strncpy(setup.name, "CNA Test Pen", sizeof(setup.name) - 1);
        if (::ioctl(fd_, UI_DEV_SETUP, &setup) != 0 || ::ioctl(fd_, UI_DEV_CREATE) != 0)
        {
            return false;
        }
        char sysname[64] = {};
        if (::ioctl(fd_, UI_GET_SYSNAME(sizeof(sysname)), sysname) < 0)
        {
            return false;
        }
        const std::string directory = std::string("/sys/devices/virtual/input/") + sysname;
        if (DIR* entries = ::opendir(directory.c_str()))
        {
            while (const dirent* entry = ::readdir(entries))
            {
                if (std::strncmp(entry->d_name, "event", 5) == 0)
                {
                    node_ = std::string("/dev/input/") + entry->d_name;
                }
            }
            ::closedir(entries);
        }
        return !node_.empty();
    }

    ~VirtualPen()
    {
        if (fd_ >= 0)
        {
            ::ioctl(fd_, UI_DEV_DESTROY);
            ::close(fd_);
        }
    }

    [[nodiscard]] const std::string& Node() const { return node_; }

    /// Brings the pen into range above a point. A real stylus streams positions while it hovers;
    /// a scripted one has to, or the driver has nothing to place it with: evdev applies a
    /// position that arrives with the coming into range only at the next move in range, and the
    /// kernel drops an axis whose value has not changed since the pen last reported it (the same
    /// point as a previous test, typically). So it enters diagonally beside the point and moves
    /// onto it, both axes changing both times.
    [[nodiscard]] bool Hover(const int screenX, const int screenY)
    {
        return Frame({{EV_KEY, BTN_TOOL_PEN, 1},
                      {EV_ABS, ABS_X, DeviceX(screenX + 4)},
                      {EV_ABS, ABS_Y, DeviceY(screenY + 4)}}) &&
               Frame({{EV_ABS, ABS_X, DeviceX(screenX)}, {EV_ABS, ABS_Y, DeviceY(screenY)}});
    }

    /// Puts the tip down with a pressure.
    [[nodiscard]] bool Touch(const int pressure)
    {
        return Frame({{EV_ABS, ABS_PRESSURE, pressure}, {EV_KEY, BTN_TOUCH, 1}});
    }

    /// Moves -- touching or hovering, whichever the pen is doing.
    [[nodiscard]] bool Move(const int screenX, const int screenY, const int pressure)
    {
        return Frame({{EV_ABS, ABS_X, DeviceX(screenX)},
                      {EV_ABS, ABS_Y, DeviceY(screenY)},
                      {EV_ABS, ABS_PRESSURE, pressure}});
    }

    /// Lifts the tip.
    [[nodiscard]] bool Lift() { return Frame({{EV_ABS, ABS_PRESSURE, 0}, {EV_KEY, BTN_TOUCH, 0}}); }

    /// Takes the pen out of range.
    [[nodiscard]] bool Leave() { return Frame({{EV_KEY, BTN_TOOL_PEN, 0}}); }

private:
    bool Frame(const std::vector<std::array<int, 3>>& events)
    {
        if (!HeldExclusivelyByAnother(node_))
        {
            return false;
        }
        for (const auto& [type, code, value] : events)
        {
            input_event event{};
            event.type = static_cast<__u16>(type);
            event.code = static_cast<__u16>(code);
            event.value = value;
            (void) ::write(fd_, &event, sizeof(event));
        }
        input_event sync{};
        sync.type = EV_SYN;
        sync.code = SYN_REPORT;
        (void) ::write(fd_, &sync, sizeof(sync));
        return true;
    }

    int fd_ = -1;
    std::string node_;
};

/// A rootless Xorg with the dummy video driver and the virtual devices, taken exclusively.
class PrivateXorg
{
public:
    /// Why it could not be started, or empty once it runs.
    std::string Start(const std::vector<std::string>& nodes)
    {
        const std::string xorg = "/usr/lib/xorg/Xorg";
        if (::access(xorg.c_str(), X_OK) != 0)
        {
            return "no Xorg server binary at " + xorg + " (xserver-xorg-core)";
        }
        std::vector<std::string> modulePaths = {"/usr/lib/xorg/modules"};
        if (const char* extra = std::getenv("CNA_X11_XORG_MODULE_PATH"))
        {
            std::stringstream paths(extra);
            std::string path;
            while (std::getline(paths, path, ':'))
            {
                if (!path.empty()) { modulePaths.push_back(path); }
            }
        }
        const auto findModule = [&modulePaths](const std::string& relative) {
            return std::any_of(modulePaths.begin(), modulePaths.end(), [&relative](const std::string& path) {
                return ::access((path + "/" + relative).c_str(), R_OK) == 0;
            });
        };
        if (!findModule("drivers/dummy_drv.so") || !findModule("input/evdev_drv.so"))
        {
            return "the Xorg dummy video driver and evdev input driver were not found "
                   "(xserver-xorg-video-dummy, xserver-xorg-input-evdev, or "
                   "CNA_X11_XORG_MODULE_PATH)";
        }

        char directoryTemplate[] = "/tmp/cna-x11-touch-XXXXXX";
        if (::mkdtemp(directoryTemplate) == nullptr)
        {
            return "no temporary directory";
        }
        directory_ = directoryTemplate;
        ::mkdir((directory_ + "/confd").c_str(), 0700);
        // A private config directory with one file in it keeps the server from reading the
        // machine's own snippets; GLX and DRI are left out so it opens no GPU.
        std::ofstream(directory_ + "/confd/00-empty.conf") << "# CNA: no system snippets\n";
        std::ofstream config(directory_ + "/xorg.conf");
        config << "Section \"Files\"\n";
        for (const std::string& path : modulePaths)
        {
            config << "    ModulePath \"" << path << "\"\n";
        }
        config << "EndSection\n"
                  "Section \"ServerFlags\"\n"
                  "    Option \"AutoAddDevices\" \"false\"\n"
                  "    Option \"AutoEnableDevices\" \"false\"\n"
                  "    Option \"AutoAddGPU\" \"false\"\n"
                  "    Option \"AutoBindGPU\" \"false\"\n"
                  "    Option \"DontVTSwitch\" \"true\"\n"
                  "EndSection\n"
                  "Section \"Module\"\n"
                  "    Disable \"glx\"\n    Disable \"glamoregl\"\n    Disable \"dri\"\n"
                  "    Disable \"dri2\"\n"
                  "EndSection\n"
                  "Section \"Device\"\n    Identifier \"cna-dummy\"\n    Driver \"dummy\"\n"
                  "    VideoRam 65536\nEndSection\n"
                  "Section \"Monitor\"\n    Identifier \"cna-monitor\"\n"
                  "    HorizSync 5.0-1000.0\n    VertRefresh 5.0-200.0\nEndSection\n"
                  "Section \"Screen\"\n    Identifier \"cna-screen\"\n    Device \"cna-dummy\"\n"
                  "    Monitor \"cna-monitor\"\n    DefaultDepth 24\n"
                  "    SubSection \"Display\"\n        Depth 24\n        Modes \"1280x1024\"\n"
                  "    EndSubSection\nEndSection\n"
                  ;
        // Each device evdev reads with GrabDevice: the kernel then gives its events to this
        // server alone.
        for (std::size_t index = 0; index < nodes.size(); ++index)
        {
            config << "Section \"InputDevice\"\n    Identifier \"cna-device-" << index << "\"\n"
                   << "    Driver \"evdev\"\n    Option \"Device\" \"" << nodes[index] << "\"\n"
                   << "    Option \"GrabDevice\" \"on\"\nEndSection\n";
        }
        config << "Section \"ServerLayout\"\n    Identifier \"cna-layout\"\n"
                  "    Screen \"cna-screen\"\n";
        for (std::size_t index = 0; index < nodes.size(); ++index)
        {
            config << "    InputDevice \"cna-device-" << index << "\"\n";
        }
        config << "EndSection\n";
        config.close();

        // Well clear of the range the launcher searches, and wide: a busy build machine has many
        // private servers up at once.
        for (int number = 400; number < 700; ++number)
        {
            const std::string socket = "/tmp/.X11-unix/X" + std::to_string(number);
            const std::string lock = "/tmp/.X" + std::to_string(number) + "-lock";
            if (::access(socket.c_str(), F_OK) != 0 && ::access(lock.c_str(), F_OK) != 0)
            {
                display_ = ":" + std::to_string(number);
                socket_ = socket;
                break;
            }
        }
        if (display_.empty())
        {
            return "no free display number between :400 and :699";
        }

        const std::string configPath = directory_ + "/xorg.conf";
        const std::string configDirectory = directory_ + "/confd";
        const std::string log = directory_ + "/xorg.log";
        pid_ = ::fork();
        if (pid_ == 0)
        {
            // The server goes with this process however it ends -- a crashed test must not leave
            // a server (and its hold on nothing) running on the machine.
            ::prctl(PR_SET_PDEATHSIG, SIGTERM);
            const int devNull = ::open("/dev/null", O_RDWR);
            if (devNull >= 0)
            {
                ::dup2(devNull, 0);
                ::dup2(devNull, 1);
                ::dup2(devNull, 2);
            }
            ::execl(xorg.c_str(), xorg.c_str(), display_.c_str(), "-config", configPath.c_str(),
                    "-configdir", configDirectory.c_str(), "-noreset", "-nolisten", "tcp",
                    "-logfile", log.c_str(), "-novtswitch", "-sharevts",
                    static_cast<char*>(nullptr));
            ::_exit(127);
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (::waitpid(pid_, nullptr, WNOHANG) == pid_)
            {
                pid_ = -1;
                return "the private Xorg exited while starting; see its log";
            }
            if (::access(socket_.c_str(), F_OK) == 0)
            {
                return {};
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        return "the private Xorg did not come up";
    }

    void Stop()
    {
        if (pid_ > 0)
        {
            ::kill(pid_, SIGTERM);
            ::waitpid(pid_, nullptr, 0);
            pid_ = -1;
        }
        if (!directory_.empty())
        {
            std::remove((directory_ + "/confd/00-empty.conf").c_str());
            ::rmdir((directory_ + "/confd").c_str());
            std::remove((directory_ + "/xorg.conf").c_str());
            std::remove((directory_ + "/xorg.log").c_str());
            std::remove((directory_ + "/xorg.log.old").c_str());
            ::rmdir(directory_.c_str());
            directory_.clear();
        }
    }

    [[nodiscard]] const std::string& Display() const { return display_; }

private:
    pid_t pid_ = -1;
    std::string directory_;
    std::string display_;
    std::string socket_;
};

class X11Touchscreen : public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        skipReason_.clear();
        const char* optIn = std::getenv("CNA_X11_TEST_TOUCHSCREEN");
        if (optIn == nullptr || std::string(optIn) != "1")
        {
            skipReason_ = "opt-in (CNA_X11_TEST_TOUCHSCREEN=1): creates a virtual touchscreen and a "
                          "private Xorg";
            return;
        }
        if (::access("/dev/uinput", W_OK) != 0)
        {
            skipReason_ = "/dev/uinput is not writable";
            return;
        }
        touchscreen_ = new VirtualTouchscreen();
        if (!touchscreen_->Create())
        {
            skipReason_ = "could not create a uinput touchscreen";
            return;
        }
        pen_ = new VirtualPen();
        if (!pen_->Create())
        {
            skipReason_ = "could not create a uinput pen";
            return;
        }
        // A node's group and ACL arrive from udev a moment after the kernel creates it.
        for (const std::string& node : {touchscreen_->Node(), pen_->Node()})
        {
            bool readable = false;
            for (int attempt = 0; attempt < 100 && !readable; ++attempt)
            {
                const int fd = ::open(node.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
                if (fd >= 0)
                {
                    ::close(fd);
                    readable = true;
                }
                else
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
            }
            if (!readable)
            {
                skipReason_ = "the virtual device's node " + node + " is not readable";
                return;
            }
        }
        server_ = new PrivateXorg();
        if (const std::string refusal = server_->Start({touchscreen_->Node(), pen_->Node()});
            !refusal.empty())
        {
            skipReason_ = refusal;
            return;
        }
        // The server adds the devices as it starts; each grab is taken when its device is enabled.
        for (const std::string& node : {touchscreen_->Node(), pen_->Node()})
        {
            bool exclusive = false;
            for (int attempt = 0; attempt < 100 && !exclusive; ++attempt)
            {
                exclusive = HeldExclusivelyByAnother(node);
                if (!exclusive) { std::this_thread::sleep_for(std::chrono::milliseconds(50)); }
            }
            if (!exclusive)
            {
                skipReason_ = "the private Xorg does not hold " + node + " exclusively, so its "
                              "input could reach the desktop; nothing was injected";
                return;
            }
        }
        savedDisplay_ = System::Environment::GetEnvironmentVariable("DISPLAY");
        System::Environment::SetEnvironmentVariable("DISPLAY", server_->Display());
        displaySet_ = true;
    }

    static void TearDownTestSuite()
    {
        if (displaySet_)
        {
            System::Environment::SetEnvironmentVariable("DISPLAY", savedDisplay_);
            displaySet_ = false;
        }
        if (server_ != nullptr)
        {
            server_->Stop();
            delete server_;
            server_ = nullptr;
        }
        delete touchscreen_;
        touchscreen_ = nullptr;
        delete pen_;
        pen_ = nullptr;
    }

    void SetUp() override
    {
        if (!skipReason_.empty())
        {
            GTEST_SKIP() << skipReason_;
        }
        platform_ = PlatformFactory::Create("X11");
        platform_->AcquireSubsystem(PlatformSubsystem::Video);
        acquired_ = true;
        WindowDescription description;
        description.title = "CNA touch target";
        description.centered = false;
        description.x = 100;
        description.y = 80;
        description.width = 320;
        description.height = 240;
        window_ = platform_->CreateWindow(description);
        const WindowId id = window_->GetId();
        ASSERT_TRUE(PumpUntil([id](const std::vector<PlatformEvent>& events) {
            return std::any_of(events.begin(), events.end(), [id](const PlatformEvent& event) {
                const auto* windowEvent = std::get_if<WindowEvent>(&event);
                return windowEvent != nullptr && windowEvent->window == id &&
                       (windowEvent->kind == WindowEventKind::Exposed ||
                        windowEvent->kind == WindowEventKind::Restored);
            });
        })) << "the window never mapped on the private server";
        const WindowBounds bounds = window_->GetClientBounds();
        ASSERT_EQ(bounds.x, 100);
        ASSERT_EQ(bounds.y, 80);
        seen_.clear();
    }

    void TearDown() override
    {
        window_.reset();
        if (acquired_)
        {
            platform_->ReleaseSubsystem(PlatformSubsystem::Video);
        }
        platform_.reset();
    }

    bool PumpUntil(const std::function<bool(const std::vector<PlatformEvent>&)>& done,
                   const std::chrono::milliseconds budget = std::chrono::milliseconds(3000))
    {
        const auto deadline = std::chrono::steady_clock::now() + budget;
        std::vector<PlatformEvent> batch;
        while (std::chrono::steady_clock::now() < deadline)
        {
            platform_->PollEvents(batch);
            seen_.insert(seen_.end(), batch.begin(), batch.end());
            if (done(seen_))
            {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return false;
    }

    void Settle() { (void) PumpUntil([](const std::vector<PlatformEvent>&) { return false; }, std::chrono::milliseconds(250)); }

    [[nodiscard]] std::vector<TouchEvent> Touches() const
    {
        std::vector<TouchEvent> touches;
        for (const PlatformEvent& event : seen_)
        {
            if (const auto* touch = std::get_if<TouchEvent>(&event)) { touches.push_back(*touch); }
        }
        return touches;
    }

    /// Everything seen, briefly -- for a failure message.
    [[nodiscard]] std::string Describe() const
    {
        std::ostringstream out;
        for (const PlatformEvent& event : seen_)
        {
            if (const auto* touch = std::get_if<TouchEvent>(&event))
            {
                out << "touch:" << ToString(touch->kind) << "(" << touch->x << "," << touch->y << ") ";
            }
            else if (const auto* motion = std::get_if<MouseMotionEvent>(&event))
            {
                out << "move(" << motion->window << ":" << motion->x << "," << motion->y << ") ";
            }
            else if (const auto* button = std::get_if<MouseButtonEvent>(&event))
            {
                out << (button->pressed ? "press(" : "release(") << button->window << ":" << button->x
                    << "," << button->y << ") ";
            }
            else
            {
                out << GetEventTypeName(event) << " ";
            }
        }
        return out.str();
    }

    [[nodiscard]] std::vector<MouseButtonEvent> Buttons() const
    {
        std::vector<MouseButtonEvent> buttons;
        for (const PlatformEvent& event : seen_)
        {
            if (const auto* button = std::get_if<MouseButtonEvent>(&event)) { buttons.push_back(*button); }
        }
        return buttons;
    }

    static inline std::string skipReason_;
    static inline VirtualTouchscreen* touchscreen_ = nullptr;
    static inline VirtualPen* pen_ = nullptr;
    static inline PrivateXorg* server_ = nullptr;
    static inline std::optional<std::string> savedDisplay_;
    static inline bool displaySet_ = false;

    std::unique_ptr<IPlatform> platform_;
    std::unique_ptr<IPlatformWindow> window_;
    std::vector<PlatformEvent> seen_;
    bool acquired_ = false;
};

TEST_F(X11Touchscreen, TheTouchscreenIsEnumeratedBeforeAnythingTouchesIt)
{
    // plans/plan_x11.md X11-0165: what TouchPanel asks before the first touch. The private Xorg's
    // devices are exactly the two the suite made, named -- as Xorg names a configured device -- by
    // their configuration's identifiers: the touchscreen first, then the pen.
    IPlatformInputDevices* devices = platform_->GetInputDevices();
    ASSERT_NE(devices, nullptr);
    const auto names = [devices](const InputDeviceKind kind) {
        std::vector<std::string> result;
        for (const InputDeviceInfo& info : devices->GetDevices(kind))
        {
            result.push_back(info.name);
            EXPECT_EQ(info.kind, kind);
        }
        std::sort(result.begin(), result.end());
        return result;
    };
    EXPECT_EQ(names(InputDeviceKind::Touch), std::vector<std::string>{"cna-device-0"});
    EXPECT_EQ(names(InputDeviceKind::Mouse), (std::vector<std::string>{"cna-device-0", "cna-device-1"}));
    EXPECT_TRUE(names(InputDeviceKind::Keyboard).empty());
    EXPECT_TRUE(devices->HasDevice(InputDeviceKind::Touch));
}

TEST_F(X11Touchscreen, ATouchArrivesInTheWindowsNormalisedCoordinates)
{
    // The window's centre: client (160, 120) is screen (260, 200).
    ASSERT_TRUE(touchscreen_->Down(0, 10, 260, 200)) << "the grab was lost; nothing injected";
    ASSERT_TRUE(PumpUntil([this](const std::vector<PlatformEvent>&) { return !Touches().empty(); }))
        << "no touch arrived";
    ASSERT_TRUE(touchscreen_->Up(0, true));
    ASSERT_TRUE(PumpUntil([this](const std::vector<PlatformEvent>&) {
        return Touches().size() >= 2 && Touches().back().kind == TouchEventKind::Up;
    })) << "the touch never lifted";

    const std::vector<TouchEvent> touches = Touches();
    const TouchEvent& down = touches.front();
    EXPECT_EQ(down.kind, TouchEventKind::Down);
    EXPECT_EQ(down.window, window_->GetId());
    EXPECT_NEAR(down.x, 0.5f, 0.01f);
    EXPECT_NEAR(down.y, 0.5f, 0.01f);
    EXPECT_EQ(down.clientWidth, 320);
    EXPECT_EQ(down.clientHeight, 240);
    EXPECT_FLOAT_EQ(down.pressure, 1.0f);
    EXPECT_EQ(touches.back().fingerId, down.fingerId);
}

TEST_F(X11Touchscreen, AMovingTouchReportsMotionWithItsDelta)
{
    ASSERT_TRUE(touchscreen_->Down(0, 11, 260, 200));
    ASSERT_TRUE(PumpUntil([this](const std::vector<PlatformEvent>&) { return !Touches().empty(); }));
    // Client x 160 -> 192: a tenth of the width.
    ASSERT_TRUE(touchscreen_->Move(0, 292, 200));
    ASSERT_TRUE(PumpUntil([this](const std::vector<PlatformEvent>&) {
        const std::vector<TouchEvent> touches = Touches();
        return std::any_of(touches.begin(), touches.end(), [](const TouchEvent& touch) {
            return touch.kind == TouchEventKind::Motion;
        });
    })) << "no motion arrived";
    ASSERT_TRUE(touchscreen_->Up(0, true));
    Settle();

    const std::vector<TouchEvent> touches = Touches();
    const auto motion = std::find_if(touches.begin(), touches.end(), [](const TouchEvent& touch) {
        return touch.kind == TouchEventKind::Motion;
    });
    ASSERT_NE(motion, touches.end());
    EXPECT_NEAR(motion->x, 0.6f, 0.01f);
    EXPECT_NEAR(motion->deltaX, 0.1f, 0.01f);
    EXPECT_NEAR(motion->deltaY, 0.0f, 0.01f);
}

TEST_F(X11Touchscreen, TwoFingersAreTwoContacts)
{
    ASSERT_TRUE(touchscreen_->Down(0, 20, 200, 150));
    ASSERT_TRUE(touchscreen_->Down(1, 21, 350, 250));
    ASSERT_TRUE(PumpUntil([this](const std::vector<PlatformEvent>&) {
        const std::vector<TouchEvent> touches = Touches();
        return std::count_if(touches.begin(), touches.end(), [](const TouchEvent& touch) {
                   return touch.kind == TouchEventKind::Down;
               }) >= 2;
    })) << "the second finger did not arrive";
    ASSERT_TRUE(touchscreen_->Up(1, false));
    ASSERT_TRUE(touchscreen_->Up(0, true));
    ASSERT_TRUE(PumpUntil([this](const std::vector<PlatformEvent>&) {
        const std::vector<TouchEvent> touches = Touches();
        return std::count_if(touches.begin(), touches.end(), [](const TouchEvent& touch) {
                   return touch.kind == TouchEventKind::Up;
               }) >= 2;
    })) << "the fingers did not both lift";

    std::vector<std::uint64_t> fingers;
    for (const TouchEvent& touch : Touches())
    {
        if (touch.kind == TouchEventKind::Down) { fingers.push_back(touch.fingerId); }
    }
    ASSERT_EQ(fingers.size(), 2u);
    EXPECT_NE(fingers[0], fingers[1]);
}

TEST_F(X11Touchscreen, TheTouchThatEmulatesThePointerAlsoDrivesTheMouseExactlyOnce)
{
    // A mouse-driven game on a touchscreen: the window took the touch events, so the pointer
    // events the server emulates from them are not delivered to it -- the backend makes them.
    ASSERT_TRUE(touchscreen_->Down(0, 30, 260, 200));
    ASSERT_TRUE(PumpUntil([this](const std::vector<PlatformEvent>&) { return !Buttons().empty(); }))
        << "the touch pressed no mouse button";
    IPlatformMouse* mouse = platform_->GetMouse();
    ASSERT_NE(mouse, nullptr);
    mouse->Update();
    EXPECT_NE(mouse->GetSnapshot().buttons & 1u, 0u) << "the snapshot does not hold the left button";
    ASSERT_TRUE(touchscreen_->Up(0, true));
    ASSERT_TRUE(PumpUntil([this](const std::vector<PlatformEvent>&) { return Buttons().size() >= 2; }));
    Settle();
    mouse->Update();
    EXPECT_EQ(mouse->GetSnapshot().buttons & 1u, 0u);

    const std::vector<MouseButtonEvent> buttons = Buttons();
    ASSERT_EQ(buttons.size(), 2u) << "one touch must be one press and one release, not two of each";
    EXPECT_TRUE(buttons[0].pressed);
    EXPECT_EQ(buttons[0].button, 1);
    EXPECT_NEAR(buttons[0].x, 160.0f, 1.0f);
    EXPECT_NEAR(buttons[0].y, 120.0f, 1.0f);
    EXPECT_FALSE(buttons[1].pressed);
}

TEST_F(X11Touchscreen, ATouchOutsideTheWindowIsNotItsTouch)
{
    ASSERT_TRUE(touchscreen_->Down(0, 40, 20, 20));
    ASSERT_TRUE(touchscreen_->Up(0, true));
    Settle();
    EXPECT_TRUE(Touches().empty());
    EXPECT_TRUE(Buttons().empty());
}

TEST_F(X11Touchscreen, AWindowDestroyedMidTouchCancelsItsContact)
{
    ASSERT_TRUE(touchscreen_->Down(0, 50, 260, 200));
    ASSERT_TRUE(PumpUntil([this](const std::vector<PlatformEvent>&) { return !Touches().empty(); }));
    window_.reset();
    Settle();
    ASSERT_TRUE(touchscreen_->Up(0, true));
    Settle();

    const std::vector<TouchEvent> touches = Touches();
    ASSERT_GE(touches.size(), 2u);
    EXPECT_EQ(touches.back().kind, TouchEventKind::Cancelled)
        << "a finger on a window that is gone must not stay down";
    EXPECT_EQ(touches.back().fingerId, touches.front().fingerId);
    // And the left button it held for the mouse lets go with it.
    const std::vector<MouseButtonEvent> buttons = Buttons();
    ASSERT_FALSE(buttons.empty());
    EXPECT_FALSE(buttons.back().pressed);
}

TEST_F(X11Touchscreen, APenTouchingTheWindowIsATouchWithItsPressure)
{
    ASSERT_TRUE(pen_->Hover(260, 200)) << "the grab was lost; nothing injected";
    Settle();
    EXPECT_TRUE(Touches().empty()) << "a hovering pen is not a touch";
    ASSERT_TRUE(pen_->Touch(512));
    ASSERT_TRUE(PumpUntil([this](const std::vector<PlatformEvent>&) { return !Touches().empty(); }))
        << "the pen's tip made no touch; seen: " << Describe();
    ASSERT_TRUE(pen_->Move(292, 200, 767));
    ASSERT_TRUE(PumpUntil([this](const std::vector<PlatformEvent>&) {
        const std::vector<TouchEvent> touches = Touches();
        return std::any_of(touches.begin(), touches.end(), [](const TouchEvent& touch) {
            return touch.kind == TouchEventKind::Motion;
        });
    })) << "the pen moved touching and no motion arrived";
    ASSERT_TRUE(pen_->Lift());
    ASSERT_TRUE(pen_->Leave());
    ASSERT_TRUE(PumpUntil([this](const std::vector<PlatformEvent>&) {
        return Touches().back().kind == TouchEventKind::Up;
    })) << "the pen lifted and the touch did not end";

    const std::vector<TouchEvent> touches = Touches();
    EXPECT_EQ(touches.front().kind, TouchEventKind::Down);
    EXPECT_NEAR(touches.front().x, 0.5f, 0.01f);
    EXPECT_NEAR(touches.front().pressure, 512.0f / VirtualPen::kPressureMaximum, 0.01f);
    // The server delivers a pen's move in its own steps -- evdev's reported the new pressure at
    // an intermediate position and the final position with the lift -- so what is checked is
    // that the drag's pressure arrived and that the contact ended where the pen went.
    std::ostringstream sequence;
    for (const TouchEvent& touch : touches)
    {
        sequence << ToString(touch.kind) << "(" << touch.x << "," << touch.y << " p" << touch.pressure
                 << ") ";
    }
    EXPECT_TRUE(std::any_of(touches.begin(), touches.end(), [](const TouchEvent& touch) {
        return touch.kind == TouchEventKind::Motion &&
               std::abs(touch.pressure - 767.0f / VirtualPen::kPressureMaximum) < 0.01f;
    })) << "the drag's pressure never arrived: " << sequence.str();
    EXPECT_EQ(touches.back().kind, TouchEventKind::Up);
    EXPECT_NEAR(touches.back().x, 0.6f, 0.01f) << sequence.str();
    for (const TouchEvent& touch : touches)
    {
        EXPECT_EQ(touch.fingerId, touches.front().fingerId) << "one pen is one contact";
    }
}

TEST_F(X11Touchscreen, APenStillDrivesTheMouseThroughTheCorePointer)
{
    // The pen's own XI2 events are selected per device; the core pointer events it drives must
    // still reach the window -- once each, not doubled by the touch the pen also makes.
    ASSERT_TRUE(pen_->Hover(260, 200));
    ASSERT_TRUE(pen_->Touch(600));
    ASSERT_TRUE(PumpUntil([this](const std::vector<PlatformEvent>&) { return !Buttons().empty(); }))
        << "the pen's tip pressed no mouse button";
    ASSERT_TRUE(pen_->Lift());
    ASSERT_TRUE(pen_->Leave());
    ASSERT_TRUE(PumpUntil([this](const std::vector<PlatformEvent>&) { return Buttons().size() >= 2; }));
    Settle();

    const std::vector<MouseButtonEvent> buttons = Buttons();
    ASSERT_EQ(buttons.size(), 2u);
    EXPECT_TRUE(buttons[0].pressed);
    EXPECT_EQ(buttons[0].button, 1);
    EXPECT_NEAR(buttons[0].x, 160.0f, 1.0f);
    EXPECT_FALSE(buttons[1].pressed);
    bool moved = false;
    for (const PlatformEvent& event : seen_)
    {
        if (std::holds_alternative<MouseMotionEvent>(event)) { moved = true; }
    }
    EXPECT_TRUE(moved) << "the hovering pen never moved the mouse";
}

} // namespace
