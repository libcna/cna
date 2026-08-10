// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "Microsoft/Devices/Sensors/CalibrationEventArgs.hpp"
#include "System/EventArgs.hpp"

using Microsoft::Devices::Sensors::CalibrationEventArgs;

TEST(CalibrationEventArgsTests, DefaultConstructorDoesNotThrow)
{
    EXPECT_NO_THROW({ const CalibrationEventArgs e; (void)e; });
}

TEST(CalibrationEventArgsTests, UsableAsEventArgsReference)
{
    const CalibrationEventArgs e;
    const System::EventArgs& base = e;
    EXPECT_EQ(&base, static_cast<const System::EventArgs*>(&e));
}

TEST(CalibrationEventArgsTests, GetTypeName)
{
    const CalibrationEventArgs e;
    EXPECT_EQ(e.GetTypeName(), "Microsoft.Devices.Sensors.CalibrationEventArgs");
}
