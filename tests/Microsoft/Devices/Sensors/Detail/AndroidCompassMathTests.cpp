// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <cmath>
#include <limits>

#include "Microsoft/Devices/Sensors/Detail/AndroidCompassMath.hpp"
#include "System/DateTimeOffset.hpp"
#include "System/TimeSpan.hpp"

using Microsoft::Devices::Sensors::Detail::AndroidSensorAccuracyStatus;
using Microsoft::Devices::Sensors::Detail::ComputeCompassMaxSampleSkew;
using Microsoft::Devices::Sensors::Detail::ConvertMagneticFieldAccuracyStatusToHeadingAccuracyDegrees;
using Microsoft::Devices::Sensors::Detail::ConvertRotationVectorToMagneticHeadingDegrees;
using Microsoft::Devices::Sensors::Detail::ConvertRotationVectorToMagneticHeadingDegreesWithTiltMode;
using Microsoft::Devices::Sensors::Detail::ConvertRotationVectorToUprightMagneticHeadingDegrees;
using Microsoft::Devices::Sensors::Detail::IsCompassSampleFresh;
using Microsoft::Devices::Sensors::Detail::IsDeviceInUprightCompassMode;
using Microsoft::Devices::Sensors::Detail::NormalizeCompassQuaternion;
using Microsoft::Devices::Sensors::Detail::ShouldRaiseCalibrateForAccuracyStatus;
using System::DateTimeOffset;
using System::TimeSpan;

namespace
{
    constexpr double Tolerance = 1e-3;

    // M_PI is a POSIX/BSD <cmath> extension, not standard C++ -- not
    // guaranteed to be defined on every toolchain/standard-library
    // combination this project targets (Task: replace non-standard M_PI).
    constexpr double Pi = 3.141592653589793238462643383279502884;
}

// Task DEVICES-0090: identity quaternion (no rotation) must produce azimuth
// 0 -- this is the one case simple enough to assert an absolute value for
// without independent hardware verification (any correctly-implemented
// quaternion-to-azimuth formula agrees on the "no rotation" base case).
TEST(AndroidCompassMathTests, IdentityQuaternionProducesZeroHeading)
{
    const double heading = ConvertRotationVectorToMagneticHeadingDegrees(0.0f, 0.0f, 0.0f, 1.0f);
    EXPECT_NEAR(heading, 0.0, Tolerance);
}

// A 90-degree yaw rotation around the sensor's Z axis: quaternion
// (0, 0, sin(45deg), cos(45deg)). Asserts the formula's own self-consistent
// output for this input, not an independently-verified real-world value
// (never checked against real hardware -- see this header's own doc
// comment and docs/devices-hardware-checklist.md).
TEST(AndroidCompassMathTests, NinetyDegreeYawProducesConsistentNonZeroHeading)
{
    const float half = static_cast<float>(Pi / 4.0);
    const double heading = ConvertRotationVectorToMagneticHeadingDegrees(0.0f, 0.0f, std::sin(half), std::cos(half));
    EXPECT_NEAR(heading, 270.0, Tolerance);
}

// Rotating further in the same direction (180 degrees) must land on a
// different, still-consistent heading than the 90-degree case -- confirms
// the formula responds monotonically to yaw, not just correctly at 0.
TEST(AndroidCompassMathTests, OneEightyDegreeYawDiffersFromNinetyDegreeYaw)
{
    const float ninety = static_cast<float>(Pi / 4.0);
    const float oneEighty = static_cast<float>(Pi / 2.0);

    const double headingAtNinety =
        ConvertRotationVectorToMagneticHeadingDegrees(0.0f, 0.0f, std::sin(ninety), std::cos(ninety));
    const double headingAtOneEighty =
        ConvertRotationVectorToMagneticHeadingDegrees(0.0f, 0.0f, std::sin(oneEighty), std::cos(oneEighty));

    EXPECT_NE(headingAtNinety, headingAtOneEighty);
}

