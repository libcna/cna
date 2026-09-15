// SPDX-License-Identifier: MS-PL
//
// plans/plan_x11.md X11-0163: host facts from Linux itself -- battery state from the kernel's
// power-supply class, read here from power-supply trees the tests build, and the preferred locales
// from the POSIX variables. No device, no display.

#include <gtest/gtest.h>

#ifdef CNA_PLATFORM_HAVE_EVDEV

#include "../../../src/Linux/LinuxSystemInfo.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include <unistd.h>

namespace {

using namespace CNA::Platform;
using CNA::Platform::Linux::LinuxSystemInfo;
using CNA::Platform::Linux::ParsePreferredLocales;
using CNA::Platform::Linux::ReadPowerSupplies;

/// A power-supply class directory of the test's own.
class PowerSupplies
{
public:
    PowerSupplies()
        : root_(std::filesystem::temp_directory_path() /
                ("cna-power-supply-" + std::to_string(::getpid()) + "-" + std::to_string(++count_)))
    {
        std::filesystem::create_directories(root_);
    }
    ~PowerSupplies() { std::filesystem::remove_all(root_); }
    PowerSupplies(const PowerSupplies&) = delete;
    PowerSupplies& operator=(const PowerSupplies&) = delete;

    void Add(const std::string& name, std::initializer_list<std::pair<const char*, const char*>> attributes)
    {
        const std::filesystem::path supply = root_ / name;
        std::filesystem::create_directories(supply);
        for (const auto& [attribute, value] : attributes)
        {
            std::ofstream(supply / attribute) << value << "\n";
        }
    }

