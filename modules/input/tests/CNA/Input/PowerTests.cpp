// SPDX-License-Identifier: MS-PL
//
// PLAT-89: CNA::Input::Power reads the platform, not SDL.
//
// These used to inject a fake `ISystemPowerBackend` -- one of the eight per-subsystem seams
// PLAT-77 concluded should end up fewer rather than renamed. The coverage they gave is worth
// keeping exactly as it was, because CI has no battery and the state mapping is otherwise
// unobservable: what changed is that the canned answer now arrives through the platform contract,
// which is the seam that will still exist after the migration.

#include <gtest/gtest.h>

#include "CNA/Input/Power.hpp"
#include "CNA/Input/PowerState.hpp"
#include "CNA/Platform/PlatformTestDecorator.hpp"

using CNA::Input::Power;
using CNA::Input::PowerStateEXT;
using CNA::Platform::PlatformCapabilities;
using CNA::Platform::PowerInfo;
using CNA::Platform::PowerState;
using CNA::Platform::Testing::PlatformTestDecorator;
using CNA::Platform::Testing::ScopedCurrentPlatform;

namespace
{
    /// Reports a canned PowerInfo and forwards everything else to the real service, so the
    /// state mapping and the seconds/percent forwarding are checkable with no battery present.
    class CannedPowerSystemInfo final : public CNA::Platform::IPlatformSystemInfo
    {
    public:
        explicit CannedPowerSystemInfo(CNA::Platform::IPlatformSystemInfo& inner) : inner_(inner) {}

        PowerInfo canned;
        mutable int calls = 0;

        [[nodiscard]] PowerInfo GetPowerInfo() const override
        {
            ++calls;
            return canned;
        }

        [[nodiscard]] std::string GetPlatformName() const override
        {
            return inner_.GetPlatformName();
        }
        [[nodiscard]] int GetSystemMemoryMegabytes() const override
        {
            return inner_.GetSystemMemoryMegabytes();
        }
        [[nodiscard]] int GetLogicalCoreCount() const override
        {
            return inner_.GetLogicalCoreCount();
        }
        [[nodiscard]] std::vector<CNA::Platform::PlatformLocale> GetPreferredLocales() const override
        {
            return inner_.GetPreferredLocales();
        }
        bool OpenUrl(const std::string& url) override { return inner_.OpenUrl(url); }

    private:
        CNA::Platform::IPlatformSystemInfo& inner_;
    };

    class CannedPowerPlatform final : public PlatformTestDecorator
    {
    public:
        CannedPowerPlatform() : systemInfo_(*PlatformTestDecorator::GetSystemInfo()) {}

        [[nodiscard]] CNA::Platform::IPlatformSystemInfo* GetSystemInfo() override
        {
            return &systemInfo_;
        }

        CannedPowerSystemInfo& Canned() { return systemInfo_; }

    private:
        CannedPowerSystemInfo systemInfo_;
    };

    class CnaInputPowerTest : public ::testing::Test
    {
    protected:
        CannedPowerPlatform platform;
        std::unique_ptr<ScopedCurrentPlatform> installed;

        void SetUp() override { installed = std::make_unique<ScopedCurrentPlatform>(platform); }
        void TearDown() override { installed.reset(); }
    };
}

TEST_F(CnaInputPowerTest, GetInfoMapsStateAndForwardsSecondsAndPercent)
{
    struct Case { PowerState platform; PowerStateEXT ext; int seconds; int percent; };
    const Case cases[] = {
        {PowerState::OnBattery, PowerStateEXT::OnBattery, 3600, 85},
        {PowerState::NoBattery, PowerStateEXT::NoBattery, -1,   -1},
        {PowerState::Charging,  PowerStateEXT::Charging,  1800, 40},
        {PowerState::Charged,   PowerStateEXT::Charged,   -1,   100},
        {PowerState::Unknown,   PowerStateEXT::Unknown,   -1,   -1},
        {PowerState::Error,     PowerStateEXT::Error,     -1,   -1},
    };
    for (const Case& testCase : cases)
    {
        platform.Canned().canned = PowerInfo{testCase.platform, testCase.percent, testCase.seconds};

        int secondsLeft = 999;
        int percent = 999;
        EXPECT_EQ(Power::GetInfoEXT(secondsLeft, percent), testCase.ext);
        EXPECT_EQ(secondsLeft, testCase.seconds);
        EXPECT_EQ(percent, testCase.percent);
    }
}

TEST_F(CnaInputPowerTest, OutParamsCarryTheUnknownSentinelWhenNothingIsReported)
{
    // A platform with no battery information reports -1 for both, and the caller's prior values
    // must not survive: a stale 456 would render as a 456% charge.
    platform.Canned().canned = PowerInfo{};

    int secondsLeft = 123;
    int percent = 456;
    EXPECT_EQ(Power::GetInfoEXT(secondsLeft, percent), PowerStateEXT::Unknown);
    EXPECT_EQ(secondsLeft, -1);
    EXPECT_EQ(percent, -1);
}

TEST_F(CnaInputPowerTest, AllThreeAnswersComeFromASingleQuery)
{
    // PowerInfo carries state, percentage and runtime together. Querying three times could report
    // them from three different instants, which is visibly wrong the moment a charger is plugged
    // in mid-call -- "on battery, 100%, charging" is a state no device is ever in.
    platform.Canned().canned = PowerInfo{PowerState::Charging, 42, 1200};
    platform.Canned().calls = 0;

    int secondsLeft = 0;
    int percent = 0;
    (void)Power::GetInfoEXT(secondsLeft, percent);

    EXPECT_EQ(platform.Canned().calls, 1);
}

TEST_F(CnaInputPowerTest, TheRealPlatformAnswersWithinTheDocumentedRanges)
{
    // Without the canned service: whatever this machine reports must still satisfy the contract's
    // sentinels, so a platform implementation that invents its own "unknown" value is caught.
    installed.reset();

    int secondsLeft = 0;
    int percent = 0;
    const PowerStateEXT state = Power::GetInfoEXT(secondsLeft, percent);

    EXPECT_TRUE(state == PowerStateEXT::Error || state == PowerStateEXT::Unknown ||
                state == PowerStateEXT::OnBattery || state == PowerStateEXT::NoBattery ||
                state == PowerStateEXT::Charging || state == PowerStateEXT::Charged);
    EXPECT_TRUE(percent == -1 || (percent >= 0 && percent <= 100));
    EXPECT_TRUE(secondsLeft == -1 || secondsLeft >= 0);
}
