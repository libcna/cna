// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "Microsoft/Devices/Sensors/Detail/AndroidMotionMath.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"

using Microsoft::Devices::Sensors::Detail::ConvertRotationVectorToXnaQuaternion;
using Microsoft::Devices::Sensors::Detail::ExtractYawPitchRollFromQuaternion;
using Microsoft::Xna::Framework::Quaternion;

namespace
{
    constexpr float Tolerance = 1e-3f;
}

TEST(AndroidMotionMathTests, ConvertRotationVectorToXnaQuaternionIsComponentwise)
{
    const Quaternion q = ConvertRotationVectorToXnaQuaternion(0.1f, 0.2f, 0.3f, 0.9f);
    EXPECT_FLOAT_EQ(q.X, 0.1f);
    EXPECT_FLOAT_EQ(q.Y, 0.2f);
    EXPECT_FLOAT_EQ(q.Z, 0.3f);
    EXPECT_FLOAT_EQ(q.W, 0.9f);
}

// Task DEVICES-0107: round-trips ExtractYawPitchRollFromQuaternion() through
// CNA's own already-tested Quaternion::CreateFromYawPitchRoll() for several
// angle combinations -- this is how the formula was derived and verified
// (numerically, before being written into AndroidMotionMath.hpp), not an
// independent guess at XNA's Euler convention.

TEST(AndroidMotionMathTests, RoundTripsThroughCreateFromYawPitchRoll_CaseA)
{
    const Quaternion q = Quaternion::CreateFromYawPitchRoll(0.3f, 0.2f, 0.1f);
    float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
    ExtractYawPitchRollFromQuaternion(q, yaw, pitch, roll);
    EXPECT_NEAR(yaw, 0.3f, Tolerance);
    EXPECT_NEAR(pitch, 0.2f, Tolerance);
    EXPECT_NEAR(roll, 0.1f, Tolerance);
}

TEST(AndroidMotionMathTests, RoundTripsThroughCreateFromYawPitchRoll_CaseB)
{
    const Quaternion q = Quaternion::CreateFromYawPitchRoll(0.5f, -0.4f, 0.6f);
    float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
    ExtractYawPitchRollFromQuaternion(q, yaw, pitch, roll);
    EXPECT_NEAR(yaw, 0.5f, Tolerance);
    EXPECT_NEAR(pitch, -0.4f, Tolerance);
    EXPECT_NEAR(roll, 0.6f, Tolerance);
}

TEST(AndroidMotionMathTests, RoundTripsThroughCreateFromYawPitchRoll_CaseC)
{
    const Quaternion q = Quaternion::CreateFromYawPitchRoll(1.0f, 0.3f, -0.2f);
    float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
    ExtractYawPitchRollFromQuaternion(q, yaw, pitch, roll);
    EXPECT_NEAR(yaw, 1.0f, Tolerance);
    EXPECT_NEAR(pitch, 0.3f, Tolerance);
    EXPECT_NEAR(roll, -0.2f, Tolerance);
}

TEST(AndroidMotionMathTests, IdentityQuaternionProducesZeroYawPitchRoll)
{
    float yaw = 1.0f, pitch = 1.0f, roll = 1.0f;
    ExtractYawPitchRollFromQuaternion(Quaternion::Identity, yaw, pitch, roll);
    EXPECT_NEAR(yaw, 0.0f, Tolerance);
    EXPECT_NEAR(pitch, 0.0f, Tolerance);
    EXPECT_NEAR(roll, 0.0f, Tolerance);
}
