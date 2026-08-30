// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>

#include "Microsoft/Devices/DeviceType.hpp"
#include "Microsoft/Devices/Environment.hpp"

using Microsoft::Devices::DeviceType;
using Microsoft::Devices::Environment;

TEST(EnvironmentTests, DeviceTypeDefinesDeviceAndEmulatorAsDistinctValues)
{
    EXPECT_NE(DeviceType::Device, DeviceType::Emulator);
}

TEST(EnvironmentTests, DeviceTypePropertyReportsPhysicalDeviceOnSupportedHosts)
{
    EXPECT_EQ(Environment::getDeviceTypeProperty(), DeviceType::Device);
}
