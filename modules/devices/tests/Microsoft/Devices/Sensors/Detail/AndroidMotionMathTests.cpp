// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <cmath>
#include <limits>

#include "Microsoft/Devices/Sensors/Detail/AndroidMotionMath.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

using Microsoft::Devices::Sensors::Detail::ConvertRotationVectorToXnaQuaternion;
using Microsoft::Devices::Sensors::Detail::ExtractYawPitchRollFromQuaternion;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Quaternion;
using Microsoft::Xna::Framework::Vector3;

namespace
{
    constexpr float Tolerance = 1e-3f;
}

// Task MOT2-002 (2026-07-17, external audit `audit_devices_2026-07-17.md`):
// ConvertRotationVectorToXnaQuaternion() now normalizes its input (see its
// own doc comment) -- {0.1, 0.2, 0.3, 0.9} is not unit-length
// (LengthSquared() == 0.95), so it is no longer returned unchanged. Updated
// to an already-unit-length input, for which normalization is numerically a
// no-op, preserving this test's original intent for that specific case; a
// new, separate test below locks in the actual normalization behavior for
// non-unit input.
TEST(AndroidMotionMathTests, ConvertRotationVectorToXnaQuaternionIsComponentwiseForAlreadyUnitInput)
{
    const Quaternion q = ConvertRotationVectorToXnaQuaternion(0.0f, 0.0f, 0.0f, 1.0f);
    EXPECT_NEAR(q.X, 0.0f, Tolerance);
    EXPECT_NEAR(q.Y, 0.0f, Tolerance);
    EXPECT_NEAR(q.Z, 0.0f, Tolerance);
    EXPECT_NEAR(q.W, 1.0f, Tolerance);
}

TEST(AndroidMotionMathTests, ConvertRotationVectorToXnaQuaternionNormalizesNonUnitInput)
{
    const Quaternion q = ConvertRotationVectorToXnaQuaternion(0.1f, 0.2f, 0.3f, 0.9f);
    EXPECT_NEAR(q.LengthSquared(), 1.0f, Tolerance);
    // Direction is preserved -- only magnitude changes.
    EXPECT_NEAR(q.X / q.W, 0.1f / 0.9f, Tolerance);
    EXPECT_NEAR(q.Y / q.W, 0.2f / 0.9f, Tolerance);
    EXPECT_NEAR(q.Z / q.W, 0.3f / 0.9f, Tolerance);
}

TEST(AndroidMotionMathTests, ConvertRotationVectorToXnaQuaternionWithNaNFallsBackToIdentity)
{
    const Quaternion q = ConvertRotationVectorToXnaQuaternion(
        std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f, 1.0f);
    EXPECT_EQ(q.X, Quaternion::Identity.X);
    EXPECT_EQ(q.Y, Quaternion::Identity.Y);
    EXPECT_EQ(q.Z, Quaternion::Identity.Z);
    EXPECT_EQ(q.W, Quaternion::Identity.W);
}

TEST(AndroidMotionMathTests, ConvertRotationVectorToXnaQuaternionWithInfinityFallsBackToIdentity)
{
    const Quaternion q = ConvertRotationVectorToXnaQuaternion(
        std::numeric_limits<float>::infinity(), 0.0f, 0.0f, 0.0f);
    EXPECT_EQ(q.X, Quaternion::Identity.X);
    EXPECT_EQ(q.W, Quaternion::Identity.W);
}

TEST(AndroidMotionMathTests, ConvertRotationVectorToXnaQuaternionWithZeroQuaternionFallsBackToIdentity)
{
    const Quaternion q = ConvertRotationVectorToXnaQuaternion(0.0f, 0.0f, 0.0f, 0.0f);
    EXPECT_EQ(q.X, Quaternion::Identity.X);
    EXPECT_EQ(q.Y, Quaternion::Identity.Y);
    EXPECT_EQ(q.Z, Quaternion::Identity.Z);
    EXPECT_EQ(q.W, Quaternion::Identity.W);
}

// x*x alone overflows a float (max ~3.4e38) -- LengthSquared() itself
// becomes +Infinity, caught by the same !isfinite() check as an explicit
// NaN/Inf component.
TEST(AndroidMotionMathTests, ConvertRotationVectorToXnaQuaternionWithHugeOverflowingValueFallsBackToIdentity)
{
    const Quaternion q = ConvertRotationVectorToXnaQuaternion(1e30f, 0.0f, 0.0f, 0.0f);
    EXPECT_EQ(q.X, Quaternion::Identity.X);
    EXPECT_EQ(q.W, Quaternion::Identity.W);
}

TEST(AndroidMotionMathTests, ConvertRotationVectorToXnaQuaternionWithSubnormalNearZeroFallsBackToIdentity)
{
    const Quaternion q = ConvertRotationVectorToXnaQuaternion(
        std::numeric_limits<float>::denorm_min(), 0.0f, 0.0f, 0.0f);
    EXPECT_EQ(q.X, Quaternion::Identity.X);
    EXPECT_EQ(q.W, Quaternion::Identity.W);
}

// ExtractYawPitchRollFromQuaternion() is a general-purpose pure function,
// not exclusively reached through ConvertRotationVectorToXnaQuaternion() --
// exercises its own independent validation directly.
TEST(AndroidMotionMathTests, ExtractYawPitchRollFromQuaternionWithNaNProducesZeroNotNaN)
{
    float yaw = 1.0f, pitch = 1.0f, roll = 1.0f;
    ExtractYawPitchRollFromQuaternion(
        Quaternion(std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f, 1.0f), yaw, pitch, roll);
    EXPECT_FALSE(std::isnan(yaw));
    EXPECT_FALSE(std::isnan(pitch));
    EXPECT_FALSE(std::isnan(roll));
    EXPECT_NEAR(yaw, 0.0f, Tolerance);
    EXPECT_NEAR(pitch, 0.0f, Tolerance);
    EXPECT_NEAR(roll, 0.0f, Tolerance);
}

