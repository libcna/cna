// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <string>

#include "Microsoft/Devices/VibrateController.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/TimeSpan.hpp"

using Microsoft::Devices::VibrateController;
using System::TimeSpan;

// NOTE: VibrateController::Start() deliberately skips haptic devices that
// are also connected joysticks/gamepads, so it never competes with
// GamePad::SetVibration() for the same physical rumble motor (see
// IsConnectedGamepadHapticDevice() in VibrateController.cpp). That
// exclusion behavior isn't unit-testable here: it requires a real connected
// haptic-capable gamepad to observe, which isn't available in this headless
// environment and can't be simulated through this class's public API. The
// tests below still cover the same-process consequence that matters for
// this class's own contract: every in-range call is a no-throw silent
// no-op when no suitable device is found.

TEST(VibrateControllerTests, GetDefaultPropertyIsNeverNull)
{
    ASSERT_NE(VibrateController::getDefaultProperty(), nullptr);
}

TEST(VibrateControllerTests, GetDefaultPropertyReturnsSameInstance)
{
    EXPECT_EQ(VibrateController::getDefaultProperty(), VibrateController::getDefaultProperty());
}

TEST(VibrateControllerTests, StopBeforeAnyStartDoesNotThrow)
{
    EXPECT_NO_THROW(VibrateController::getDefaultProperty()->Stop());
}

TEST(VibrateControllerTests, StartWithZeroDurationDoesNotThrow)
{
    EXPECT_NO_THROW(VibrateController::getDefaultProperty()->Start(TimeSpan::Zero));
}

TEST(VibrateControllerTests, StartWithShortDurationDoesNotThrow)
{
    EXPECT_NO_THROW(VibrateController::getDefaultProperty()->Start(TimeSpan::FromMilliseconds(100)));
}

TEST(VibrateControllerTests, StartWithExactlyMaxDurationDoesNotThrow)
{
    // XNA/WP7 max is 5 seconds, inclusive.
    EXPECT_NO_THROW(VibrateController::getDefaultProperty()->Start(TimeSpan::FromSeconds(5)));
}

TEST(VibrateControllerTests, StartWithNegativeDurationThrows)
{
    EXPECT_THROW(
        VibrateController::getDefaultProperty()->Start(TimeSpan::FromMilliseconds(-1)),
        System::ArgumentOutOfRangeException);
}

TEST(VibrateControllerTests, StartWithOverlongDurationThrows)
{
    // XNA/WP7 max is 5 seconds; anything past it must throw, not clamp.
    EXPECT_THROW(
        VibrateController::getDefaultProperty()->Start(TimeSpan::FromSeconds(5.001)),
        System::ArgumentOutOfRangeException);
}

TEST(VibrateControllerTests, StopAfterStartDoesNotThrow)
{
    VibrateController::getDefaultProperty()->Start(TimeSpan::FromMilliseconds(100));
    EXPECT_NO_THROW(VibrateController::getDefaultProperty()->Stop());
}

TEST(VibrateControllerTests, TwoConsecutiveStartsDoNotCrash)
{
    EXPECT_NO_THROW(VibrateController::getDefaultProperty()->Start(TimeSpan::FromMilliseconds(50)));
    EXPECT_NO_THROW(VibrateController::getDefaultProperty()->Start(TimeSpan::FromMilliseconds(50)));
}

// Phase 6 NOXNA extensions (Tasks P2-10 through P2-13). Same silent-no-op
// contract as the plain Start()/Stop() above when no suitable device is
// found; these tests only assert no-throw/no-crash behavior, not that a
// device was actually found or actuated (this environment's haptic
// availability isn't guaranteed either way).

TEST(VibrateControllerTests, StartWithIntensityZeroDoesNotThrow)
{
    EXPECT_NO_THROW(VibrateController::getDefaultProperty()->Start(TimeSpan::FromMilliseconds(50), 0.0f));
}

TEST(VibrateControllerTests, StartWithIntensityHalfDoesNotThrow)
{
    EXPECT_NO_THROW(VibrateController::getDefaultProperty()->Start(TimeSpan::FromMilliseconds(50), 0.5f));
}

TEST(VibrateControllerTests, StartWithIntensityOneDoesNotThrow)
{
    EXPECT_NO_THROW(VibrateController::getDefaultProperty()->Start(TimeSpan::FromMilliseconds(50), 1.0f));
}

