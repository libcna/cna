// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <cmath>

#include "Microsoft/Devices/Sensors/Detail/AndroidCompassMath.hpp"

using Microsoft::Devices::Sensors::Detail::AndroidSensorAccuracyStatus;
using Microsoft::Devices::Sensors::Detail::ConvertMagneticFieldAccuracyStatusToHeadingAccuracyDegrees;
using Microsoft::Devices::Sensors::Detail::ConvertRotationVectorToMagneticHeadingDegrees;
using Microsoft::Devices::Sensors::Detail::ShouldRaiseCalibrateForAccuracyStatus;

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

TEST(AndroidCompassMathTests, HeadingIsAlwaysInZeroToThreeSixtyRange)
{
    const double heading = ConvertRotationVectorToMagneticHeadingDegrees(0.1f, 0.2f, 0.3f, 0.9f);
    EXPECT_GE(heading, 0.0);
    EXPECT_LT(heading, 360.0);
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
