// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "Microsoft/Devices/Sensors/AttitudeReading.hpp"
#include "System/DateTimeOffset.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"

using Microsoft::Devices::Sensors::AttitudeReading;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Quaternion;
using System::DateTimeOffset;

TEST(AttitudeReadingTests, DefaultConstructorIdentityValues)
{
    const AttitudeReading r;
    EXPECT_EQ(r.getPitchProperty(), 0.0f);
    EXPECT_EQ(r.getRollProperty(), 0.0f);
    EXPECT_EQ(r.getYawProperty(), 0.0f);
    EXPECT_TRUE(r.getQuaternionProperty() == Quaternion::Identity);
    EXPECT_TRUE(r.getRotationMatrixProperty() == Matrix::getIdentityProperty());
    EXPECT_EQ(r.getTimestampProperty(), DateTimeOffset());
}

TEST(AttitudeReadingTests, ParameterizedConstructorStoresValues)
{
    const DateTimeOffset ts(System::DateTime(1000000LL), System::TimeSpan::Zero);
    const Quaternion q(0.0f, 0.0f, 0.0f, 1.0f);
    const Matrix m = Matrix::getIdentityProperty();
    const AttitudeReading r(1.0f, 2.0f, 3.0f, q, m, ts);
    EXPECT_EQ(r.getPitchProperty(), 1.0f);
    EXPECT_EQ(r.getRollProperty(), 2.0f);
    EXPECT_EQ(r.getYawProperty(), 3.0f);
    EXPECT_TRUE(r.getQuaternionProperty() == q);
    EXPECT_TRUE(r.getRotationMatrixProperty() == m);
    EXPECT_EQ(r.getTimestampProperty(), ts);
}

// NOTE (Task P3-2): all setXProperty() methods on AttitudeReading are
// private + friend Motion as of this task, matching the real WP7 API's
// `internal set`, so they can no longer be exercised directly from this
// test file. Field storage/round-trip is still fully covered via
// ParameterizedConstructorStoresValues() above.

TEST(AttitudeReadingTests, EqualityOperatorEqualInstances)
{
    const DateTimeOffset ts(System::DateTime(500LL), System::TimeSpan::Zero);
    const AttitudeReading a(1.0f, 2.0f, 3.0f, Quaternion::Identity, Matrix::getIdentityProperty(), ts);
    const AttitudeReading b(1.0f, 2.0f, 3.0f, Quaternion::Identity, Matrix::getIdentityProperty(), ts);
    EXPECT_TRUE(a == b);
}

TEST(AttitudeReadingTests, EqualityOperatorUnequalPitch)
{
    const DateTimeOffset ts(System::DateTime(500LL), System::TimeSpan::Zero);
    const AttitudeReading a(1.0f, 2.0f, 3.0f, Quaternion::Identity, Matrix::getIdentityProperty(), ts);
    const AttitudeReading b(9.0f, 2.0f, 3.0f, Quaternion::Identity, Matrix::getIdentityProperty(), ts);
    EXPECT_FALSE(a == b);
}

TEST(AttitudeReadingTests, InequalityOperatorComplementary)
{
    const DateTimeOffset ts(System::DateTime(500LL), System::TimeSpan::Zero);
    const AttitudeReading a(1.0f, 2.0f, 3.0f, Quaternion::Identity, Matrix::getIdentityProperty(), ts);
    const AttitudeReading b(1.0f, 2.0f, 3.0f, Quaternion::Identity, Matrix::getIdentityProperty(), ts);
    const AttitudeReading c(9.0f, 2.0f, 3.0f, Quaternion::Identity, Matrix::getIdentityProperty(), ts);
    EXPECT_FALSE(a != b);
    EXPECT_TRUE(a != c);
}

TEST(AttitudeReadingTests, ToStringFormat)
{
    const AttitudeReading r(1.0f, 2.0f, 3.0f, Quaternion::Identity, Matrix::getIdentityProperty(), DateTimeOffset());
    const std::string s = r.ToString();
    EXPECT_NE(s.find("Pitch:1"), std::string::npos);
    EXPECT_NE(s.find("Roll:2"), std::string::npos);
    EXPECT_NE(s.find("Yaw:3"), std::string::npos);
}

TEST(AttitudeReadingTests, GetHashCodeConsistency)
{
    const DateTimeOffset ts(System::DateTime(500LL), System::TimeSpan::Zero);
    const AttitudeReading a(1.0f, 2.0f, 3.0f, Quaternion::Identity, Matrix::getIdentityProperty(), ts);
    const AttitudeReading b(1.0f, 2.0f, 3.0f, Quaternion::Identity, Matrix::getIdentityProperty(), ts);
    EXPECT_EQ(a.GetHashCode(), b.GetHashCode());
}

TEST(AttitudeReadingTests, GetHashCodeDifferentForUnequalInstances)
{
    const DateTimeOffset ts(System::DateTime(500LL), System::TimeSpan::Zero);
    const AttitudeReading a(1.0f, 2.0f, 3.0f, Quaternion::Identity, Matrix::getIdentityProperty(), ts);
    const AttitudeReading b(7.0f, 8.0f, 9.0f, Quaternion::Identity, Matrix::getIdentityProperty(), ts);
    EXPECT_NE(a.GetHashCode(), b.GetHashCode());
}

TEST(AttitudeReadingTests, GetTypeName)
{
    const AttitudeReading r;
    EXPECT_EQ(r.GetTypeName(), "Microsoft.Devices.Sensors.AttitudeReading");
}
