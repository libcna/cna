// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "Microsoft/Devices/VibrateController.hpp"
#include "System/TimeSpan.hpp"

using Microsoft::Devices::VibrateController;
using System::TimeSpan;

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