// Task COMP2-003 (2026-07-18, external audit `audit_devices_2026-07-17.md`):
// completes flat-mode cardinal-heading coverage -- 0deg/90deg-yaw already had
// exact-value tests above; 180deg-yaw only had a not-equal check
// (OneEightyDegreeYawDiffersFromNinetyDegreeYaw), and 270deg-yaw had no
// coverage at all. Yaw and the resulting heading move in opposite directions
// (heading = (360 - yawDegrees) mod 360, matching the pattern the existing
// 0deg->0deg/90deg->270deg tests already establish) -- numerically verified
// independently before committing, not just hand-derived.
TEST(AndroidCompassMathTests, OneEightyDegreeYawProducesOneEightyDegreeHeading)
{
    const float half = static_cast<float>(Pi / 2.0); // 180deg yaw -> half-angle 90deg
    const double heading = ConvertRotationVectorToMagneticHeadingDegrees(0.0f, 0.0f, std::sin(half), std::cos(half));
    EXPECT_NEAR(heading, 180.0, Tolerance);
}

TEST(AndroidCompassMathTests, TwoSeventyDegreeYawProducesNinetyDegreeHeading)
{
    const float half = static_cast<float>(3.0 * Pi / 4.0); // 270deg yaw -> half-angle 135deg
    const double heading = ConvertRotationVectorToMagneticHeadingDegrees(0.0f, 0.0f, std::sin(half), std::cos(half));
    EXPECT_NEAR(heading, 90.0, Tolerance);
}

TEST(AndroidCompassMathTests, HeadingIsAlwaysInZeroToThreeSixtyRange)
{
    const double heading = ConvertRotationVectorToMagneticHeadingDegrees(0.1f, 0.2f, 0.3f, 0.9f);
    EXPECT_GE(heading, 0.0);
    EXPECT_LT(heading, 360.0);
}

// Task COMPASS-009: hand-derived test quaternions below (documented inline via
// their construction, not just their numeric literals, so a future reader can
// re-derive and re-check them independently of this file's own claims).
//
// q_uprightBase = rotation of -90 degrees about the device's local X axis,
// starting from the "lying flat" identity pose -- i.e. tilting the top edge
// up and back until the screen faces the user vertically, the physical pose
// the WP7 walkthrough's "isCompassUsingNegativeZAxis" condition describes.
// Quaternion for a -90-degree rotation about X: (sin(-45deg), 0, 0, cos(-45deg)).
//
// q_uprightRotated90 = q_uprightBase composed (Hamilton product) with an
// additional +90-degree rotation about the device's own local Y axis --
// physically, spinning the phone in place while still held upright, which is
// the actual "changing compass heading" motion in this pose. Computed by hand
// via the standard Hamilton quaternion product formula (not verified against
// any Android or WP7 API call, only against the formula itself).
//
// q_oppositeTilt = the mirror-image +90-degree rotation about X (top edge
// tilted the *other* way, screen facing down/away instead of toward the
// user) -- deliberately included to confirm IsDeviceInUprightCompassMode()
// only recognizes the one specific tilt direction the WP7 condition
// describes, not "any" vertical tilt, matching that condition's own
// asymmetric Y-sign check.
namespace
{
    constexpr float UprightBaseX = -0.70710678f;
    constexpr float UprightBaseW = 0.70710678f;

    constexpr float UprightRotated90X = -0.5f;
    constexpr float UprightRotated90Y = 0.5f;
    constexpr float UprightRotated90Z = -0.5f;
    constexpr float UprightRotated90W = 0.5f;

    constexpr float OppositeTiltX = 0.70710678f;
    constexpr float OppositeTiltW = 0.70710678f;
} // namespace

TEST(AndroidCompassMathTests, IdentityQuaternionIsNotUprightMode)
{
    EXPECT_FALSE(IsDeviceInUprightCompassMode(0.0f, 0.0f, 0.0f, 1.0f));
}

TEST(AndroidCompassMathTests, UprightBaseQuaternionIsUprightMode)
{
    EXPECT_TRUE(IsDeviceInUprightCompassMode(UprightBaseX, 0.0f, 0.0f, UprightBaseW));
}

