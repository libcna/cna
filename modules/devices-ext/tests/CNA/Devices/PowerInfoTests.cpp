// SPDX-License-Identifier: MS-PL
#ifdef CNA_DEVICES

#include <gtest/gtest.h>

#include "CNA/Devices/PowerInfo.hpp"
#include "CNA/Platform/CannedSystemInfo.hpp"

using CNA::Devices::PowerInfo;
using CNA::Devices::PowerState;

// Oracle: PowerInfo.hpp forwards platform battery state and unknown sentinels.

namespace
{
    class PowerInfoTests : public ::testing::Test
    {
    protected:
        CNA::Platform::Testing::CannedSystemInfoPlatform platform;
        CNA::Platform::Testing::ScopedCurrentPlatform current{platform};
    };
}

TEST_F(PowerInfoTests, GetStatePropertyDoesNotCrashAndReturnsValidEnumValue)
{
    const PowerState state = PowerInfo::getStateProperty();
    EXPECT_TRUE(
        state == PowerState::Error || state == PowerState::Unknown ||
        state == PowerState::OnBattery || state == PowerState::NoBattery ||
        state == PowerState::Charging || state == PowerState::Charged);
}

TEST_F(PowerInfoTests, GetBatteryPercentPropertyIsNegativeOneOrAValidPercentage)
{
    const int percent = PowerInfo::getBatteryPercentProperty();
    EXPECT_TRUE(percent == -1 || (percent >= 0 && percent <= 100));
}

TEST_F(PowerInfoTests, GetSecondsRemainingPropertyIsNegativeOneOrNonNegative)
{
    const int seconds = PowerInfo::getSecondsRemainingProperty();
    EXPECT_TRUE(seconds == -1 || seconds >= 0);
}

TEST_F(PowerInfoTests, RepeatedCallsDoNotCrash)
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

TEST_F(PowerInfoTests, EveryPlatformStateMapsToTheCorrespondingPublicState)
{
    using Native = CNA::Platform::PowerState;
    const std::pair<Native, PowerState> cases[]{
        {Native::Error, PowerState::Error}, {Native::Unknown, PowerState::Unknown},
        {Native::OnBattery, PowerState::OnBattery}, {Native::NoBattery, PowerState::NoBattery},
        {Native::Charging, PowerState::Charging}, {Native::Charged, PowerState::Charged}};
    for (const auto& [native, expected] : cases)
    {
        platform.power.state = native;
        EXPECT_EQ(PowerInfo::getStateProperty(), expected);
    }
}

TEST_F(PowerInfoTests, UnknownAndBoundaryBatteryValuesAreForwardedExactly)
{
    // Each query must read the current provider value, rather than cache an earlier reading.
    for (const int percent : {-1, 0, 37, 100})
    {
        platform.power.percent = percent;
        EXPECT_EQ(PowerInfo::getBatteryPercentProperty(), percent);
    }
    for (const int seconds : {-1, 0, 12345})
    {
        platform.power.secondsRemaining = seconds;
        EXPECT_EQ(PowerInfo::getSecondsRemainingProperty(), seconds);
    }
}

#endif // CNA_DEVICES
