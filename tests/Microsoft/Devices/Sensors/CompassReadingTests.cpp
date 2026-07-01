// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "Microsoft/Devices/Sensors/CompassReading.hpp"
#include "System/DateTimeOffset.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

using Microsoft::Devices::Sensors::CompassReading;
using Microsoft::Xna::Framework::Vector3;
using System::DateTimeOffset;

TEST(CompassReadingTests, DefaultConstructorZeroValues)
{
    const CompassReading r;
    EXPECT_EQ(r.getHeadingAccuracyProperty(), 0.0);
    EXPECT_EQ(r.getMagneticHeadingProperty(), 0.0);
    EXPECT_EQ(r.getMagnetometerReadingProperty(), Vector3::Zero);
    EXPECT_EQ(r.getTimestampProperty(), DateTimeOffset());
    EXPECT_EQ(r.getTrueHeadingProperty(), 0.0);
}

TEST(CompassReadingTests, ParameterizedConstructorStoresValues)
{
    const DateTimeOffset ts(System::DateTime(1000000LL), System::TimeSpan::Zero);
    const Vector3 mag(1.0f, 2.0f, 3.0f);
    const CompassReading r(5.0, 90.0, mag, ts, 95.0);
    EXPECT_EQ(r.getHeadingAccuracyProperty(), 5.0);
    EXPECT_EQ(r.getMagneticHeadingProperty(), 90.0);
    EXPECT_EQ(r.getMagnetometerReadingProperty(), mag);
    EXPECT_EQ(r.getTimestampProperty(), ts);
    EXPECT_EQ(r.getTrueHeadingProperty(), 95.0);
}

TEST(CompassReadingTests, SetHeadingAccuracy)
{
    CompassReading r;
    r.setHeadingAccuracyProperty(3.5);
    EXPECT_EQ(r.getHeadingAccuracyProperty(), 3.5);
}

TEST(CompassReadingTests, SetMagneticHeading)
{
    CompassReading r;
    r.setMagneticHeadingProperty(180.0);
    EXPECT_EQ(r.getMagneticHeadingProperty(), 180.0);
}

TEST(CompassReadingTests, SetMagnetometerReading)
{
    CompassReading r;
    const Vector3 v(0.5f, -0.5f, 1.0f);
    r.setMagnetometerReadingProperty(v);
    EXPECT_EQ(r.getMagnetometerReadingProperty(), v);
}

TEST(CompassReadingTests, SetTimestamp)
{
    CompassReading r;
    const DateTimeOffset ts(System::DateTime(9999LL), System::TimeSpan::Zero);
    r.setTimestampProperty(ts);
    EXPECT_EQ(r.getTimestampProperty(), ts);
}

TEST(CompassReadingTests, SetTrueHeading)
{
    CompassReading r;
    r.setTrueHeadingProperty(270.0);
    EXPECT_EQ(r.getTrueHeadingProperty(), 270.0);
}

TEST(CompassReadingTests, EqualityOperatorEqualInstances)
{
    const DateTimeOffset ts(System::DateTime(500LL), System::TimeSpan::Zero);
    const Vector3 mag(1.0f, 0.0f, 0.0f);
    const CompassReading a(1.0, 2.0, mag, ts, 3.0);
    const CompassReading b(1.0, 2.0, mag, ts, 3.0);
    EXPECT_TRUE(a == b);
}

TEST(CompassReadingTests, EqualityOperatorUnequalHeadingAccuracy)
{
    const DateTimeOffset ts(System::DateTime(500LL), System::TimeSpan::Zero);
    const Vector3 mag(1.0f, 0.0f, 0.0f);
    const CompassReading a(1.0, 2.0, mag, ts, 3.0);
    const CompassReading b(9.0, 2.0, mag, ts, 3.0);
    EXPECT_FALSE(a == b);
}

TEST(CompassReadingTests, InequalityOperatorComplementary)
{
    const DateTimeOffset ts(System::DateTime(500LL), System::TimeSpan::Zero);
    const Vector3 mag(1.0f, 0.0f, 0.0f);
    const CompassReading a(1.0, 2.0, mag, ts, 3.0);
    const CompassReading b(1.0, 2.0, mag, ts, 3.0);
    const CompassReading c(9.0, 2.0, mag, ts, 3.0);
    EXPECT_FALSE(a != b);
    EXPECT_TRUE(a != c);
}

TEST(CompassReadingTests, ToStringFormat)
{
    const DateTimeOffset ts(System::DateTime(500LL), System::TimeSpan::Zero);
    const CompassReading r(1.0, 2.0, Vector3(3.0f, 4.0f, 5.0f), ts, 6.0);
    const std::string s = r.ToString();
    EXPECT_NE(s.find("MagneticHeading:2"), std::string::npos);
    EXPECT_NE(s.find("TrueHeading:6"), std::string::npos);
    EXPECT_NE(s.find("HeadingAccuracy:1"), std::string::npos);
    EXPECT_NE(s.find("MagnetometerReading:"), std::string::npos);
}

TEST(CompassReadingTests, GetHashCodeConsistency)
{
    const DateTimeOffset ts(System::DateTime(500LL), System::TimeSpan::Zero);
    const Vector3 mag(1.0f, 2.0f, 3.0f);
    const CompassReading a(1.0, 2.0, mag, ts, 3.0);
    const CompassReading b(1.0, 2.0, mag, ts, 3.0);
    EXPECT_EQ(a.GetHashCode(), b.GetHashCode());
}

TEST(CompassReadingTests, GetTypeName)
{
    const CompassReading r;
    EXPECT_EQ(r.GetTypeName(), "Microsoft.Devices.Sensors.CompassReading");
}
