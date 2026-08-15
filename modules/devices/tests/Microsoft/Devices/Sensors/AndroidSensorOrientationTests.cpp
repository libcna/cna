// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "Microsoft/Devices/Sensors/Detail/AndroidSensorOrientation.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

using Microsoft::Devices::Sensors::Detail::AndroidSensorLandscapeOrientation;
using Microsoft::Devices::Sensors::Detail::ConvertAndroidPortraitToXnaLandscape;
using Microsoft::Devices::Sensors::Detail::IsAndroidLandscapeRemapEnabled;
using Microsoft::Devices::Sensors::Detail::SetAndroidLandscapeRemapEnabled;
using Microsoft::Xna::Framework::Vector3;

namespace
{
    // Task ACCEL-008: SetAndroidLandscapeRemapEnabled() is process-wide state, so
    // every test that touches it must restore the default (true) afterward, or a
    // change here would leak into unrelated tests run later in the same process --
    // mirrors this codebase's established ScopedFake*Backend RAII-restore pattern
    // (see e.g. FileDialogTests.cpp) applied to a plain flag instead of a backend.
    class ScopedAndroidLandscapeRemapSetting
    {
    public:
        explicit ScopedAndroidLandscapeRemapSetting(bool enabled)
        {
            SetAndroidLandscapeRemapEnabled(enabled);
        }

        ~ScopedAndroidLandscapeRemapSetting()
        {
            SetAndroidLandscapeRemapEnabled(true);
        }

        ScopedAndroidLandscapeRemapSetting(const ScopedAndroidLandscapeRemapSetting&) = delete;
        ScopedAndroidLandscapeRemapSetting& operator=(const ScopedAndroidLandscapeRemapSetting&) = delete;
    };
} // namespace

// Task P5-7: Accelerometer.cpp's/Gyroscope.cpp's #ifdef __ANDROID__ axis-remap
// code was previously untestable off-Android — it called
// the native display-orientation query directly, and the whole function was
// compiled out entirely on non-Android builds (confirmed compiling under
// the real Android NDK for the first time in Task P4-11, but its actual
// sign/axis correctness was still only reasoned about from documentation,
// never tested). The sign-remap math itself is now
// Detail::ConvertAndroidPortraitToXnaLandscape() — a pure function taking
// an explicit AndroidSensorLandscapeOrientation instead of querying SDL,
// buildable and testable on any platform, including this headless Linux
// dev container.
//
// The four tests below cover accelerometer-shaped and gyroscope-shaped
// inputs against both allowed rotations, per the task's own list — note
// this is genuinely the same shared function for both (the sign remap
// doesn't depend on what physical quantity the raw values represent), so
// the accelerometer/gyroscope split here is about covering the units each
// class actually passes through it (g-normalized floats vs. radians/second),
// not about testing two different code paths.
//
// Docs/devices-hardware-checklist.md still separately requires physically
// verifying these signs against real device tilt — this only proves the
// documented convention is what the code actually implements, not that the
// convention itself is correct on a real device.

TEST(AndroidSensorOrientationTests, AccelerometerRotation90NegatesY)
{
    // ROTATION_90 (landscape): xnaX = rawX, xnaY = -rawY, xnaZ = rawZ.
    const float rawX = 0.25f;
    const float rawY = -0.75f;
    const float rawZ = 1.0f;

    const Vector3 result = ConvertAndroidPortraitToXnaLandscape(
        rawX, rawY, rawZ, AndroidSensorLandscapeOrientation::Rotation90);

    EXPECT_FLOAT_EQ(result.X, rawX);
    EXPECT_FLOAT_EQ(result.Y, -rawY);
    EXPECT_FLOAT_EQ(result.Z, rawZ);
}

TEST(AndroidSensorOrientationTests, AccelerometerRotation270NegatesX)
{
    // ROTATION_270 (flipped landscape): xnaX = -rawX, xnaY = rawY, xnaZ = rawZ.
    const float rawX = 0.25f;
    const float rawY = -0.75f;
    const float rawZ = 1.0f;

    const Vector3 result = ConvertAndroidPortraitToXnaLandscape(
        rawX, rawY, rawZ, AndroidSensorLandscapeOrientation::Rotation270);

    EXPECT_FLOAT_EQ(result.X, -rawX);
    EXPECT_FLOAT_EQ(result.Y, rawY);
    EXPECT_FLOAT_EQ(result.Z, rawZ);
}