TEST(AndroidCompassMathTests, UprightRotated90QuaternionIsStillUprightMode)
{
    // Confirms spinning the phone in place (changing heading) while it stays
    // physically upright does not itself change the flat-vs-upright mode
    // classification -- only the initial tilt-about-X matters.
    EXPECT_TRUE(IsDeviceInUprightCompassMode(
        UprightRotated90X, UprightRotated90Y, UprightRotated90Z, UprightRotated90W));
}

TEST(AndroidCompassMathTests, OppositeTiltQuaternionIsNotUprightMode)
{
    EXPECT_FALSE(IsDeviceInUprightCompassMode(OppositeTiltX, 0.0f, 0.0f, OppositeTiltW));
}

TEST(AndroidCompassMathTests, UprightBaseQuaternionProducesZeroDegreeUprightHeading)
{
    const double heading =
        ConvertRotationVectorToUprightMagneticHeadingDegrees(UprightBaseX, 0.0f, 0.0f, UprightBaseW);
    EXPECT_NEAR(heading, 0.0, Tolerance);
}

TEST(AndroidCompassMathTests, UprightRotated90QuaternionProducesNinetyDegreeUprightHeading)
{
    const double heading = ConvertRotationVectorToUprightMagneticHeadingDegrees(
        UprightRotated90X, UprightRotated90Y, UprightRotated90Z, UprightRotated90W);
    EXPECT_NEAR(heading, 90.0, Tolerance);
}

TEST(AndroidCompassMathTests, UprightHeadingIsAlwaysInZeroToThreeSixtyRange)
{
    const double heading = ConvertRotationVectorToUprightMagneticHeadingDegrees(
        UprightRotated90X, UprightRotated90Y, UprightRotated90Z, UprightRotated90W);
    EXPECT_GE(heading, 0.0);
    EXPECT_LT(heading, 360.0);
}

TEST(AndroidCompassMathTests, WithTiltModeUsesFlatFormulaWhenLyingFlat)
{
    // Identity quaternion -- flat mode -- must match the plain flat-mode
    // function's own result exactly, not the upright one.
    const double combined = ConvertRotationVectorToMagneticHeadingDegreesWithTiltMode(0.0f, 0.0f, 0.0f, 1.0f);
    const double flatOnly = ConvertRotationVectorToMagneticHeadingDegrees(0.0f, 0.0f, 0.0f, 1.0f);
    EXPECT_NEAR(combined, flatOnly, Tolerance);
}

TEST(AndroidCompassMathTests, WithTiltModeUsesUprightFormulaWhenHeldUpright)
{
    const double combined = ConvertRotationVectorToMagneticHeadingDegreesWithTiltMode(
        UprightBaseX, 0.0f, 0.0f, UprightBaseW);
    const double uprightOnly =
        ConvertRotationVectorToUprightMagneticHeadingDegrees(UprightBaseX, 0.0f, 0.0f, UprightBaseW);
    EXPECT_NEAR(combined, uprightOnly, Tolerance);
}

// Task COMP2-002 (2026-07-17, external audit `audit_devices_2026-07-17.md`):
// NormalizeCompassQuaternion() directly.
TEST(AndroidCompassMathTests, NormalizeCompassQuaternionNormalizesNonUnitInput)
{
    float x = 1.0f, y = 2.0f, z = 3.0f, w = 4.0f;
    EXPECT_TRUE(NormalizeCompassQuaternion(x, y, z, w));

    const double lengthSquared =
        (static_cast<double>(x) * x) + (static_cast<double>(y) * y) +
        (static_cast<double>(z) * z) + (static_cast<double>(w) * w);
    EXPECT_NEAR(lengthSquared, 1.0, Tolerance);
    // Direction preserved.
    EXPECT_NEAR(static_cast<double>(y) / x, 2.0, Tolerance);
    EXPECT_NEAR(static_cast<double>(z) / x, 3.0, Tolerance);
    EXPECT_NEAR(static_cast<double>(w) / x, 4.0, Tolerance);
}

