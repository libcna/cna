// SPDX-License-Identifier: MS-PL
//
// PLAT-77a / PLAT-77b: desktop-space pointer control and input device enumeration.
//
// Both of these exist because PLAT-77's mapping found that `modules/input` ships capabilities the
// contract had nowhere to put. That makes the interesting cases the ones a build machine actually
// has: no attached gamepad, no touchscreen, possibly no desktop at all. Every check below is
// about what must be true when the answer is "none" -- because "none" is the answer that a
// migration silently turns into "unknown", and a capability probe that returns unknown reads as
// false to every caller.

#include "CNA/Platform/PlatformException.hpp"
#include "CNA/Platform/PlatformFactory.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace {

using namespace CNA::Platform;

class Sdl3InputDevicesTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const std::vector<std::string> available = PlatformFactory::GetAvailable();
        if (std::find(available.begin(), available.end(), "SDL3") == available.end())
        {
            GTEST_SKIP() << "built with CNA_PLATFORM != SDL3";
        }
        platform_ = PlatformFactory::Create("SDL3");
    }

    std::unique_ptr<IPlatform> platform_;
};

constexpr InputDeviceKind kEveryKind[] = {
    InputDeviceKind::Keyboard, InputDeviceKind::Mouse,   InputDeviceKind::Gamepad,
    InputDeviceKind::Joystick, InputDeviceKind::Touch,   InputDeviceKind::Haptic,
    InputDeviceKind::Sensor,
};

// --- PLAT-77b: enumeration --------------------------------------------------------------------

TEST_F(Sdl3InputDevicesTest, TheServiceIsPresentBecauseItsCapabilityIsTrue)
{
    EXPECT_EQ(platform_->GetInputDevices() != nullptr,
              platform_->GetCapabilities().inputDeviceEnumeration);
}

TEST_F(Sdl3InputDevicesTest, EveryDeviceClassCanBeEnumeratedWithoutThrowing)
{
    // The point of enumeration is that it answers before anything is plugged in. A class with no
    // devices must produce an empty list, not an exception a caller has to guard every probe with.
    IPlatformInputDevices* devices = platform_->GetInputDevices();
    ASSERT_NE(devices, nullptr);

    for (const InputDeviceKind kind : kEveryKind)
    {
        EXPECT_NO_THROW({
            const std::vector<InputDeviceInfo> found = devices->GetDevices(kind);
            (void)found;
        }) << ToString(kind);
    }
}

TEST_F(Sdl3InputDevicesTest, HasDeviceAgreesWithGetDevices)
{
    // HasDevice exists so an implementation can skip building a vector of names to answer the
    // only question most callers have. That optimisation is worth nothing if the two can disagree
    // -- and `TouchPanel::GetCapabilities` would be reading whichever one happened to be wrong.
    IPlatformInputDevices* devices = platform_->GetInputDevices();
    ASSERT_NE(devices, nullptr);

    for (const InputDeviceKind kind : kEveryKind)
    {
        EXPECT_EQ(devices->HasDevice(kind), !devices->GetDevices(kind).empty()) << ToString(kind);
    }
}

TEST_F(Sdl3InputDevicesTest, EveryEnumeratedDeviceReportsTheKindItWasAskedFor)
{
    // A device that came back tagged with the wrong kind would corrupt a caller merging several
    // lists -- which is exactly what InputDevicesEXT does.
    IPlatformInputDevices* devices = platform_->GetInputDevices();
    ASSERT_NE(devices, nullptr);

    for (const InputDeviceKind kind : kEveryKind)
    {
        for (const InputDeviceInfo& device : devices->GetDevices(kind))
        {
            EXPECT_EQ(device.kind, kind);
            EXPECT_NE(device.id, 0u) << "zero is the contract's no-device id";
        }
    }
}

TEST_F(Sdl3InputDevicesTest, EnumerationIsRepeatableWithNoHotplug)
{
    IPlatformInputDevices* devices = platform_->GetInputDevices();
    ASSERT_NE(devices, nullptr);

    for (const InputDeviceKind kind : kEveryKind)
    {
        EXPECT_EQ(devices->GetDevices(kind).size(), devices->GetDevices(kind).size())
            << ToString(kind);
    }
}

TEST_F(Sdl3InputDevicesTest, RepeatedEnumerationDoesNotLeakTheReturnedIdArrays)
{
    // SDL's enumeration calls hand back a malloc'd array the caller must SDL_free. A missed free
    // is invisible in a single call and obvious under ASan after a few hundred -- which is what
    // this loop is for, since these run under ASan+UBSan in this project.
    IPlatformInputDevices* devices = platform_->GetInputDevices();
    ASSERT_NE(devices, nullptr);

    for (int i = 0; i < 200; ++i)
    {
        for (const InputDeviceKind kind : kEveryKind)
        {
            (void)devices->GetDevices(kind);
            (void)devices->HasDevice(kind);
        }
    }
    SUCCEED();
}

// --- PLAT-77a: desktop-space pointer ----------------------------------------------------------

TEST_F(Sdl3InputDevicesTest, GlobalPointerCallsAreAvailableWhenTheCapabilityIsTrue)
{
    if (!platform_->GetCapabilities().globalPointer)
    {
        GTEST_SKIP() << "this platform reports no global pointer";
    }
    IPlatformMouse* mouse = platform_->GetMouse();
    ASSERT_NE(mouse, nullptr);

    float x = 0.0f;
    float y = 0.0f;
    EXPECT_NO_THROW((void)mouse->TryGetGlobalPosition(x, y));
}

TEST_F(Sdl3InputDevicesTest, GlobalPositionLeavesItsOutputsAloneWhenUnavailable)
{
    IPlatformMouse* mouse = platform_->GetMouse();
    ASSERT_NE(mouse, nullptr);

    float x = 1234.5f;
    float y = 6789.5f;
    if (mouse->TryGetGlobalPosition(x, y))
    {
        GTEST_SKIP() << "a desktop position is available here; the unavailable path is untestable";
    }
    // The contract says untouched on false, and a caller that ignores the return value must not
    // end up warping the pointer to whatever the service happened to leave behind.
    EXPECT_FLOAT_EQ(x, 1234.5f);
    EXPECT_FLOAT_EQ(y, 6789.5f);
}

TEST_F(Sdl3InputDevicesTest, CaptureAndWarpReportStatusRatherThanThrowing)
{
    // Both are ordinary operations that a windowing system may simply decline -- a compositor
    // that refuses pointer warping is a normal Wayland configuration, not an error condition.
    IPlatformMouse* mouse = platform_->GetMouse();
    ASSERT_NE(mouse, nullptr);

    EXPECT_NO_THROW((void)mouse->SetCapture(true));
    EXPECT_NO_THROW((void)mouse->SetCapture(false));
    EXPECT_NO_THROW((void)mouse->SetGlobalPosition(0.0f, 0.0f));
}

TEST_F(Sdl3InputDevicesTest, ReleasingACaptureThatWasNeverTakenIsSafe)
{
    IPlatformMouse* mouse = platform_->GetMouse();
    ASSERT_NE(mouse, nullptr);
    EXPECT_NO_THROW((void)mouse->SetCapture(false));
    EXPECT_NO_THROW((void)mouse->SetCapture(false));
}

} // namespace
