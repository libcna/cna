// SPDX-License-Identifier: MS-PL
//
// plans/plan_x11.md X11-0165: input-device enumeration through XInput2 -- the classification and
// the hot-plug diff without a server, and the service on the launcher's Xvfb, checked against the
// server's own device list, with a real controller plugged in through uinput. The touchscreen suite
// (X11TouchTests.cpp) checks it against real touch devices on a private Xorg.

#include <gtest/gtest.h>

#include "../../../src/X11/X11Headers.hpp"
#include "../../../src/X11/X11InputDevices.hpp"

#include "CNA/Platform/PlatformFactory.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#ifdef CNA_PLATFORM_HAVE_EVDEV
#include "../../../src/Linux/EvdevHaptics.hpp"

#include <fcntl.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace {

using namespace CNA::Platform;
using CNA::Platform::X11::ClassifyX11InputDevice;
using CNA::Platform::X11::DiffX11InputDevices;
using CNA::Platform::X11::X11InputDeviceFacts;
using CNA::Platform::X11::kX11InputDeviceIdBase;

#if defined(CNA_X11_HAVE_XI)

TEST(X11InputDeviceClassification, SlavesAreDevicesAndTheServersOwnAreNot)
{
    EXPECT_EQ(ClassifyX11InputDevice({10, XISlaveKeyboard, "AT Translated Set 2 keyboard", true, false}),
              std::vector<InputDeviceKind>{InputDeviceKind::Keyboard});
    EXPECT_EQ(ClassifyX11InputDevice({11, XISlavePointer, "TPPS/2 Elan TrackPoint", true, false}),
              std::vector<InputDeviceKind>{InputDeviceKind::Mouse});
    EXPECT_EQ(ClassifyX11InputDevice({12, XISlavePointer, "ELAN Touchscreen", true, true}),
              (std::vector<InputDeviceKind>{InputDeviceKind::Mouse, InputDeviceKind::Touch}));

    // The core aggregates every server has, and the server's own XTEST devices.
    EXPECT_TRUE(ClassifyX11InputDevice({2, XIMasterPointer, "Virtual core pointer", true, false}).empty());
    EXPECT_TRUE(ClassifyX11InputDevice({3, XIMasterKeyboard, "Virtual core keyboard", true, false}).empty());
    EXPECT_TRUE(ClassifyX11InputDevice({4, XISlavePointer, "Virtual core XTEST pointer", true, false}).empty());
    EXPECT_TRUE(ClassifyX11InputDevice({5, XISlaveKeyboard, "Virtual core XTEST keyboard", true, false}).empty());
    // Floating and disabled devices deliver nothing to a game.
    EXPECT_TRUE(ClassifyX11InputDevice({13, XIFloatingSlave, "Wacom tablet", true, false}).empty());
    EXPECT_TRUE(ClassifyX11InputDevice({14, XISlaveKeyboard, "Disabled keyboard", false, false}).empty());
}

#endif // CNA_X11_HAVE_XI

TEST(X11InputDeviceClassification, AHotPlugIsReportedClassByClass)
{
    const DeviceId keyboard = kX11InputDeviceIdBase + 10;
    const DeviceId touchscreen = kX11InputDeviceIdBase + 12;
    const DeviceId mouse = kX11InputDeviceIdBase + 15;
    const std::map<DeviceId, std::vector<InputDeviceKind>> before = {
        {keyboard, {InputDeviceKind::Keyboard}},
        {touchscreen, {InputDeviceKind::Mouse, InputDeviceKind::Touch}}};
    const std::map<DeviceId, std::vector<InputDeviceKind>> after = {
        {keyboard, {InputDeviceKind::Keyboard}},
        {mouse, {InputDeviceKind::Mouse}}};
    const std::vector<DeviceEvent> changes = DiffX11InputDevices(before, after);
    ASSERT_EQ(changes.size(), 3u);
    EXPECT_EQ(changes[0].device, touchscreen);
    EXPECT_EQ(changes[0].kind, InputDeviceKind::Mouse);
    EXPECT_FALSE(changes[0].connected);
    EXPECT_EQ(changes[1].device, touchscreen);
    EXPECT_EQ(changes[1].kind, InputDeviceKind::Touch);
    EXPECT_FALSE(changes[1].connected);
    EXPECT_EQ(changes[2].device, mouse);
    EXPECT_EQ(changes[2].kind, InputDeviceKind::Mouse);
    EXPECT_TRUE(changes[2].connected);
    EXPECT_TRUE(DiffX11InputDevices(after, after).empty());
}