TEST(AndroidCompassMathTests, NormalizeCompassQuaternionRejectsNaN)
{
    float x = std::numeric_limits<float>::quiet_NaN(), y = 0.0f, z = 0.0f, w = 1.0f;
    EXPECT_FALSE(NormalizeCompassQuaternion(x, y, z, w));
}

TEST(AndroidCompassMathTests, NormalizeCompassQuaternionRejectsInfinity)
{
    float x = std::numeric_limits<float>::infinity(), y = 0.0f, z = 0.0f, w = 0.0f;
    EXPECT_FALSE(NormalizeCompassQuaternion(x, y, z, w));
}

TEST(AndroidCompassMathTests, NormalizeCompassQuaternionRejectsZeroQuaternion)
{
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f;
    EXPECT_FALSE(NormalizeCompassQuaternion(x, y, z, w));
}

TEST(AndroidCompassMathTests, NormalizeCompassQuaternionRejectsSubnormalNearZero)
{
    float x = std::numeric_limits<float>::denorm_min(), y = 0.0f, z = 0.0f, w = 0.0f;
    EXPECT_FALSE(NormalizeCompassQuaternion(x, y, z, w));
}

// Confirms double-precision intermediate arithmetic genuinely avoids the
// overflow-to-Inf risk AndroidMotionMath.hpp's equivalent float-only
// LengthSquared() has for the same extreme input -- the largest possible
// float component, squared, still fits in double.
TEST(AndroidCompassMathTests, NormalizeCompassQuaternionAcceptsLargestFiniteFloatWithoutOverflow)
{
    float x = std::numeric_limits<float>::max(), y = 0.0f, z = 0.0f, w = 0.0f;
    EXPECT_TRUE(NormalizeCompassQuaternion(x, y, z, w));
    EXPECT_NEAR(x, 1.0f, Tolerance);
}

TEST(AndroidCompassMathTests, WithTiltModeReturnsZeroForNaNInputInsteadOfPropagatingNaN)
{
    const double heading = ConvertRotationVectorToMagneticHeadingDegreesWithTiltMode(
        std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f, 1.0f);
    EXPECT_FALSE(std::isnan(heading));
    EXPECT_NEAR(heading, 0.0, Tolerance);
}

TEST(AndroidCompassMathTests, WithTiltModeReturnsZeroForZeroQuaternionInsteadOfPropagatingNaN)
{
    const double heading = ConvertRotationVectorToMagneticHeadingDegreesWithTiltMode(0.0f, 0.0f, 0.0f, 0.0f);
    EXPECT_FALSE(std::isnan(heading));
    EXPECT_NEAR(heading, 0.0, Tolerance);
}

// Confirms the tilt-mode decision and the heading value it feeds are
// computed from the *same* normalized quaternion, not a raw-vs-normalized
// mismatch -- a non-unit-but-otherwise-valid "upright" input must still
// correctly route to the upright formula and match that formula's own
// result for the equivalent normalized input.
TEST(AndroidCompassMathTests, WithTiltModeNormalizesBeforeChoosingFormula)
{
    constexpr float Scale = 3.0f;
    const double combined = ConvertRotationVectorToMagneticHeadingDegreesWithTiltMode(
        UprightBaseX * Scale, 0.0f, 0.0f, UprightBaseW * Scale);
    const double uprightOnly =
        ConvertRotationVectorToUprightMagneticHeadingDegrees(UprightBaseX, 0.0f, 0.0f, UprightBaseW);
    EXPECT_NEAR(combined, uprightOnly, Tolerance);
}

TEST(AndroidCompassMathTests, HighAccuracyStatusMapsToSmallestDegreeValue)
{
    EXPECT_EQ(ConvertMagneticFieldAccuracyStatusToHeadingAccuracyDegrees(AndroidSensorAccuracyStatus::High), 5.0);
}

TEST(AndroidCompassMathTests, MediumAccuracyStatusMapsToFifteenDegrees)
{
    EXPECT_EQ(ConvertMagneticFieldAccuracyStatusToHeadingAccuracyDegrees(AndroidSensorAccuracyStatus::Medium), 15.0);
}

