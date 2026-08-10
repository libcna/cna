// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "Microsoft/Devices/Sensors/GyroscopeReading.hpp"
#include "System/DateTimeOffset.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

using Microsoft::Devices::Sensors::GyroscopeReading;
using Microsoft::Xna::Framework::Vector3;
using System::DateTimeOffset;

TEST(GyroscopeReadingTests, DefaultConstructorZeroValues)
{
    const GyroscopeReading r;
    EXPECT_EQ(r.getRotationRateProperty(), Vector3::Zero);
    EXPECT_EQ(r.getTimestampProperty(), DateTimeOffset());
}

TEST(GyroscopeReadingTests, ParameterizedConstructorStoresValues)
{
    const DateTimeOffset ts(System::DateTime(1000000LL), System::TimeSpan::Zero);
    const Vector3 rate(1.0f, 2.0f, 3.0f);
    const GyroscopeReading r(rate, ts);
    EXPECT_EQ(r.getRotationRateProperty(), rate);
    EXPECT_EQ(r.getTimestampProperty(), ts);
}

// NOTE (Task P3-2): setRotationRateProperty()/setTimestampProperty() are
// private + friend Gyroscope as of this task, matching the real WP7 API's
// `internal set`, so they can no longer be exercised directly from this
// test file. Field storage/round-trip is still fully covered via
// ParameterizedConstructorStoresValues() above.

TEST(GyroscopeReadingTests, EqualityOperatorEqualInstances)
{
    const DateTimeOffset ts(System::DateTime(500LL), System::TimeSpan::Zero);
    const Vector3 rate(1.0f, 0.0f, 0.0f);
    const GyroscopeReading a(rate, ts);
    const GyroscopeReading b(rate, ts);
    EXPECT_TRUE(a == b);
}

TEST(GyroscopeReadingTests, EqualityOperatorUnequalRotationRate)
{
    const DateTimeOffset ts(System::DateTime(500LL), System::TimeSpan::Zero);
    const GyroscopeReading a(Vector3(1.0f, 0.0f, 0.0f), ts);
    const GyroscopeReading b(Vector3(0.0f, 1.0f, 0.0f), ts);
    EXPECT_FALSE(a == b);
}

TEST(GyroscopeReadingTests, EqualityOperatorUnequalTimestamp)
{
    const Vector3 rate(1.0f, 0.0f, 0.0f);
    const GyroscopeReading a(rate, DateTimeOffset(System::DateTime(100LL), System::TimeSpan::Zero));
    const GyroscopeReading b(rate, DateTimeOffset(System::DateTime(200LL), System::TimeSpan::Zero));
    EXPECT_FALSE(a == b);
}

TEST(GyroscopeReadingTests, InequalityOperatorComplementary)
{
    const DateTimeOffset ts(System::DateTime(500LL), System::TimeSpan::Zero);
    const Vector3 rate(1.0f, 0.0f, 0.0f);
    const GyroscopeReading a(rate, ts);
    const GyroscopeReading b(rate, ts);
    const GyroscopeReading c(Vector3(2.0f, 0.0f, 0.0f), ts);
    EXPECT_FALSE(a != b);
    EXPECT_TRUE(a != c);
}

TEST(GyroscopeReadingTests, ToStringFormat)
{
    GyroscopeReading r;
    const std::string s = r.ToString();
    EXPECT_NE(s.find("RotationRate:"), std::string::npos);
}

TEST(GyroscopeReadingTests, GetHashCodeConsistency)
{
    const DateTimeOffset ts(System::DateTime(500LL), System::TimeSpan::Zero);
    const Vector3 rate(1.0f, 2.0f, 3.0f);
    const GyroscopeReading a(rate, ts);
    const GyroscopeReading b(rate, ts);
    EXPECT_EQ(a.GetHashCode(), b.GetHashCode());
}

TEST(GyroscopeReadingTests, GetHashCodeDifferentForUnequalInstances)
{
    const DateTimeOffset ts(System::DateTime(500LL), System::TimeSpan::Zero);
    const GyroscopeReading a(Vector3(1.0f, 2.0f, 3.0f), ts);
    const GyroscopeReading b(Vector3(4.0f, 5.0f, 6.0f), ts);
    EXPECT_NE(a.GetHashCode(), b.GetHashCode());
}

TEST(GyroscopeReadingTests, GetTypeName)
{
    const GyroscopeReading r;
    EXPECT_EQ(r.GetTypeName(), "Microsoft.Devices.Sensors.GyroscopeReading");
}