// --- the service, on the launcher's server ----------------------------------------------------------

class X11InputDevicesLive : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const char* privateServer = std::getenv("CNA_X11_PRIVATE_TEST_SERVER");
        if (privateServer == nullptr || std::string(privateServer) != "1")
        {
            GTEST_SKIP() << "needs tools/platform/x11_test_server.sh";
        }
        platform_ = PlatformFactory::Create("X11");
        platform_->AcquireSubsystem(PlatformSubsystem::Video);
        acquired_ = true;
    }

    void TearDown() override
    {
        if (acquired_)
        {
            platform_->ReleaseSubsystem(PlatformSubsystem::Video);
        }
        platform_.reset();
    }

    std::unique_ptr<IPlatform> platform_;
    bool acquired_ = false;
};

TEST_F(X11InputDevicesLive, TheServiceExistsExactlyWhereXInputTwoDoes)
{
    EXPECT_EQ(platform_->GetInputDevices() != nullptr, platform_->GetCapabilities().inputDeviceEnumeration);
#if defined(CNA_X11_HAVE_XI)
    EXPECT_TRUE(platform_->GetCapabilities().inputDeviceEnumeration) << "Xvfb speaks XInput2";
#endif
}

TEST_F(X11InputDevicesLive, TheServersOwnDevicesAreLeftOutAndItsSlavesListed)
{
    IPlatformInputDevices* devices = platform_->GetInputDevices();
    ASSERT_NE(devices, nullptr);
#if defined(CNA_X11_HAVE_XI)
    // The server's own list, asked independently: Xvfb has its core pointer and keyboard, their
    // XTEST slaves, and a slave keyboard and mouse of its own.
    ::Display* display = XOpenDisplay(nullptr);
    ASSERT_NE(display, nullptr);
    int count = 0;
    XIDeviceInfo* info = XIQueryDevice(display, XIAllDevices, &count);
    std::vector<std::string> keyboards;
    std::vector<std::string> mice;
    bool sawMaster = false;
    bool sawXtest = false;
    for (int index = 0; index < count; ++index)
    {
        const std::string name = info[index].name;
        const bool xtest = name.find("XTEST") != std::string::npos;
        sawMaster = sawMaster || info[index].use == XIMasterKeyboard || info[index].use == XIMasterPointer;
        sawXtest = sawXtest || xtest;
        if (!xtest && info[index].enabled != 0 && info[index].use == XISlaveKeyboard) { keyboards.push_back(name); }
        if (!xtest && info[index].enabled != 0 && info[index].use == XISlavePointer) { mice.push_back(name); }
    }
    XIFreeDeviceInfo(info);
    XCloseDisplay(display);
    ASSERT_TRUE(sawMaster && sawXtest) << "the exclusions have something to exclude";

    const auto names = [devices](const InputDeviceKind kind) {
        std::vector<std::string> result;
        for (const InputDeviceInfo& device : devices->GetDevices(kind))
        {
            EXPECT_EQ(device.kind, kind);
            EXPECT_GE(device.id, kX11InputDeviceIdBase);
            EXPECT_EQ(device.name.find("XTEST"), std::string::npos) << device.name;
            EXPECT_EQ(device.name.find("Virtual core"), std::string::npos) << device.name;
            result.push_back(device.name);
        }
        std::sort(result.begin(), result.end());
        return result;
    };
    std::sort(keyboards.begin(), keyboards.end());
    std::sort(mice.begin(), mice.end());
    EXPECT_EQ(names(InputDeviceKind::Keyboard), keyboards);
    EXPECT_EQ(names(InputDeviceKind::Mouse), mice);
    EXPECT_EQ(devices->HasDevice(InputDeviceKind::Keyboard), !keyboards.empty());
#endif
    // Xvfb has no touch device, and nothing here has a sensor.
    for (const InputDeviceKind kind : {InputDeviceKind::Touch, InputDeviceKind::Sensor})
    {
        EXPECT_TRUE(devices->GetDevices(kind).empty()) << ToString(kind);
        EXPECT_FALSE(devices->HasDevice(kind)) << ToString(kind);
    }
    // Haptic devices are Linux's force-feedback nodes (X11-0168), whatever the machine has.
    for (const InputDeviceInfo& haptic : devices->GetDevices(InputDeviceKind::Haptic))
    {
        EXPECT_EQ(haptic.kind, InputDeviceKind::Haptic);
#ifdef CNA_PLATFORM_HAVE_EVDEV
        EXPECT_GT(haptic.id, CNA::Platform::Linux::kEvdevHapticIdBase);
#endif
    }
}

