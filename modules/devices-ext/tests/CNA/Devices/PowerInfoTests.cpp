// SPDX-License-Identifier: MS-PL
#ifdef CNA_DEVICES

#include <gtest/gtest.h>

#include "CNA/Devices/PowerInfo.hpp"

using CNA::Devices::PowerInfo;
using CNA::Devices::PowerState;

// This headless container has no real battery, so getStateProperty() is expected to
// report NoBattery/Unknown/Error depending on what the host reports -- these tests
// assert the *contract* (valid enum, documented sentinel ranges), not a specific
// battery-having value, since none is available to observe here.

TEST(PowerInfoTests, GetStatePropertyDoesNotCrashAndReturnsValidEnumValue)
{
    const PowerState state = PowerInfo::getStateProperty();
    EXPECT_TRUE(
        state == PowerState::Error || state == PowerState::Unknown ||
        state == PowerState::OnBattery || state == PowerState::NoBattery ||
        state == PowerState::Charging || state == PowerState::Charged);
}

TEST(PowerInfoTests, GetBatteryPercentPropertyIsNegativeOneOrAValidPercentage)
{
    const int percent = PowerInfo::getBatteryPercentProperty();
    EXPECT_TRUE(percent == -1 || (percent >= 0 && percent <= 100));
}

TEST(PowerInfoTests, GetSecondsRemainingPropertyIsNegativeOneOrNonNegative)
{
    const int seconds = PowerInfo::getSecondsRemainingProperty();
    EXPECT_TRUE(seconds == -1 || seconds >= 0);
}

TEST(PowerInfoTests, RepeatedCallsDoNotCrash)
{
    for (int i = 0; i < 10; ++i)
    {
        EXPECT_NO_THROW({
            (void)PowerInfo::getStateProperty();
            (void)PowerInfo::getBatteryPercentProperty();
            (void)PowerInfo::getSecondsRemainingProperty();
        });
    }
}

#endif // CNA_DEVICES