    [[nodiscard]] std::string Root() const { return root_.string(); }

private:
    static inline int count_ = 0;
    std::filesystem::path root_;
};

TEST(LinuxSystemInfo, AMachineWithoutABatteryIsPluggedIn)
{
    PowerSupplies supplies;
    supplies.Add("AC", {{"type", "Mains"}, {"online", "1"}});
    const PowerInfo info = ReadPowerSupplies(supplies.Root());
    EXPECT_EQ(info.state, PowerState::NoBattery);
    EXPECT_EQ(info.percent, -1);
    EXPECT_EQ(info.secondsRemaining, -1);
}

TEST(LinuxSystemInfo, ADischargingBatteryReportsItsChargeAndTimeLeft)
{
    PowerSupplies supplies;
    supplies.Add("AC", {{"type", "Mains"}, {"online", "0"}});
    // 30 Wh left at 10 W: three hours.
    supplies.Add("BAT0", {{"type", "Battery"}, {"status", "Discharging"}, {"capacity", "57"},
                          {"energy_now", "30000000"}, {"power_now", "10000000"}});
    const PowerInfo info = ReadPowerSupplies(supplies.Root());
    EXPECT_EQ(info.state, PowerState::OnBattery);
    EXPECT_EQ(info.percent, 57);
    EXPECT_EQ(info.secondsRemaining, 3 * 3600);
}

TEST(LinuxSystemInfo, ChargeOverCurrentWhenABatteryReportsNoEnergy)
{
    PowerSupplies supplies;
    // 2000 mAh left at 1000 mA: two hours.
    supplies.Add("BAT1", {{"type", "Battery"}, {"status", "Discharging"}, {"capacity", "40"},
                          {"charge_now", "2000000"}, {"current_now", "1000000"}});
    EXPECT_EQ(ReadPowerSupplies(supplies.Root()).secondsRemaining, 2 * 3600);
}

TEST(LinuxSystemInfo, TheBatterysStatusNamesTheState)
{
    for (const auto& [status, expected] : std::vector<std::pair<const char*, PowerState>>{
             {"Charging", PowerState::Charging},
             {"Full", PowerState::Charged},
             {"Not charging", PowerState::Charged},  // plugged in, held at a charge threshold
             {"Unknown", PowerState::Unknown}})
    {
        PowerSupplies supplies;
        supplies.Add("BAT0", {{"type", "Battery"}, {"status", status}, {"capacity", "80"}});
        const PowerInfo info = ReadPowerSupplies(supplies.Root());
        EXPECT_EQ(info.state, expected) << status;
        EXPECT_EQ(info.percent, 80) << status;
        EXPECT_EQ(info.secondsRemaining, -1) << status << ": time left only while discharging";
    }
}

TEST(LinuxSystemInfo, ADevicesOwnBatteryAndAnAbsentOneAreNotTheMachines)
{
    PowerSupplies supplies;
    supplies.Add("sony_controller_battery", {{"type", "Battery"}, {"scope", "Device"},
                                             {"status", "Discharging"}, {"capacity", "20"}});
    supplies.Add("BAT0", {{"type", "Battery"}, {"present", "0"}});
    const PowerInfo info = ReadPowerSupplies(supplies.Root());
    EXPECT_EQ(info.state, PowerState::NoBattery);
    EXPECT_EQ(info.percent, -1);
}

TEST(LinuxSystemInfo, OfTwoBatteriesTheOneWithMoreTimeLeftOrMoreChargeWins)
{
    PowerSupplies timed;
    timed.Add("BAT0", {{"type", "Battery"}, {"status", "Discharging"}, {"capacity", "90"},
                       {"time_to_empty_now", "600"}});
    timed.Add("BAT1", {{"type", "Battery"}, {"status", "Discharging"}, {"capacity", "30"},
                       {"time_to_empty_now", "7200"}});
    EXPECT_EQ(ReadPowerSupplies(timed.Root()).secondsRemaining, 7200);
    EXPECT_EQ(ReadPowerSupplies(timed.Root()).percent, 30);

    PowerSupplies untimed;
    untimed.Add("BAT0", {{"type", "Battery"}, {"status", "Charging"}, {"capacity", "35"}});
    untimed.Add("BAT1", {{"type", "Battery"}, {"status", "Charging"}, {"capacity", "150"}});
    EXPECT_EQ(ReadPowerSupplies(untimed.Root()).percent, 100) << "the fullest, clamped";
}

TEST(LinuxSystemInfo, WithoutThePowerSupplyClassNothingIsKnown)
{
    const PowerInfo info = ReadPowerSupplies("/nonexistent/cna/power_supply");
    EXPECT_EQ(info.state, PowerState::Unknown);
    EXPECT_EQ(info.percent, -1);
}

TEST(LinuxSystemInfo, LocalesComeFromLangThenLanguage)
{
    const std::vector<PlatformLocale> locales =
        ParsePreferredLocales("cs_CZ.UTF-8", "en_GB:en:de_DE@euro:cs_CZ");
    ASSERT_EQ(locales.size(), 4u);
    EXPECT_EQ(locales[0].language, "cs");
    EXPECT_EQ(locales[0].country, "CZ");
    EXPECT_EQ(locales[1].language, "en");
    EXPECT_EQ(locales[1].country, "GB");
    EXPECT_EQ(locales[2].language, "en");
    EXPECT_EQ(locales[2].country, "");
    EXPECT_EQ(locales[3].language, "de");
    EXPECT_EQ(locales[3].country, "DE") << "cs_CZ again is not listed twice";

    EXPECT_TRUE(ParsePreferredLocales("C.UTF-8", nullptr).empty()) << "the C locale says nothing";
    EXPECT_TRUE(ParsePreferredLocales("POSIX", "").empty());
    EXPECT_TRUE(ParsePreferredLocales(nullptr, nullptr).empty());
}

TEST(LinuxSystemInfo, TheMachineAnswersFromItsOwnKernel)
{
    // This machine's own answers, read not fabricated: memory and processors are there on every
    // Linux; the battery is whatever the kernel reports here, and is printed for the record.
    LinuxSystemInfo info;
    EXPECT_EQ(info.GetPlatformName(), "Linux");
    EXPECT_GT(info.GetSystemMemoryMegabytes(), 0);
    EXPECT_GT(info.GetLogicalCoreCount(), 0);
    EXPECT_FALSE(info.OpenUrl("https://example.com")) << "no program is started on a game's behalf";
    const PowerInfo power = info.GetPowerInfo();
    EXPECT_TRUE(power.percent == -1 || (power.percent >= 0 && power.percent <= 100));
    std::printf("[          ] %d MB, %d cores, power state %d, %d%%, %d s\n", info.GetSystemMemoryMegabytes(),
                info.GetLogicalCoreCount(), static_cast<int>(power.state), power.percent,
                power.secondsRemaining);
}

} // namespace

#endif // CNA_PLATFORM_HAVE_EVDEV
