// SPDX-License-Identifier: MS-PL
#ifdef CNA_DEVICES

#include <gtest/gtest.h>

#include "CNA/Devices/SystemInfo.hpp"

using CNA::Devices::SystemInfo;

TEST(SystemInfoTests, GetLogicalCpuCoreCountPropertyIsAtLeastOne)
{
    EXPECT_GE(SystemInfo::getLogicalCpuCoreCountProperty(), 1);
}

TEST(SystemInfoTests, GetSystemRamMegabytesPropertyIsPositive)
{
    EXPECT_GT(SystemInfo::getSystemRamMegabytesProperty(), 0);
}

TEST(SystemInfoTests, RepeatedCallsReturnTheSameValue)
{
    // Neither value is expected to change during a single process's lifetime.
    const int firstCoreCount = SystemInfo::getLogicalCpuCoreCountProperty();
    const int secondCoreCount = SystemInfo::getLogicalCpuCoreCountProperty();
    EXPECT_EQ(firstCoreCount, secondCoreCount);

    const int firstRam = SystemInfo::getSystemRamMegabytesProperty();
    const int secondRam = SystemInfo::getSystemRamMegabytesProperty();
    EXPECT_EQ(firstRam, secondRam);
}

#endif // CNA_DEVICES
