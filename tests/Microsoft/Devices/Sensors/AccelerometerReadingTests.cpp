// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "Microsoft/Devices/Sensors/AccelerometerReading.hpp"
#include "System/DateTimeOffset.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

using Microsoft::Devices::Sensors::AccelerometerReading;
using Microsoft::Xna::Framework::Vector3;
using System::DateTimeOffset;

TEST(AccelerometerReadingTests, DefaultConstructorZeroValues)
{
    const AccelerometerReading r;
    EXPECT_EQ(r.getAccelerationProperty(), Vector3::Zero);
    EXPECT_EQ(r.getTimestampProperty(), DateTimeOffset());
}

TEST(AccelerometerReadingTests, ParameterizedConstructorStoresValues)
{
    const DateTimeOffset ts(System::DateTime(1000000LL), System::TimeSpan::Zero);
    const Vector3 accel(1.0f, 2.0f, 3.0f);
    const AccelerometerReading r(ts, accel);
    EXPECT_EQ(r.getAccelerationProperty(), accel);
    EXPECT_EQ(r.getTimestampProperty(), ts);
}

// NOTE (Task P3-2): setAccelerationProperty()/setTimestampProperty() are
// private + friend Accelerometer as of this task, matching the real WP7
// API's `internal set`, so they can no longer be exercised directly from
// this test file. Field storage/round-trip is still fully covered via
// ParameterizedConstructorStoresValues() above (same underlying storage,
// just set through the constructor's initializer list instead of the
// now-private setters).

TEST(AccelerometerReadingTests, EqualityOperatorEqualInstances)
{
    const DateTimeOffset ts(System::DateTime(500LL), System::TimeSpan::Zero);
    const Vector3 accel(1.0f, 0.0f, 0.0f);
    const AccelerometerReading a(ts, accel);
    const AccelerometerReading b(ts, accel);
    EXPECT_TRUE(a == b);
}

TEST(AccelerometerReadingTests, EqualityOperatorUnequalAcceleration)
{
    const DateTimeOffset ts(System::DateTime(500LL), System::TimeSpan::Zero);
    const AccelerometerReading a(ts, Vector3(1.0f, 0.0f, 0.0f));
    const AccelerometerReading b(ts, Vector3(0.0f, 1.0f, 0.0f));
    EXPECT_FALSE(a == b);
}

TEST(AccelerometerReadingTests, EqualityOperatorUnequalTimestamp)
{
    const Vector3 accel(1.0f, 0.0f, 0.0f);
    const AccelerometerReading a(DateTimeOffset(System::DateTime(100LL), System::TimeSpan::Zero), accel);
    const AccelerometerReading b(DateTimeOffset(System::DateTime(200LL), System::TimeSpan::Zero), accel);
    EXPECT_FALSE(a == b);
}

TEST(AccelerometerReadingTests, InequalityOperatorComplementary)
{
    const DateTimeOffset ts(System::DateTime(500LL), System::TimeSpan::Zero);
    const Vector3 accel(1.0f, 0.0f, 0.0f);
    const AccelerometerReading a(ts, accel);
    const AccelerometerReading b(ts, accel);
    const AccelerometerReading c(ts, Vector3(2.0f, 0.0f, 0.0f));
    EXPECT_FALSE(a != b);
    EXPECT_TRUE(a != c);
}

TEST(AccelerometerReadingTests, ToStringFormat)
{
    AccelerometerReading r;
    const std::string s = r.ToString();
    EXPECT_NE(s.find("Acceleration:"), std::string::npos);
}

TEST(AccelerometerReadingTests, GetHashCodeConsistency)
{
    const DateTimeOffset ts(System::DateTime(500LL), System::TimeSpan::Zero);
    const Vector3 accel(1.0f, 2.0f, 3.0f);
    const AccelerometerReading a(ts, accel);
    const AccelerometerReading b(ts, accel);
    EXPECT_EQ(a.GetHashCode(), b.GetHashCode());
}

TEST(AccelerometerReadingTests, GetTypeName)
{
    const AccelerometerReading r;
    EXPECT_EQ(r.GetTypeName(), "Microsoft.Devices.Sensors.AccelerometerReading");
}