TEST(VibrateControllerTests, StartWithOutOfRangeIntensityIsClampedSilentlyAndDoesNotThrow)
{
    // Unlike duration, out-of-range intensity is clamped, not rejected —
    // there is no real WP7 API contract to preserve here, so CNA is free to
    // choose the more forgiving behavior for its own extension.
    EXPECT_NO_THROW(VibrateController::getDefaultProperty()->Start(TimeSpan::FromMilliseconds(50), 1.5f));
    EXPECT_NO_THROW(VibrateController::getDefaultProperty()->Start(TimeSpan::FromMilliseconds(50), -0.5f));
}

TEST(VibrateControllerTests, StartWithIntensityOutOfRangeDurationStillThrows)
{
    // The intensity overload must still enforce the same duration contract
    // as the XNA-compliant overload it's layered on top of.
    EXPECT_THROW(
        VibrateController::getDefaultProperty()->Start(TimeSpan::FromSeconds(5.001), 1.0f),
        System::ArgumentOutOfRangeException);
}

TEST(VibrateControllerTests, GetIsSupportedPropertyDoesNotCrash)
{
    const bool supported = VibrateController::getDefaultProperty()->getIsSupportedProperty();
    (void)supported;
}

TEST(VibrateControllerTests, GetDeviceNamePropertyDoesNotCrash)
{
    const std::string name = VibrateController::getDefaultProperty()->getDeviceNameProperty();
    (void)name;
}

TEST(VibrateControllerTests, StartLeftRightDoesNotThrow)
{
    EXPECT_NO_THROW(
        VibrateController::getDefaultProperty()->StartLeftRight(1.0f, 1.0f, TimeSpan::FromMilliseconds(50)));
    EXPECT_NO_THROW(
        VibrateController::getDefaultProperty()->StartLeftRight(0.25f, 0.75f, TimeSpan::FromMilliseconds(50)));
    EXPECT_NO_THROW(VibrateController::getDefaultProperty()->Stop());
}

TEST(VibrateControllerTests, StartLeftRightWithOutOfRangeMagnitudesIsClampedSilentlyAndDoesNotThrow)
{
    EXPECT_NO_THROW(
        VibrateController::getDefaultProperty()->StartLeftRight(1.5f, -0.5f, TimeSpan::FromMilliseconds(50)));
}

TEST(VibrateControllerTests, StartLeftRightWithOutOfRangeDurationThrows)
{
    EXPECT_THROW(
        VibrateController::getDefaultProperty()->StartLeftRight(1.0f, 1.0f, TimeSpan::FromSeconds(5.001)),
        System::ArgumentOutOfRangeException);
}

// Task P3-5: Start()/Start(duration,intensity) and StartLeftRight() use
// independent SDL haptic effect slots and must stop each other's effect
// before starting their own, so they never run layered on the same
// motor(s). This environment can't assert on actual simultaneous-motor
// state headless (no real haptic hardware guaranteed), but it can assert
// the call sequence itself is safe end-to-end: switching between the two
// paths repeatedly, in both directions, must never throw or leak/double-
// free the StartLeftRight() effect slot, and Stop() must still clean up
// correctly afterward.

TEST(VibrateControllerTests, StartThenStartLeftRightThenStopDoesNotThrow)
{
    VibrateController* controller = VibrateController::getDefaultProperty();

    EXPECT_NO_THROW(controller->Start(TimeSpan::FromMilliseconds(50)));
    EXPECT_NO_THROW(controller->StartLeftRight(1.0f, 1.0f, TimeSpan::FromMilliseconds(50)));
    EXPECT_NO_THROW(controller->Stop());
}

TEST(VibrateControllerTests, StartLeftRightThenStartThenStopDoesNotThrow)
{
    VibrateController* controller = VibrateController::getDefaultProperty();

    EXPECT_NO_THROW(controller->StartLeftRight(1.0f, 1.0f, TimeSpan::FromMilliseconds(50)));
    EXPECT_NO_THROW(controller->Start(TimeSpan::FromMilliseconds(50)));
    EXPECT_NO_THROW(controller->Stop());
}

TEST(VibrateControllerTests, AlternatingStartAndStartLeftRightRepeatedlyDoesNotThrow)
{
    VibrateController* controller = VibrateController::getDefaultProperty();

    for (int i = 0; i < 3; ++i)
    {
        EXPECT_NO_THROW(controller->Start(TimeSpan::FromMilliseconds(10), 0.5f));
        EXPECT_NO_THROW(controller->StartLeftRight(0.5f, 0.5f, TimeSpan::FromMilliseconds(10)));
    }

    EXPECT_NO_THROW(controller->Stop());
}