// Task COMPASS-006 (2026-07-06): Low's value was previously 45 degrees,
// which -- cross-checked against the real Compass.Calibrate event's own
// documented threshold ("If the HeadingAccuracy exceeds +/- 20 degrees,
// this event is raised", archived MSDN hh203107) -- silently contradicted
// ShouldRaiseCalibrateForAccuracyStatus() choosing not to fire Calibrate
// for Low. Changed to exactly 20 degrees, which does not itself "exceed"
// 20, keeping the reported value consistent with the no-fire decision.
TEST(AndroidCompassMathTests, LowAccuracyStatusMapsToTwentyDegrees)
{
    EXPECT_EQ(ConvertMagneticFieldAccuracyStatusToHeadingAccuracyDegrees(AndroidSensorAccuracyStatus::Low), 20.0);
}

TEST(AndroidCompassMathTests, UnreliableAccuracyStatusMapsToOneEightyDegrees)
{
    EXPECT_EQ(ConvertMagneticFieldAccuracyStatusToHeadingAccuracyDegrees(AndroidSensorAccuracyStatus::Unreliable), 180.0);
}

TEST(AndroidCompassMathTests, NoContactAccuracyStatusMapsToOneEightyDegrees)
{
    EXPECT_EQ(ConvertMagneticFieldAccuracyStatusToHeadingAccuracyDegrees(AndroidSensorAccuracyStatus::NoContact), 180.0);
}

TEST(AndroidCompassMathTests, UnreliableAndNoContactRaiseCalibrate)
{
    EXPECT_TRUE(ShouldRaiseCalibrateForAccuracyStatus(AndroidSensorAccuracyStatus::Unreliable));
    EXPECT_TRUE(ShouldRaiseCalibrateForAccuracyStatus(AndroidSensorAccuracyStatus::NoContact));
}

TEST(AndroidCompassMathTests, LowMediumHighDoNotRaiseCalibrate)
{
    EXPECT_FALSE(ShouldRaiseCalibrateForAccuracyStatus(AndroidSensorAccuracyStatus::Low));
    EXPECT_FALSE(ShouldRaiseCalibrateForAccuracyStatus(AndroidSensorAccuracyStatus::Medium));
    EXPECT_FALSE(ShouldRaiseCalibrateForAccuracyStatus(AndroidSensorAccuracyStatus::High));
}

// Task COMPASS-006: for every accuracy status, ShouldRaiseCalibrateForAccuracyStatus()
// must agree with the real Compass.Calibrate contract ("fires iff HeadingAccuracy
// exceeds +/- 20 degrees", MSDN hh203107) applied to that same status's own
// ConvertMagneticFieldAccuracyStatusToHeadingAccuracyDegrees() value -- these are two
// independently-implemented functions and nothing at the type level keeps them in
// sync; this test is the cross-check that previously would have caught the
// Low-status 45-degree/no-fire contradiction fixed by this same task.
TEST(AndroidCompassMathTests, CalibrateDecisionIsConsistentWithHeadingAccuracyThreshold)
{
    constexpr AndroidSensorAccuracyStatus AllStatuses[] = {
        AndroidSensorAccuracyStatus::NoContact,
        AndroidSensorAccuracyStatus::Unreliable,
        AndroidSensorAccuracyStatus::Low,
        AndroidSensorAccuracyStatus::Medium,
        AndroidSensorAccuracyStatus::High,
    };

    for (const AndroidSensorAccuracyStatus status : AllStatuses)
    {
        const double headingAccuracy = ConvertMagneticFieldAccuracyStatusToHeadingAccuracyDegrees(status);
        const bool exceedsDocumentedThreshold = headingAccuracy > 20.0;
        EXPECT_EQ(ShouldRaiseCalibrateForAccuracyStatus(status), exceedsDocumentedThreshold)
            << "status " << static_cast<int>(status) << " reports HeadingAccuracy="
            << headingAccuracy << " but its Calibrate-firing decision doesn't match "
            << "the documented \"exceeds 20 degrees\" rule";
    }
}