TEST(AndroidSensorOrientationTests, GyroscopeRotation90NegatesY)
{
    // Same convention, gyroscope-shaped (radians/second) magnitudes.
    const float rawX = 0.5f;
    const float rawY = -1.25f;
    const float rawZ = 2.0f;

    const Vector3 result = ConvertAndroidPortraitToXnaLandscape(
        rawX, rawY, rawZ, AndroidSensorLandscapeOrientation::Rotation90);

    EXPECT_FLOAT_EQ(result.X, rawX);
    EXPECT_FLOAT_EQ(result.Y, -rawY);
    EXPECT_FLOAT_EQ(result.Z, rawZ);
}

TEST(AndroidSensorOrientationTests, GyroscopeRotation270NegatesX)
{
    const float rawX = 0.5f;
    const float rawY = -1.25f;
    const float rawZ = 2.0f;

    const Vector3 result = ConvertAndroidPortraitToXnaLandscape(
        rawX, rawY, rawZ, AndroidSensorLandscapeOrientation::Rotation270);

    EXPECT_FLOAT_EQ(result.X, -rawX);
    EXPECT_FLOAT_EQ(result.Y, rawY);
    EXPECT_FLOAT_EQ(result.Z, rawZ);
}

// Confirms the "right tilt → xnaY > 0" WP7 convention documented in both
// Accelerometer.cpp/Gyroscope.cpp actually holds for both rotations, not
// just the raw sign-flip mechanics above: a negative rawY (as the device
// coordinate system produces when physically tilted right, per each
// file's own coordinate-system doc comment) must map to a positive xnaY
// in ROTATION_90 and a positive rawY must map to positive xnaY in
// ROTATION_270 (the two rotations report opposite raw signs for the same
// physical tilt direction, per the documented rationale).
TEST(AndroidSensorOrientationTests, RightTiltIsAlwaysPositiveYRegardlessOfRotation)
{
    constexpr float TiltMagnitude = 0.6f;

    const Vector3 rotation90Result = ConvertAndroidPortraitToXnaLandscape(
        0.0f, -TiltMagnitude, 0.0f, AndroidSensorLandscapeOrientation::Rotation90);
    const Vector3 rotation270Result = ConvertAndroidPortraitToXnaLandscape(
        0.0f, TiltMagnitude, 0.0f, AndroidSensorLandscapeOrientation::Rotation270);

    EXPECT_GT(rotation90Result.Y, 0.0f);
    EXPECT_GT(rotation270Result.Y, 0.0f);
}

// Task P6-7: mirror of the tilt-right test above for the opposite physical
// direction — a left tilt must be the sign-inverse of a right tilt in each
// rotation (same magnitude, opposite raw sign as the tilt-right case),
// consistently producing xnaY < 0 regardless of rotation.
TEST(AndroidSensorOrientationTests, LeftTiltIsAlwaysNegativeYRegardlessOfRotation)
{
    constexpr float TiltMagnitude = 0.6f;

    const Vector3 rotation90Result = ConvertAndroidPortraitToXnaLandscape(
        0.0f, TiltMagnitude, 0.0f, AndroidSensorLandscapeOrientation::Rotation90);
    const Vector3 rotation270Result = ConvertAndroidPortraitToXnaLandscape(
        0.0f, -TiltMagnitude, 0.0f, AndroidSensorLandscapeOrientation::Rotation270);

    EXPECT_LT(rotation90Result.Y, 0.0f);
    EXPECT_LT(rotation270Result.Y, 0.0f);
}

// Task P6-7: Z is untouched by either rotation's formula (both return rawZ
// verbatim) — laying the device flat, screen facing up (away from the
// ground), is the "perpendicular to screen" case both files' doc comments
// describe: the raw Z axis ("out of screen, toward the user") points away
// from gravity, so a real accelerometer reports a positive value here.
// Since the formula never negates or remaps Z, this holds identically in
// both rotations — unlike X/Y, this does not depend on which landscape
// rotation is active.
TEST(AndroidSensorOrientationTests, FaceUpProducesPositiveZRegardlessOfRotation)
{
    constexpr float FaceUpMagnitude = 1.0f;

    const Vector3 rotation90Result = ConvertAndroidPortraitToXnaLandscape(
        0.0f, 0.0f, FaceUpMagnitude, AndroidSensorLandscapeOrientation::Rotation90);
    const Vector3 rotation270Result = ConvertAndroidPortraitToXnaLandscape(
        0.0f, 0.0f, FaceUpMagnitude, AndroidSensorLandscapeOrientation::Rotation270);

    EXPECT_GT(rotation90Result.Z, 0.0f);
    EXPECT_GT(rotation270Result.Z, 0.0f);
}