#ifdef CNA_PLATFORM_HAVE_EVDEV

TEST_F(X11InputDevicesLive, AControllerIsListedUnderTheIdItsEventsCarry)
{
    // A real kernel device through uinput -- controller buttons and axes only, which a desktop's
    // input stack ignores.
    const int uinput = ::open("/dev/uinput", O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (uinput < 0)
    {
        GTEST_SKIP() << "/dev/uinput cannot be opened";
    }
    const std::string name = "CNA enumeration test pad " + std::to_string(::getpid());
    ioctl(uinput, UI_SET_EVBIT, EV_KEY);
    for (const int key : {BTN_SOUTH, BTN_EAST, BTN_NORTH, BTN_WEST, BTN_START})
    {
        ioctl(uinput, UI_SET_KEYBIT, key);
    }
    // It rumbles, so it is a haptic device as well (X11-0168).
    ioctl(uinput, UI_SET_EVBIT, EV_FF);
    ioctl(uinput, UI_SET_FFBIT, FF_RUMBLE);
    ioctl(uinput, UI_SET_EVBIT, EV_ABS);
    for (const int axis : {ABS_X, ABS_Y})
    {
        uinput_abs_setup setup{};
        setup.code = static_cast<std::uint16_t>(axis);
        setup.absinfo.minimum = -32768;
        setup.absinfo.maximum = 32767;
        ioctl(uinput, UI_SET_ABSBIT, axis);
        ioctl(uinput, UI_ABS_SETUP, &setup);
    }
    uinput_setup setup{};
    std::strncpy(setup.name, name.c_str(), UINPUT_MAX_NAME_SIZE - 1);
    setup.id.bustype = BUS_USB;
    setup.id.vendor = 0x1209;
    setup.id.product = 0x0004;
    setup.ff_effects_max = 1;
    ASSERT_EQ(ioctl(uinput, UI_DEV_SETUP, &setup), 0);
    ASSERT_EQ(ioctl(uinput, UI_DEV_CREATE), 0);

    IPlatformInputDevices* devices = platform_->GetInputDevices();
    ASSERT_NE(devices, nullptr);
    std::vector<InputDeviceInfo> gamepads;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    const auto listed = [&name](const std::vector<InputDeviceInfo>& list) {
        return std::find_if(list.begin(), list.end(), [&name](const InputDeviceInfo& info) { return info.name == name; });
    };
    while (std::chrono::steady_clock::now() < deadline)
    {
        std::vector<PlatformEvent> events;
        platform_->PollEvents(events);
        gamepads = devices->GetDevices(InputDeviceKind::Gamepad);
        if (listed(gamepads) != gamepads.end()) { break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    const auto pad = listed(gamepads);
    const std::vector<InputDeviceInfo> joysticks = devices->GetDevices(InputDeviceKind::Joystick);
    const std::vector<InputDeviceInfo> haptics = devices->GetDevices(InputDeviceKind::Haptic);
    const std::vector<HapticInfo> served = platform_->GetHaptics()->GetHaptics();
    const bool hasGamepad = devices->HasDevice(InputDeviceKind::Gamepad);
    ioctl(uinput, UI_DEV_DESTROY);
    ::close(uinput);
    ASSERT_NE(pad, gamepads.end()) << "the pad never appeared (its node may not be readable here)";
    EXPECT_EQ(pad->kind, InputDeviceKind::Gamepad);
    EXPECT_TRUE(hasGamepad);
    // The same controller as a joystick, under the same id.
    const auto asJoystick = listed(joysticks);
    ASSERT_NE(asJoystick, joysticks.end());
    EXPECT_EQ(asJoystick->id, pad->id);
    EXPECT_LT(pad->id, kX11InputDeviceIdBase) << "controller ids and X device ids never meet";
    // Its force feedback, under the haptics service's id -- the one IPlatformHaptics takes.
    const auto asHaptic = listed(haptics);
    ASSERT_NE(asHaptic, haptics.end());
    EXPECT_EQ(asHaptic->kind, InputDeviceKind::Haptic);
    EXPECT_GT(asHaptic->id, CNA::Platform::Linux::kEvdevHapticIdBase);
    EXPECT_TRUE(std::any_of(served.begin(), served.end(),
                            [&](const HapticInfo& info) { return info.id == asHaptic->id && info.name == name; }));
}

#endif // CNA_PLATFORM_HAVE_EVDEV

} // namespace
