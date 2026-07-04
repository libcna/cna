// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "Microsoft/Devices/Sensors/Detail/AndroidSensorOrientation.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

using Microsoft::Devices::Sensors::Detail::AndroidSensorLandscapeOrientation;
using Microsoft::Devices::Sensors::Detail::ConvertAndroidPortraitToXnaLandscape;
using Microsoft::Xna::Framework::Vector3;

// Task P5-7: Accelerometer.cpp's/Gyroscope.cpp's #ifdef __ANDROID__ axis-remap
// code was previously untestable off-Android — it called
// SDL_GetCurrentDisplayOrientation() directly, and the whole function was
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
    // ROTATION_90 (SDL_ORIENTATION_LANDSCAPE): xnaX = rawX, xnaY = -rawY, xnaZ = rawZ.
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
    // ROTATION_270 (SDL_ORIENTATION_LANDSCAPE_FLIPPED): xnaX = -rawX, xnaY = rawY, xnaZ = rawZ.
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