// Task COMP2-001 (2026-07-17, external audit `audit_devices_2026-07-17.md`):
// ComputeCompassMaxSampleSkew()/IsCompassSampleFresh() together back
// AndroidCompassBackend's "don't fuse indefinitely stale data" fix. Both are
// pure functions (no Android/sensor dependency at all), so -- unlike
// AndroidCompassBackend.cpp itself, entirely #ifdef __ANDROID__-gated -- they
// are fully host-testable, matching this file's own established pattern for
// every other pure Compass math helper.

TEST(AndroidCompassMathTests, ComputeCompassMaxSampleSkewFlooredForSmallInterval)
{
    // 10ms * 5 == 50ms, well below the 500ms floor.
    EXPECT_EQ(ComputeCompassMaxSampleSkew(TimeSpan::FromMilliseconds(10.0)), TimeSpan::FromMilliseconds(500.0));
}

TEST(AndroidCompassMathTests, ComputeCompassMaxSampleSkewFlooredForZeroInterval)
{
    EXPECT_EQ(ComputeCompassMaxSampleSkew(TimeSpan::Zero), TimeSpan::FromMilliseconds(500.0));
}

TEST(AndroidCompassMathTests, ComputeCompassMaxSampleSkewScalesToFiveTimesLargeInterval)
{
    // 200ms * 5 == 1000ms, above the 500ms floor -- the scaled value wins.
    EXPECT_EQ(ComputeCompassMaxSampleSkew(TimeSpan::FromMilliseconds(200.0)), TimeSpan::FromMilliseconds(1000.0));
}

TEST(AndroidCompassMathTests, IsCompassSampleFreshTrueForExactlySimultaneousTimestamp)
{
    const DateTimeOffset now = DateTimeOffset::getUtcNowProperty();
    EXPECT_TRUE(IsCompassSampleFresh(now, now, TimeSpan::FromMilliseconds(500.0)));
}

TEST(AndroidCompassMathTests, IsCompassSampleFreshTrueForFutureTimestamp)
{
    // A sample timestamp "after now" (clock-read ordering edge case, not a
    // real future sample) is treated as fresh rather than rejected -- see
    // IsCompassSampleFresh()'s own doc comment.
    const DateTimeOffset now = DateTimeOffset::getUtcNowProperty();
    const DateTimeOffset sampleInFuture = now.AddMilliseconds(50.0);
    EXPECT_TRUE(IsCompassSampleFresh(sampleInFuture, now, TimeSpan::FromMilliseconds(500.0)));
}

TEST(AndroidCompassMathTests, IsCompassSampleFreshTrueWellWithinMaxSkew)
{
    const DateTimeOffset now = DateTimeOffset::getUtcNowProperty();
    const DateTimeOffset sample = now - TimeSpan::FromMilliseconds(100.0);
    EXPECT_TRUE(IsCompassSampleFresh(sample, now, TimeSpan::FromMilliseconds(500.0)));
}

TEST(AndroidCompassMathTests, IsCompassSampleFreshTrueExactlyAtMaxSkewBoundary)
{
    // The boundary itself ("<=") is still fresh, not stale.
    const DateTimeOffset now = DateTimeOffset::getUtcNowProperty();
    const DateTimeOffset sample = now - TimeSpan::FromMilliseconds(500.0);
    EXPECT_TRUE(IsCompassSampleFresh(sample, now, TimeSpan::FromMilliseconds(500.0)));
}

TEST(AndroidCompassMathTests, IsCompassSampleFreshFalseBeyondMaxSkew)
{
    const DateTimeOffset now = DateTimeOffset::getUtcNowProperty();
    const DateTimeOffset sample = now - TimeSpan::FromMilliseconds(501.0);
    EXPECT_FALSE(IsCompassSampleFresh(sample, now, TimeSpan::FromMilliseconds(500.0)));
}

TEST(AndroidCompassMathTests, IsCompassSampleFreshFalseForLongStaleSample)
{
    // The scenario IsCompassSampleFresh() exists for: a stream that stopped
    // delivering minutes ago must not still be considered fresh.
    const DateTimeOffset now = DateTimeOffset::getUtcNowProperty();
    const DateTimeOffset sample = now - TimeSpan::FromMinutes(5.0);
    EXPECT_FALSE(IsCompassSampleFresh(sample, now, TimeSpan::FromMilliseconds(500.0)));
}