// Task P6-7: mirror of the face-up test — flipping the device face-down
// (screen toward the ground) flips the raw Z sign, and since Z passes
// through unchanged, this holds regardless of rotation too.
TEST(AndroidSensorOrientationTests, FaceDownProducesNegativeZRegardlessOfRotation)
{
    constexpr float FaceDownMagnitude = -1.0f;

    const Vector3 rotation90Result = ConvertAndroidPortraitToXnaLandscape(
        0.0f, 0.0f, FaceDownMagnitude, AndroidSensorLandscapeOrientation::Rotation90);
    const Vector3 rotation270Result = ConvertAndroidPortraitToXnaLandscape(
        0.0f, 0.0f, FaceDownMagnitude, AndroidSensorLandscapeOrientation::Rotation270);

    EXPECT_LT(rotation90Result.Z, 0.0f);
    EXPECT_LT(rotation270Result.Z, 0.0f);
}

// Task P6-7: unlike Y (which has an explicit, WP7-documented absolute
// convention — "right tilt -> Y > 0" — confirmed above), neither
// Accelerometer.cpp/Gyroscope.cpp's own doc comment nor
// docs/devices-hardware-checklist.md assert a single authoritative sign
// for the forward/backward (X) axis; the checklist explicitly only
// requires internal consistency ("Confirm Acceleration.X's sign matches
// what feels like 'forward' tilt, consistently between runs"), deferring
// the absolute physical direction to real-hardware verification. This
// test therefore only confirms the code's own documented behavior: the
// same raw +X magnitude ("Portrait +X" in each file's coordinate-system
// comment) intentionally produces an XNA X with opposite sign between the
// two rotations (xnaX = rawX for Rotation90, xnaX = -rawX for Rotation270)
// — it does NOT assert which sign corresponds to "top edge down" versus
// "bottom edge down" in the physical world, since that has never been
// confirmed against real hardware and independently re-deriving it from
// rotation geometry alone was found, during this task's own investigation,
// to be easy to get backwards without a real device to check against.
TEST(AndroidSensorOrientationTests, ForwardBackwardSignConventionIntentionallyFlipsBetweenRotations)
{
    constexpr float TiltMagnitude = 0.5f;

    const Vector3 rotation90Result = ConvertAndroidPortraitToXnaLandscape(
        TiltMagnitude, 0.0f, 0.0f, AndroidSensorLandscapeOrientation::Rotation90);
    const Vector3 rotation270Result = ConvertAndroidPortraitToXnaLandscape(
        TiltMagnitude, 0.0f, 0.0f, AndroidSensorLandscapeOrientation::Rotation270);

    EXPECT_GT(rotation90Result.X, 0.0f);
    EXPECT_LT(rotation270Result.X, 0.0f);
}

// Task ACCEL-008: the remap is a deliberate CNA-only deviation from real WP7
// behavior (see Detail::SetAndroidLandscapeRemapEnabled()'s own doc comment) --
// defaults to enabled, preserving this codebase's existing behavior unchanged.
TEST(AndroidSensorOrientationTests, LandscapeRemapIsEnabledByDefault)
{
    EXPECT_TRUE(IsAndroidLandscapeRemapEnabled());
}

TEST(AndroidSensorOrientationTests, SetAndroidLandscapeRemapEnabledFalseDisablesIt)
{
    ScopedAndroidLandscapeRemapSetting scoped(false);

    EXPECT_FALSE(IsAndroidLandscapeRemapEnabled());
}

TEST(AndroidSensorOrientationTests, SetAndroidLandscapeRemapEnabledTrueReEnablesIt)
{
    ScopedAndroidLandscapeRemapSetting scoped(false);
    ASSERT_FALSE(IsAndroidLandscapeRemapEnabled());

    SetAndroidLandscapeRemapEnabled(true);

    EXPECT_TRUE(IsAndroidLandscapeRemapEnabled());
}
