// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "Microsoft/Devices/Sensors/MotionReading.hpp"
#include "System/DateTimeOffset.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

using Microsoft::Devices::Sensors::AttitudeReading;
using Microsoft::Devices::Sensors::MotionReading;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Quaternion;
using Microsoft::Xna::Framework::Vector3;
using System::DateTimeOffset;

TEST(MotionReadingTests, DefaultConstructorZeroValues)
{
    const MotionReading r;
    EXPECT_EQ(r.getDeviceAccelerationProperty(), Vector3::Zero);
    EXPECT_EQ(r.getDeviceRotationRateProperty(), Vector3::Zero);
    EXPECT_EQ(r.getGravityProperty(), Vector3::Zero);
    EXPECT_TRUE(r.getAttitudeProperty() == AttitudeReading());
    EXPECT_EQ(r.getTimestampProperty(), DateTimeOffset());
}

TEST(MotionReadingTests, ParameterizedConstructorStoresValues)
{
    const DateTimeOffset ts(System::DateTime(1000000LL), System::TimeSpan::Zero);
    const AttitudeReading attitude(1.0f, 2.0f, 3.0f, Quaternion::Identity, Matrix::getIdentityProperty(), ts);
    const Vector3 accel(1.0f, 2.0f, 3.0f);
    const Vector3 rotRate(4.0f, 5.0f, 6.0f);
    const Vector3 gravity(0.0f, -1.0f, 0.0f);
    const MotionReading r(attitude, accel, rotRate, gravity, ts);
    EXPECT_TRUE(r.getAttitudeProperty() == attitude);
    EXPECT_EQ(r.getDeviceAccelerationProperty(), accel);
    EXPECT_EQ(r.getDeviceRotationRateProperty(), rotRate);
    EXPECT_EQ(r.getGravityProperty(), gravity);
    EXPECT_EQ(r.getTimestampProperty(), ts);
}

// NOTE (Task P3-2): all setXProperty() methods on MotionReading are
// private + friend Motion as of this task, matching the real WP7 API's
// `internal set`, so they can no longer be exercised directly from this
// test file. Field storage/round-trip is still fully covered via
// ParameterizedConstructorStoresValues() above.

TEST(MotionReadingTests, EqualityOperatorEqualInstances)
{
    const DateTimeOffset ts(System::DateTime(500LL), System::TimeSpan::Zero);
    const Vector3 v(1.0f, 0.0f, 0.0f);
    const MotionReading a(AttitudeReading(), v, v, v, ts);
    const MotionReading b(AttitudeReading(), v, v, v, ts);
    EXPECT_TRUE(a == b);
}

TEST(MotionReadingTests, EqualityOperatorUnequalDeviceAcceleration)
{
    const DateTimeOffset ts(System::DateTime(500LL), System::TimeSpan::Zero);
    const Vector3 v(1.0f, 0.0f, 0.0f);
    const MotionReading a(AttitudeReading(), v, v, v, ts);
    const MotionReading b(AttitudeReading(), Vector3(9.0f, 0.0f, 0.0f), v, v, ts);
    EXPECT_FALSE(a == b);
}

// Task P3-11: EqualityOperatorUnequalDeviceAcceleration above only varies
// DeviceAcceleration, one of MotionReading's 5 fields. Vary Gravity too,
// independently.
TEST(MotionReadingTests, EqualityOperatorUnequalGravity)
{
    const DateTimeOffset ts(System::DateTime(500LL), System::TimeSpan::Zero);
    const Vector3 v(1.0f, 0.0f, 0.0f);
    const MotionReading a(AttitudeReading(), v, v, v, ts);
    const MotionReading b(AttitudeReading(), v, v, Vector3(0.0f, 9.0f, 0.0f), ts);
    EXPECT_FALSE(a == b);
}

TEST(MotionReadingTests, InequalityOperatorComplementary)
{
    const DateTimeOffset ts(System::DateTime(500LL), System::TimeSpan::Zero);
    const Vector3 v(1.0f, 0.0f, 0.0f);
    const MotionReading a(AttitudeReading(), v, v, v, ts);
    const MotionReading b(AttitudeReading(), v, v, v, ts);
    const MotionReading c(AttitudeReading(), Vector3(9.0f, 0.0f, 0.0f), v, v, ts);
    EXPECT_FALSE(a != b);
    EXPECT_TRUE(a != c);
}

TEST(MotionReadingTests, ToStringFormat)
{
    const Vector3 accel(1.0f, 2.0f, 3.0f);
    const Vector3 gravity(0.0f, -1.0f, 0.0f);
    const MotionReading r(AttitudeReading(), accel, Vector3::Zero, gravity, DateTimeOffset());
    const std::string s = r.ToString();
    EXPECT_NE(s.find("DeviceAcceleration:"), std::string::npos);
    EXPECT_NE(s.find("Gravity:"), std::string::npos);
}

TEST(MotionReadingTests, GetHashCodeConsistency)
{
    const DateTimeOffset ts(System::DateTime(500LL), System::TimeSpan::Zero);
    const Vector3 v(1.0f, 2.0f, 3.0f);
    const MotionReading a(AttitudeReading(), v, v, v, ts);
    const MotionReading b(AttitudeReading(), v, v, v, ts);
    EXPECT_EQ(a.GetHashCode(), b.GetHashCode());
}

TEST(MotionReadingTests, GetHashCodeDifferentForUnequalInstances)
{
    const DateTimeOffset ts(System::DateTime(500LL), System::TimeSpan::Zero);
    const MotionReading a(AttitudeReading(), Vector3(1.0f, 2.0f, 3.0f), Vector3(1.0f, 2.0f, 3.0f), Vector3(1.0f, 2.0f, 3.0f), ts);
    const MotionReading b(AttitudeReading(), Vector3(4.0f, 5.0f, 6.0f), Vector3(4.0f, 5.0f, 6.0f), Vector3(4.0f, 5.0f, 6.0f), ts);
    EXPECT_NE(a.GetHashCode(), b.GetHashCode());
}

TEST(MotionReadingTests, GetTypeName)
{
    const MotionReading r;
    EXPECT_EQ(r.GetTypeName(), "Microsoft.Devices.Sensors.MotionReading");
}
