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

TEST(AttitudeReadingTests, SetPitch)
{
    AttitudeReading r;
    r.setPitchProperty(0.5f);
    EXPECT_EQ(r.getPitchProperty(), 0.5f);
}

TEST(AttitudeReadingTests, SetRoll)
{
    AttitudeReading r;
    r.setRollProperty(-0.5f);
    EXPECT_EQ(r.getRollProperty(), -0.5f);
}

TEST(AttitudeReadingTests, SetYaw)
{
    AttitudeReading r;
    r.setYawProperty(1.5f);
    EXPECT_EQ(r.getYawProperty(), 1.5f);
}

TEST(AttitudeReadingTests, SetQuaternion)
{
    AttitudeReading r;
    const Quaternion q(1.0f, 0.0f, 0.0f, 0.0f);
    r.setQuaternionProperty(q);
    EXPECT_TRUE(r.getQuaternionProperty() == q);
}

TEST(AttitudeReadingTests, SetRotationMatrix)
{
    AttitudeReading r;
    const Matrix m(
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        1.0f, 2.0f, 3.0f, 1.0f);
    r.setRotationMatrixProperty(m);
    EXPECT_TRUE(r.getRotationMatrixProperty() == m);
}

TEST(AttitudeReadingTests, SetTimestamp)
{
    AttitudeReading r;
    const DateTimeOffset ts(System::DateTime(9999LL), System::TimeSpan::Zero);
    r.setTimestampProperty(ts);
    EXPECT_EQ(r.getTimestampProperty(), ts);
}

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

TEST(AttitudeReadingTests, GetTypeName)
{
    const AttitudeReading r;
    EXPECT_EQ(r.GetTypeName(), "Microsoft.Devices.Sensors.AttitudeReading");
}