// Task COMP2-003 (2026-07-18, external audit `audit_devices_2026-07-17.md`):
// independently re-derives Android's own documented quaternion -> rotation
// matrix -> azimuth chain from scratch, reading only Android's public
// SensorManager documentation (not this project's own production formulas,
// which this test deliberately never calls into for its own matrix
// construction), and cross-checks the result against
// ConvertRotationVectorToMagneticHeadingDegrees()/
// ConvertRotationVectorToUprightMagneticHeadingDegrees() -- the "compare the
// derivation with Android reference matrix/orientation APIs" requirement,
// satisfied as an ongoing, regression-proof automated check rather than a
// one-time manual claim in a comment.
namespace
{
    // Independent reconstruction of SensorManager.getRotationMatrixFromVector()'s
    // documented algorithm: the standard Hamilton quaternion-to-rotation-matrix
    // identity, row-major (R[3*row+col]), matching Android's own float[9] layout.
    // Deliberately writes out all nine elements even though the azimuth formulas
    // below only read two of them -- this is meant to look like an independent
    // re-implementation of Android's actual public algorithm, not a shortcut.
    struct IndependentRotationMatrix
    {
        double r[9];
    };

    IndependentRotationMatrix BuildIndependentRotationMatrix(double x, double y, double z, double w)
    {
        IndependentRotationMatrix m{};
        m.r[0] = 1.0 - 2.0 * (y * y + z * z);
        m.r[1] = 2.0 * (x * y - z * w);
        m.r[2] = 2.0 * (x * z + y * w);
        m.r[3] = 2.0 * (x * y + z * w);
        m.r[4] = 1.0 - 2.0 * (x * x + z * z);
        m.r[5] = 2.0 * (y * z - x * w);
        m.r[6] = 2.0 * (x * z - y * w);
        m.r[7] = 2.0 * (y * z + x * w);
        m.r[8] = 1.0 - 2.0 * (x * x + y * y);
        return m;
    }

    // SensorManager.getOrientation()'s own documented azimuth formula: atan2(R[1], R[4]).
    double IndependentAzimuthDegrees(const IndependentRotationMatrix& m)
    {
        const double azimuthRadians = std::atan2(m.r[1], m.r[4]);
        double degrees = azimuthRadians * (180.0 / Pi);
        degrees = std::fmod(degrees + 360.0, 360.0);
        return degrees;
    }

    // SensorManager.remapCoordinateSystem(inR, AXIS_X, AXIS_Z, outR)'s documented
    // effect: new Y axis = old Z axis (the third axis, new Z, follows from the
    // right-hand rule and is never read by the azimuth formula, so it is not
    // computed here). Reading azimuth off the remapped matrix via the same
    // atan2(R[1], R[4]) formula is therefore equivalent to reading atan2 of the
    // *old* matrix's column-2 entries (R[2]/R[5]) directly.
    double IndependentUprightAzimuthDegrees(const IndependentRotationMatrix& m)
    {
        const double azimuthRadians = std::atan2(m.r[2], m.r[5]);
        double degrees = azimuthRadians * (180.0 / Pi);
        degrees = std::fmod(degrees + 360.0, 360.0);
        return degrees;
    }

    struct CrossCheckQuaternion
    {
        const char* description;
        float x;
        float y;
        float z;
        float w;
    };

    // A representative set, not just yaw-only rotations (which the production
    // formula's own two-term shortcut could trivially satisfy without the
    // remaining seven matrix elements this independent reconstruction also
    // computes): identity, all four cardinal yaw rotations, and two combined
    // pitch+yaw+roll poses.
    const CrossCheckQuaternion kCrossCheckQuaternions[] = {
        {"identity", 0.0f, 0.0f, 0.0f, 1.0f},
        {"90deg yaw", 0.0f, 0.0f, 0.70710678f, 0.70710678f},
        {"180deg yaw", 0.0f, 0.0f, 1.0f, 0.0f},
        {"270deg yaw", 0.0f, 0.0f, -0.70710678f, 0.70710678f},
        {"combined pitch+yaw+roll A", 0.1f, 0.2f, 0.3f, 0.9f},
        {"combined pitch+yaw+roll B", -0.2f, 0.4f, -0.1f, 0.88881944f},
    };
} // namespace

