// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>

#include "CNA/TargetPlatform.hpp"
#include "Microsoft/Devices/DeviceType.hpp"
#include "Microsoft/Devices/Environment.hpp"

using Microsoft::Devices::DeviceType;
using Microsoft::Devices::Environment;

TEST(EnvironmentTests, DeviceTypeDefinesDeviceAndEmulatorAsDistinctValues)
{
    EXPECT_NE(DeviceType::Device, DeviceType::Emulator);
}

TEST(EnvironmentTests, DeviceTypePropertyMatchesTheCurrentHostKind)
{
    const DeviceType expected = CNA::isMobilePlatform()
        ? DeviceType::Device
        : DeviceType::Emulator;
    EXPECT_EQ(Environment::getDeviceTypeProperty(), expected);
}

TEST(EnvironmentTests, DesktopUsesTheEmulatorInputPath)
{
    if (CNA::getCurrentPlatform() == CNA::TargetPlatform::Desktop)
    {
        EXPECT_EQ(Environment::getDeviceTypeProperty(), DeviceType::Emulator);
    }
}
