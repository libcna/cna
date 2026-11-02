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

// The helper's output frame has +X toward landscape right, +Y toward its top and +Z
// out of the screen. Rotation90 puts portrait +Y at landscape left; Rotation270
// puts it at landscape right. These are proper rotations, for linear and angular vectors.
TEST(AndroidSensorOrientationTests, AccelerometerRotation90ExchangesAxes)
{
    const Vector3 result = ConvertAndroidPortraitToXnaLandscape(
        0.25f, -0.75f, 1.0f, AndroidSensorLandscapeOrientation::Rotation90);
    EXPECT_FLOAT_EQ(result.X, 0.75f);
    EXPECT_FLOAT_EQ(result.Y, 0.25f);
    EXPECT_FLOAT_EQ(result.Z, 1.0f);
}

TEST(AndroidSensorOrientationTests, AccelerometerRotation270ExchangesAxes)
{
    const Vector3 result = ConvertAndroidPortraitToXnaLandscape(
        0.25f, -0.75f, 1.0f, AndroidSensorLandscapeOrientation::Rotation270);
    EXPECT_FLOAT_EQ(result.X, -0.75f);
    EXPECT_FLOAT_EQ(result.Y, -0.25f);
    EXPECT_FLOAT_EQ(result.Z, 1.0f);
}

TEST(AndroidSensorOrientationTests, GyroscopeRotation90ExchangesAxes)
{
    const Vector3 result = ConvertAndroidPortraitToXnaLandscape(
        0.5f, -1.25f, 2.0f, AndroidSensorLandscapeOrientation::Rotation90);
    EXPECT_FLOAT_EQ(result.X, 1.25f);
    EXPECT_FLOAT_EQ(result.Y, 0.5f);
    EXPECT_FLOAT_EQ(result.Z, 2.0f);
}

TEST(AndroidSensorOrientationTests, GyroscopeRotation270ExchangesAxes)
{
    const Vector3 result = ConvertAndroidPortraitToXnaLandscape(
        0.5f, -1.25f, 2.0f, AndroidSensorLandscapeOrientation::Rotation270);
    EXPECT_FLOAT_EQ(result.X, -1.25f);
    EXPECT_FLOAT_EQ(result.Y, -0.5f);
    EXPECT_FLOAT_EQ(result.Z, 2.0f);
}

TEST(AndroidSensorOrientationTests, LandscapeBasisMatchesTheDocumentedDeviceRotation)
{
    using Orientation = AndroidSensorLandscapeOrientation;
    EXPECT_EQ(ConvertAndroidPortraitToXnaLandscape(1, 0, 0, Orientation::Rotation90), Vector3::UnitY);
    EXPECT_EQ(ConvertAndroidPortraitToXnaLandscape(0, 1, 0, Orientation::Rotation90), -Vector3::UnitX);
    EXPECT_EQ(ConvertAndroidPortraitToXnaLandscape(0, 0, 1, Orientation::Rotation90), Vector3::UnitZ);
    EXPECT_EQ(ConvertAndroidPortraitToXnaLandscape(1, 0, 0, Orientation::Rotation270), -Vector3::UnitY);
    EXPECT_EQ(ConvertAndroidPortraitToXnaLandscape(0, 1, 0, Orientation::Rotation270), Vector3::UnitX);
    EXPECT_EQ(ConvertAndroidPortraitToXnaLandscape(0, 0, 1, Orientation::Rotation270), Vector3::UnitZ);
}

TEST(AndroidSensorOrientationTests, BothRotationsPreserveRightHandedCrossProductsAndLengths)
{
    const Vector3 first(0.5f, -1.25f, 2.0f);
    const Vector3 second(-3.0f, 0.75f, 1.5f);
    const Vector3 product = Vector3::Cross(first, second);
    for (const auto orientation : {AndroidSensorLandscapeOrientation::Rotation90,
                                   AndroidSensorLandscapeOrientation::Rotation270})
    {
        const auto remap = [orientation](const Vector3& v) {
            return ConvertAndroidPortraitToXnaLandscape(v.X, v.Y, v.Z, orientation);
        };
        EXPECT_EQ(Vector3::Cross(remap(first), remap(second)), remap(product));
        EXPECT_FLOAT_EQ(remap(first).LengthSquared(), first.LengthSquared());
        EXPECT_FLOAT_EQ(remap(second).LengthSquared(), second.LengthSquared());
    }
}

TEST(AndroidSensorOrientationTests, TheTwoLandscapeRotationsAreInverses)
{
    const Vector3 sample(0.5f, -1.25f, 2.0f);
    const auto first = ConvertAndroidPortraitToXnaLandscape(
        sample.X, sample.Y, sample.Z, AndroidSensorLandscapeOrientation::Rotation90);
    EXPECT_EQ(ConvertAndroidPortraitToXnaLandscape(
        first.X, first.Y, first.Z, AndroidSensorLandscapeOrientation::Rotation270), sample);
    const auto second = ConvertAndroidPortraitToXnaLandscape(
        sample.X, sample.Y, sample.Z, AndroidSensorLandscapeOrientation::Rotation270);
    EXPECT_EQ(ConvertAndroidPortraitToXnaLandscape(
        second.X, second.Y, second.Z, AndroidSensorLandscapeOrientation::Rotation90), sample);
}

TEST(AndroidSensorOrientationTests, LandscapeRightAndUpUseTheSameOutputAxesInBothRotations)
{
    using Orientation = AndroidSensorLandscapeOrientation;
    EXPECT_EQ(ConvertAndroidPortraitToXnaLandscape(0, -1, 0, Orientation::Rotation90), Vector3::UnitX);
    EXPECT_EQ(ConvertAndroidPortraitToXnaLandscape(0, 1, 0, Orientation::Rotation270), Vector3::UnitX);
    EXPECT_EQ(ConvertAndroidPortraitToXnaLandscape(1, 0, 0, Orientation::Rotation90), Vector3::UnitY);
    EXPECT_EQ(ConvertAndroidPortraitToXnaLandscape(-1, 0, 0, Orientation::Rotation270), Vector3::UnitY);
}

TEST(AndroidSensorOrientationTests, ScreenNormalIsUnchangedInBothRotations)
{
    for (const auto orientation : {AndroidSensorLandscapeOrientation::Rotation90,
                                   AndroidSensorLandscapeOrientation::Rotation270})
    {
        EXPECT_EQ(ConvertAndroidPortraitToXnaLandscape(0, 0, 1, orientation), Vector3::UnitZ);
        EXPECT_EQ(ConvertAndroidPortraitToXnaLandscape(0, 0, -1, orientation), -Vector3::UnitZ);
    }
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