TEST(IndependentReferenceCrossCheckTests, FlatHeadingMatchesIndependentlyReconstructedAndroidFormula)
{
    for (const CrossCheckQuaternion& q : kCrossCheckQuaternions)
    {
        SCOPED_TRACE(q.description);
        const IndependentRotationMatrix m = BuildIndependentRotationMatrix(q.x, q.y, q.z, q.w);
        const double independent = IndependentAzimuthDegrees(m);
        const double production = ConvertRotationVectorToMagneticHeadingDegrees(q.x, q.y, q.z, q.w);
        EXPECT_NEAR(independent, production, Tolerance);
    }
}

TEST(IndependentReferenceCrossCheckTests, UprightHeadingMatchesIndependentlyReconstructedAndroidFormula)
{
    for (const CrossCheckQuaternion& q : kCrossCheckQuaternions)
    {
        SCOPED_TRACE(q.description);
        const IndependentRotationMatrix m = BuildIndependentRotationMatrix(q.x, q.y, q.z, q.w);
        const double independent = IndependentUprightAzimuthDegrees(m);
        const double production = ConvertRotationVectorToUprightMagneticHeadingDegrees(q.x, q.y, q.z, q.w);
        EXPECT_NEAR(independent, production, Tolerance);
    }
}

// Task COMP2-003: fills the remaining cardinal-heading gaps in upright mode --
// only 0deg/90deg had exact-value coverage before this task (UprightBaseQuaternionProducesZeroDegreeUprightHeading/
// UprightRotated90QuaternionProducesNinetyDegreeUprightHeading above); 180deg/270deg
// did not. q_uprightBase composed (Hamilton product) with a further +180deg/+270deg
// rotation about the device's own local Y axis, matching q_uprightRotated90's own
// documented construction above (same axis, different angle).
TEST(AndroidCompassMathTests, UprightRotated180QuaternionProducesOneEightyDegreeUprightHeading)
{
    // q_uprightBase (-90deg about X) composed (Hamilton product) with a further
    // +180deg rotation about the device's own local Y axis:
    // (sin(-45deg),0,0,cos(-45deg)) * (0,sin(90deg),0,cos(90deg))
    // = (-0.70710678,0,0,0.70710678) * (0,1,0,0)
    // = (0, 0.70710678, -0.70710678, 0)
    // (numerically verified independently, not just hand-derived -- an earlier
    // draft of this test had an arithmetic error in this exact derivation,
    // caught by cross-checking with an independent script before committing).
    constexpr float X = 0.0f;
    constexpr float Y = 0.70710678f;
    constexpr float Z = -0.70710678f;
    constexpr float W = 0.0f;

    const double heading = ConvertRotationVectorToUprightMagneticHeadingDegrees(X, Y, Z, W);
    EXPECT_NEAR(heading, 180.0, Tolerance);
}

TEST(AndroidCompassMathTests, UprightRotated270QuaternionProducesTwoSeventyDegreeUprightHeading)
{
    // q_uprightBase (-90deg about X) composed (Hamilton product) with a further
    // +270deg rotation about the device's own local Y axis:
    // (sin(-45deg),0,0,cos(-45deg)) * (0,sin(135deg),0,cos(135deg))
    // = (-0.70710678,0,0,0.70710678) * (0,0.70710678,0,-0.70710678)
    // = (0.5, 0.5, -0.5, -0.5)
    // (numerically verified independently -- see UprightRotated180's own note above).
    constexpr float X = 0.5f;
    constexpr float Y = 0.5f;
    constexpr float Z = -0.5f;
    constexpr float W = -0.5f;

    const double heading = ConvertRotationVectorToUprightMagneticHeadingDegrees(X, Y, Z, W);
    EXPECT_NEAR(heading, 270.0, Tolerance);
}
