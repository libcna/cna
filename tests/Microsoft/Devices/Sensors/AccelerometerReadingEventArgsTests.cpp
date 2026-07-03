// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "Microsoft/Devices/Sensors/AccelerometerReadingEventArgs.hpp"
#include "System/DateTimeOffset.hpp"
#include "System/EventArgs.hpp"

using Microsoft::Devices::Sensors::AccelerometerReadingEventArgs;
using System::DateTimeOffset;

TEST(AccelerometerReadingEventArgsTests, DefaultConstructorZeroValues)
{
    const AccelerometerReadingEventArgs e;
    EXPECT_EQ(e.getXProperty(), 0.0);
    EXPECT_EQ(e.getYProperty(), 0.0);
    EXPECT_EQ(e.getZProperty(), 0.0);
    EXPECT_EQ(e.getTimestampProperty(), DateTimeOffset());
}

TEST(AccelerometerReadingEventArgsTests, ParameterizedConstructorStoresValues)
{
    const DateTimeOffset ts(System::DateTime(1000000LL), System::TimeSpan::Zero);
    const AccelerometerReadingEventArgs e(1.0, 2.0, 3.0, ts);
    EXPECT_EQ(e.getXProperty(), 1.0);
    EXPECT_EQ(e.getYProperty(), 2.0);
    EXPECT_EQ(e.getZProperty(), 3.0);
    EXPECT_EQ(e.getTimestampProperty(), ts);
}

TEST(AccelerometerReadingEventArgsTests, SetX)
{
    AccelerometerReadingEventArgs e;
    e.setXProperty(0.5);
    EXPECT_EQ(e.getXProperty(), 0.5);
}

TEST(AccelerometerReadingEventArgsTests, SetY)
{
    AccelerometerReadingEventArgs e;
    e.setYProperty(-0.5);
    EXPECT_EQ(e.getYProperty(), -0.5);
}

TEST(AccelerometerReadingEventArgsTests, SetZ)
{
    AccelerometerReadingEventArgs e;
    e.setZProperty(1.5);
    EXPECT_EQ(e.getZProperty(), 1.5);
}

TEST(AccelerometerReadingEventArgsTests, SetTimestamp)
{
    AccelerometerReadingEventArgs e;
    const DateTimeOffset ts(System::DateTime(9999LL), System::TimeSpan::Zero);
    e.setTimestampProperty(ts);
    EXPECT_EQ(e.getTimestampProperty(), ts);
}

TEST(AccelerometerReadingEventArgsTests, EqualityOperatorEqualInstances)
{
    const DateTimeOffset ts(System::DateTime(500LL), System::TimeSpan::Zero);
    const AccelerometerReadingEventArgs a(1.0, 0.0, 0.0, ts);
    const AccelerometerReadingEventArgs b(1.0, 0.0, 0.0, ts);
    EXPECT_TRUE(a == b);
}

TEST(AccelerometerReadingEventArgsTests, EqualityOperatorUnequalAxisValue)
{
    const DateTimeOffset ts(System::DateTime(500LL), System::TimeSpan::Zero);
    const AccelerometerReadingEventArgs a(1.0, 0.0, 0.0, ts);
    const AccelerometerReadingEventArgs b(0.0, 1.0, 0.0, ts);
    EXPECT_FALSE(a == b);
}

TEST(AccelerometerReadingEventArgsTests, EqualityOperatorUnequalTimestamp)
{
    const AccelerometerReadingEventArgs a(
        1.0, 0.0, 0.0, DateTimeOffset(System::DateTime(100LL), System::TimeSpan::Zero));
    const AccelerometerReadingEventArgs b(
        1.0, 0.0, 0.0, DateTimeOffset(System::DateTime(200LL), System::TimeSpan::Zero));
    EXPECT_FALSE(a == b);
}

TEST(AccelerometerReadingEventArgsTests, InequalityOperatorComplementary)
{
    const DateTimeOffset ts(System::DateTime(500LL), System::TimeSpan::Zero);
    const AccelerometerReadingEventArgs a(1.0, 0.0, 0.0, ts);
    const AccelerometerReadingEventArgs b(1.0, 0.0, 0.0, ts);
    const AccelerometerReadingEventArgs c(2.0, 0.0, 0.0, ts);
    EXPECT_FALSE(a != b);
    EXPECT_TRUE(a != c);
}

TEST(AccelerometerReadingEventArgsTests, ToStringFormat)
{
    const AccelerometerReadingEventArgs e(1.0, 2.0, 3.0, DateTimeOffset());
    const std::string s = e.ToString();
    EXPECT_NE(s.find("X:1"), std::string::npos);
    EXPECT_NE(s.find("Y:2"), std::string::npos);
    EXPECT_NE(s.find("Z:3"), std::string::npos);
}

TEST(AccelerometerReadingEventArgsTests, GetHashCodeConsistency)
{
    const DateTimeOffset ts(System::DateTime(500LL), System::TimeSpan::Zero);
    const AccelerometerReadingEventArgs a(1.0, 2.0, 3.0, ts);
    const AccelerometerReadingEventArgs b(1.0, 2.0, 3.0, ts);
    EXPECT_EQ(a.GetHashCode(), b.GetHashCode());
}

TEST(AccelerometerReadingEventArgsTests, GetHashCodeDifferentForUnequalInstances)
{
    const DateTimeOffset ts(System::DateTime(500LL), System::TimeSpan::Zero);
    const AccelerometerReadingEventArgs a(1.0, 2.0, 3.0, ts);
    const AccelerometerReadingEventArgs b(4.0, 5.0, 6.0, ts);
    EXPECT_NE(a.GetHashCode(), b.GetHashCode());
}

TEST(AccelerometerReadingEventArgsTests, GetTypeName)
{
    const AccelerometerReadingEventArgs e;
    EXPECT_EQ(e.GetTypeName(), "Microsoft.Devices.Sensors.AccelerometerReadingEventArgs");
}

TEST(AccelerometerReadingEventArgsTests, UsableAsEventArgsReference)
{
    const AccelerometerReadingEventArgs e(1.0, 2.0, 3.0, DateTimeOffset());
    const System::EventArgs& base = e;
    EXPECT_EQ(&base, static_cast<const System::EventArgs*>(&e));
}