TEST(AndroidMotionMathTests, ExtractYawPitchRollFromQuaternionWithZeroQuaternionProducesZeroNotNaN)
{
    float yaw = 1.0f, pitch = 1.0f, roll = 1.0f;
    ExtractYawPitchRollFromQuaternion(Quaternion(0.0f, 0.0f, 0.0f, 0.0f), yaw, pitch, roll);
    EXPECT_FALSE(std::isnan(yaw));
    EXPECT_FALSE(std::isnan(pitch));
    EXPECT_FALSE(std::isnan(roll));
}

// A quaternion representing exactly +/-90 degrees pitch is the gimbal-lock
// pole this fix's asin() clamp specifically targets -- floating-point
// rounding in Matrix::CreateFromQuaternion() can push -M32 fractionally
// outside [-1, 1] at exactly this angle, and an unclamped asin() of that
// returns NaN instead of +/-pi/2.
TEST(AndroidMotionMathTests, ExtractYawPitchRollFromQuaternionAtGimbalLockPoleDoesNotProduceNaN)
{
    constexpr float Pi = 3.141592653589793238462643383279502884f;

    for (const float pitchRadians : {Pi / 2.0f, -Pi / 2.0f})
    {
        const Quaternion q = Quaternion::CreateFromYawPitchRoll(0.0f, pitchRadians, 0.0f);

        float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
        ExtractYawPitchRollFromQuaternion(q, yaw, pitch, roll);

        ASSERT_FALSE(std::isnan(pitch)) << "pitchRadians=" << pitchRadians;
        EXPECT_NEAR(pitch, pitchRadians, Tolerance) << "pitchRadians=" << pitchRadians;
    }
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

// Task MOTION-002: the CaseA/B/C round-trips above already cover the general
// algebraic correctness of the extraction formula (arbitrary combined yaw+pitch+roll,
// a stronger property than isolated cardinal angles), but plans/plan_devices.md's own
// acceptance criteria names 90/180/270-degree yaw specifically -- added here for
// direct traceability, not because the general round-trip tests left a real gap.
TEST(AndroidMotionMathTests, RoundTripsAtNinetyOneEightyTwoSeventyDegreesYaw)
{
    constexpr float Pi = 3.141592653589793238462643383279502884f;
    constexpr float DegreesToTest[] = {90.0f, 180.0f, 270.0f};

    for (const float degrees : DegreesToTest)
    {
        const float yawRadians = degrees * Pi / 180.0f;
        const Quaternion q = Quaternion::CreateFromYawPitchRoll(yawRadians, 0.0f, 0.0f);

        float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
        ExtractYawPitchRollFromQuaternion(q, yaw, pitch, roll);

        // atan2's range is (-pi, pi], so 180/270-degree inputs legitimately
        // wrap to their equivalent angle in that range (270 -> -90) --
        // compare via the angle's own sine/cosine rather than the raw
        // radian value to avoid a false failure on the wrap.
        EXPECT_NEAR(std::sin(yaw), std::sin(yawRadians), Tolerance) << "degrees=" << degrees;
        EXPECT_NEAR(std::cos(yaw), std::cos(yawRadians), Tolerance) << "degrees=" << degrees;
        EXPECT_NEAR(pitch, 0.0f, Tolerance) << "degrees=" << degrees;
        EXPECT_NEAR(roll, 0.0f, Tolerance) << "degrees=" << degrees;
    }
}

// Task MOT2-001 (2026-07-18, external audit `audit_devices_2026-07-17.md`):
// independently verifies the handedness/rotation-sense claim
// ConvertRotationVectorToXnaQuaternion()'s own doc comment now makes --
// deliberately does NOT go through Quaternion::CreateFromYawPitchRoll() (the
// existing RoundTripsAtNinetyOneEightyTwoSeventyDegreesYaw tests above already
// cover that path) -- instead builds a raw quaternion directly via the same
// Hamilton half-angle formula Android's own TYPE_ROTATION_VECTOR sensor and
// Quaternion::CreateFromAxisAngle() both use (x=0,y=0,z=sin(45deg),w=cos(45deg)
// for a +90deg rotation about Z), the closest test-level equivalent to what a
// real Android sample would actually deliver, then transforms a world unit
// vector through Matrix::CreateFromQuaternion() using XNA's own row-vector
// convention (Vector3::Transform(vector, matrix)). A +90deg right-handed
// rotation about Z must send +X to +Y -- confirms Android and XNA quaternions
// are the same mathematical object under the same convention, with no
// handedness correction needed, as an executable check rather than only a
// doc-comment claim.
TEST(AndroidMotionMathTests, DirectQuaternionPlusNinetyDegreeYawRotatesUnitXToUnitYMatchingRightHandedConvention)
{
    constexpr float Pi = 3.14159265358979323846f;
    const float half = Pi / 4.0f; // 90deg rotation -> half-angle 45deg, the Hamilton formula's own convention.

    const Quaternion q = ConvertRotationVectorToXnaQuaternion(0.0f, 0.0f, std::sin(half), std::cos(half));
    const Matrix m = Matrix::CreateFromQuaternion(q);
    const Vector3 rotated = Vector3::Transform(Vector3::UnitX, m);

    EXPECT_NEAR(rotated.X, Vector3::UnitY.X, Tolerance);
    EXPECT_NEAR(rotated.Y, Vector3::UnitY.Y, Tolerance);
    EXPECT_NEAR(rotated.Z, Vector3::UnitY.Z, Tolerance);
}
