// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "Microsoft/Devices/VibrateController.hpp"
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
// this class's own contract: every call is a no-throw silent no-op when no
// suitable device is found.

TEST(VibrateControllerTests, StopBeforeAnyStartDoesNotThrow)
{
    EXPECT_NO_THROW(VibrateController::Stop());
}

TEST(VibrateControllerTests, StartWithZeroDurationDoesNotThrow)
{
    EXPECT_NO_THROW(VibrateController::Start(TimeSpan::Zero));
}

TEST(VibrateControllerTests, StartWithShortDurationDoesNotThrow)
{
    EXPECT_NO_THROW(VibrateController::Start(TimeSpan::FromMilliseconds(100)));
}

TEST(VibrateControllerTests, StartWithOverlongDurationIsClampedSilentlyAndDoesNotThrow)
{
    // XNA/WP7 max is 5 seconds; 10 seconds must be silently clamped, not throw.
    EXPECT_NO_THROW(VibrateController::Start(TimeSpan::FromSeconds(10)));
}

TEST(VibrateControllerTests, StopAfterStartDoesNotThrow)
{
    VibrateController::Start(TimeSpan::FromMilliseconds(100));
    EXPECT_NO_THROW(VibrateController::Stop());
}

TEST(VibrateControllerTests, TwoConsecutiveStartsDoNotCrash)
{
    EXPECT_NO_THROW(VibrateController::Start(TimeSpan::FromMilliseconds(50)));
    EXPECT_NO_THROW(VibrateController::Start(TimeSpan::FromMilliseconds(50)));
}
