// SPDX-License-Identifier: MS-PL
#ifdef CNA_DEVICES

#include <gtest/gtest.h>

#include "CNA/Devices/SystemInfo.hpp"
#include "CNA/Platform/CannedSystemInfo.hpp"

using CNA::Devices::SystemInfo;

namespace
{
    class SystemInfoTests : public ::testing::Test
    {
    protected:
        CNA::Platform::Testing::CannedSystemInfoPlatform platform;
        CNA::Platform::Testing::ScopedCurrentPlatform current{platform};
    };
}

TEST_F(SystemInfoTests, GetLogicalCpuCoreCountPropertyIsAtLeastOne)
{
    EXPECT_GE(SystemInfo::getLogicalCpuCoreCountProperty(), 1);
}

TEST_F(SystemInfoTests, GetSystemRamMegabytesPropertyIsPositive)
{
    EXPECT_GT(SystemInfo::getSystemRamMegabytesProperty(), 0);
}

TEST_F(SystemInfoTests, RepeatedCallsReturnTheSameValue)
{
    // Neither value is expected to change during a single process's lifetime.
    const int firstCoreCount = SystemInfo::getLogicalCpuCoreCountProperty();
    const int secondCoreCount = SystemInfo::getLogicalCpuCoreCountProperty();
    EXPECT_EQ(firstCoreCount, secondCoreCount);

    const int firstRam = SystemInfo::getSystemRamMegabytesProperty();
    const int secondRam = SystemInfo::getSystemRamMegabytesProperty();
    EXPECT_EQ(firstRam, secondRam);
}

TEST_F(SystemInfoTests, ExactHostValuesAndUnknownZeroAreNotSwappedOrFabricated)
{
    // Oracle: SystemInfo.hpp and IPlatformSystemInfo: CPU count and RAM in MiB; zero is unknown.
    EXPECT_EQ(SystemInfo::getLogicalCpuCoreCountProperty(), 7);
    EXPECT_EQ(SystemInfo::getSystemRamMegabytesProperty(), 12345);
    platform.cores = 0;
    platform.memoryMegabytes = 0;
    EXPECT_EQ(SystemInfo::getLogicalCpuCoreCountProperty(), 0);
    EXPECT_EQ(SystemInfo::getSystemRamMegabytesProperty(), 0);
}

#endif // CNA_DEVICES
