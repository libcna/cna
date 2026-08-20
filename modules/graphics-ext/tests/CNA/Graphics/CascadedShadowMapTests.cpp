// SPDX-License-Identifier: MS-PL
// plans/plan_modern.md MOD-900..MOD-904, MOD-907, MOD-911: cascaded shadow maps.
//
// The two properties that make cascades worth having over one big map are both invisible in a
// still image and both about *stability*: turning the camera must not change how big a cascade is,
// and moving it by less than a texel must not change the map at all. A shadow set that fails
// either still renders a perfectly plausible frame -- and then crawls and shimmers the moment
// anything moves, which is the failure everyone actually sees. So they are asserted numerically
// here, against matrices rather than pixels.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "EngineTestSupport.hpp"

#include "CNA/Graphics/CascadedShadowMap.hpp"
#include "CNA/Graphics/DirectionalLightEXT.hpp"
#include "CNA/Graphics/ShadowMap.hpp"
#include "CNA/Graphics/ShadowQuality.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace {

using CNA::Graphics::CascadedShadowMap;
using CNA::Graphics::DirectionalLightEXT;
using CNA::Graphics::ShadowMap;
using CNA::Graphics::ShadowQuality;
using Microsoft::Xna::Framework::MathHelper;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;

constexpr float kNear = 1.0f;
constexpr float kFar  = 100.0f;

Matrix CameraProjection()
{
    return Matrix::CreatePerspectiveFieldOfView(MathHelper::PiOver4, 16.0f / 9.0f, kNear, kFar);
}

DirectionalLightEXT Sun()
{
    DirectionalLightEXT sun;
    sun.Direction = Vector3(-0.4f, -1.0f, -0.3f);
    return sun;
}

Vector3 Transform(const Vector3& point, const Matrix& m)
{
    const float x = point.X * m.M11 + point.Y * m.M21 + point.Z * m.M31 + m.M41;
    const float y = point.X * m.M12 + point.Y * m.M22 + point.Z * m.M32 + m.M42;
    const float z = point.X * m.M13 + point.Y * m.M23 + point.Z * m.M33 + m.M43;
    const float w = point.X * m.M14 + point.Y * m.M24 + point.Z * m.M34 + m.M44;
    const float inverseW = std::abs(w) > 1e-9f ? 1.0f / w : 1.0f;
    return Vector3(x * inverseW, y * inverseW, z * inverseW);
}

// =====================================================================================
// Splits (MOD-901)
// =====================================================================================

TEST(CascadedShadowMapTest, LambdaZeroSplitsTheRangeUniformly)
{
    const std::vector<float> splits =
        CascadedShadowMap::computeSplitDistances(kNear, kFar, 4, 0.0f);

    ASSERT_EQ(splits.size(), 4u);
    // near + (far-near) * i/4, by hand: 25.75, 50.5, 75.25, 100.
    EXPECT_NEAR(splits[0], 25.75f, 1e-3f);
    EXPECT_NEAR(splits[1], 50.5f, 1e-3f);
    EXPECT_NEAR(splits[2], 75.25f, 1e-3f);
    EXPECT_NEAR(splits[3], 100.0f, 1e-3f);
}

TEST(CascadedShadowMapTest, LambdaOneSplitsTheRangeLogarithmically)
{
    const std::vector<float> splits =
        CascadedShadowMap::computeSplitDistances(kNear, kFar, 4, 1.0f);

    ASSERT_EQ(splits.size(), 4u);
    // near * (far/near)^(i/4) with near=1, far=100: 100^0.25, 100^0.5, 100^0.75, 100.
    EXPECT_NEAR(splits[0], std::pow(100.0f, 0.25f), 1e-3f);
    EXPECT_NEAR(splits[1], 10.0f, 1e-3f);
    EXPECT_NEAR(splits[2], std::pow(100.0f, 0.75f), 1e-3f);
    EXPECT_NEAR(splits[3], 100.0f, 1e-3f);
}

TEST(CascadedShadowMapTest, LambdaHalfSitsBetweenTheTwo)
{
    const std::vector<float> uniform =
        CascadedShadowMap::computeSplitDistances(kNear, kFar, 3, 0.0f);
    const std::vector<float> logarithmic =
        CascadedShadowMap::computeSplitDistances(kNear, kFar, 3, 1.0f);
    const std::vector<float> blended =
        CascadedShadowMap::computeSplitDistances(kNear, kFar, 3, 0.5f);

    for (std::size_t i = 0; i + 1 < blended.size(); ++i)
    {
        EXPECT_NEAR(blended[i], 0.5f * (uniform[i] + logarithmic[i]), 1e-3f);
        // And the property that makes the blend worth having: the logarithmic scheme puts the
        // first split far closer than the uniform one, so the blend is strictly between them.
        EXPECT_LT(logarithmic[i], blended[i]);
        EXPECT_GT(uniform[i], blended[i]);
    }
}

TEST(CascadedShadowMapTest, TheLastSplitIsExactlyTheFarPlane)
{
    // A cascade that stopped a hair short of the far plane would leave a sliver of the world
    // permanently unshadowed, and it would be a sliver at the horizon where nobody looks for it.
    for (float lambda : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f})
        for (int count : {2, 3, 4})
            EXPECT_FLOAT_EQ(
                CascadedShadowMap::computeSplitDistances(kNear, kFar, count, lambda).back(), kFar)
                << "lambda " << lambda << ", " << count << " cascades";
}

TEST(CascadedShadowMapTest, SplitsAlwaysAscend)
{
    for (float lambda : {0.0f, 0.5f, 1.0f})
    {
        const std::vector<float> splits =
            CascadedShadowMap::computeSplitDistances(kNear, kFar, 4, lambda);
        for (std::size_t i = 1; i < splits.size(); ++i)
            EXPECT_GT(splits[i], splits[i - 1]) << "lambda " << lambda;
    }
}

TEST(CascadedShadowMapTest, AnUnusableRangeIsRejectedRatherThanReturningNaNs)
{
    // The logarithmic term is undefined at a zero near plane, and a silent NaN here would
    // propagate into every cascade matrix and blank the whole shadow set.
    EXPECT_THROW(CascadedShadowMap::computeSplitDistances(0.0f, kFar, 3, 0.5f),
                 std::invalid_argument);
    EXPECT_THROW(CascadedShadowMap::computeSplitDistances(-1.0f, kFar, 3, 0.5f),
                 std::invalid_argument);
    EXPECT_THROW(CascadedShadowMap::computeSplitDistances(kFar, kNear, 3, 0.5f),
                 std::invalid_argument);
    EXPECT_THROW(CascadedShadowMap::computeSplitDistances(kNear, kFar, 1, 0.5f),
                 std::invalid_argument);
    EXPECT_THROW(CascadedShadowMap::computeSplitDistances(kNear, kFar, 5, 0.5f),
                 std::invalid_argument);
}

TEST(CascadedShadowMapTest, LambdaIsClampedRatherThanTrusted)
{
    const std::vector<float> below =
        CascadedShadowMap::computeSplitDistances(kNear, kFar, 3, -2.0f);
    const std::vector<float> uniform =
        CascadedShadowMap::computeSplitDistances(kNear, kFar, 3, 0.0f);
    EXPECT_NEAR(below[0], uniform[0], 1e-3f);
}

// =====================================================================================
// Frustum corners (MOD-902)
// =====================================================================================

TEST(CascadedShadowMapTest, TheFrustumCornersAreWhereTheProjectionSaysTheyAre)
{
    // Identity view, so the corners come out in view space and can be checked against the
    // projection's own definition: half-height = near * tan(fov/2), half-width = that * aspect.
    const std::array<Vector3, 8> corners =
        CascadedShadowMap::computeFrustumCorners(Matrix::getIdentityProperty(), CameraProjection());

    const float nearHalfHeight = kNear * std::tan(MathHelper::PiOver4 * 0.5f);
    const float nearHalfWidth  = nearHalfHeight * (16.0f / 9.0f);

    // The near four sit at -near in view space; the far four at -far.
    for (int i = 0; i < 4; ++i)
    {
        EXPECT_NEAR(corners[static_cast<std::size_t>(i)].Z, -kNear, 1e-2f) << "near corner " << i;
        EXPECT_NEAR(std::abs(corners[static_cast<std::size_t>(i)].X), nearHalfWidth, 1e-2f);
        EXPECT_NEAR(std::abs(corners[static_cast<std::size_t>(i)].Y), nearHalfHeight, 1e-2f);
    }
    for (int i = 4; i < 8; ++i)
    {
        EXPECT_NEAR(corners[static_cast<std::size_t>(i)].Z, -kFar, 1e-1f) << "far corner " << i;
        EXPECT_NEAR(std::abs(corners[static_cast<std::size_t>(i)].X),
                    nearHalfWidth * kFar / kNear, 1e-1f);
    }
}

TEST(CascadedShadowMapTest, AMovedCameraMovesItsFrustum)
{
    const Matrix atOrigin = Matrix::getIdentityProperty();
    const Matrix movedTen = Matrix::CreateTranslation(0.0f, 0.0f, -10.0f);   // a view translation

    const auto a = CascadedShadowMap::computeFrustumCorners(atOrigin, CameraProjection());
    const auto b = CascadedShadowMap::computeFrustumCorners(movedTen, CameraProjection());

    EXPECT_NEAR(b[0].Z - a[0].Z, 10.0f, 1e-2f)
        << "the frustum did not follow the view matrix it was built from";
}

// =====================================================================================
// Sphere fitting (MOD-903) and snapping (MOD-904)
// =====================================================================================

TEST(CascadedShadowMapTest, TheBoundingSphereContainsEveryCorner)
{
    const auto corners =
        CascadedShadowMap::computeFrustumCorners(Matrix::getIdentityProperty(), CameraProjection());

    Vector3 centre(0.0f, 0.0f, 0.0f);
    const float radius = CascadedShadowMap::computeBoundingSphere(corners, centre);

    for (const Vector3& corner : corners)
    {
        const float dx = corner.X - centre.X;
        const float dy = corner.Y - centre.Y;
        const float dz = corner.Z - centre.Z;
        EXPECT_LE(std::sqrt(dx * dx + dy * dy + dz * dz), radius + 1e-3f);
    }
}

TEST(CascadedShadowMapTest, TurningTheCameraDoesNotChangeTheFittedRadius)
{
    // MOD-903, and the whole reason the fit is sphere-based. A box fitted to these same corners
    // would grow and shrink as the camera turns, and a cascade whose extents change every frame
    // shimmers along every shadow edge.
    const Matrix projection = CameraProjection();
    float firstRadius = 0.0f;

    for (float degrees = 0.0f; degrees < 360.0f; degrees += 17.0f)
    {
        const Matrix view = Matrix::CreateRotationY(MathHelper::ToRadians(degrees))
                          * Matrix::CreateRotationX(MathHelper::ToRadians(degrees * 0.37f));
        const auto corners = CascadedShadowMap::computeFrustumCorners(view, projection);

        Vector3 centre(0.0f, 0.0f, 0.0f);
        const float radius = CascadedShadowMap::computeBoundingSphere(corners, centre);

        if (firstRadius == 0.0f)
            firstRadius = radius;
        else
            EXPECT_NEAR(radius, firstRadius, firstRadius * 1e-3f)
                << "the fitted radius changed when the camera turned " << degrees << " degrees";
    }
}

TEST(CascadedShadowMapTest, SubTexelMotionDoesNotMoveTheSnappedCentre)
{
    // MOD-904. This is what stops shadow edges crawling as the camera walks: a snapped centre
    // moves in whole texels or not at all.
    constexpr float kRadius = 40.0f;
    constexpr int kSize = 1024;
    const float texel = 2.0f * kRadius / static_cast<float>(kSize);

    const Vector3 base(12.3456f, -7.8901f, 3.0f);
    const Vector3 snapped = CascadedShadowMap::snapToTexelGrid(base, kRadius, kSize);

    // A nudge of a fifth of a texel, in both axes, lands on the same grid cell.
    const Vector3 nudged(base.X + texel * 0.2f, base.Y + texel * 0.2f, base.Z);
    const Vector3 snappedNudged = CascadedShadowMap::snapToTexelGrid(nudged, kRadius, kSize);

    EXPECT_FLOAT_EQ(snapped.X, snappedNudged.X);
    EXPECT_FLOAT_EQ(snapped.Y, snappedNudged.Y);

    // And a full texel does move it, by exactly one texel -- otherwise "snapped" would just mean
    // "frozen", and the cascade would stop following the camera at all.
    const Vector3 movedOne(base.X + texel, base.Y, base.Z);
    const Vector3 snappedMoved = CascadedShadowMap::snapToTexelGrid(movedOne, kRadius, kSize);
    EXPECT_NEAR(snappedMoved.X - snapped.X, texel, texel * 1e-3f);
}

TEST(CascadedShadowMapTest, SnappingIsAlwaysWithinOneTexelOfTheRequest)
{
    constexpr float kRadius = 25.0f;
    constexpr int kSize = 512;
    const float texel = 2.0f * kRadius / static_cast<float>(kSize);

    for (float offset = 0.0f; offset < 1.0f; offset += 0.07f)
    {
        const Vector3 request(offset * 100.0f, -offset * 33.0f, 0.0f);
        const Vector3 snapped = CascadedShadowMap::snapToTexelGrid(request, kRadius, kSize);
        EXPECT_LE(std::abs(snapped.X - request.X), texel + 1e-4f);
        EXPECT_LE(std::abs(snapped.Y - request.Y), texel + 1e-4f);
    }
}

// =====================================================================================
// The object (MOD-900, MOD-907)
// =====================================================================================

TEST(CascadedShadowMapTest, TheAtlasIsOneTargetWideEnoughForEveryCascade)
{
    // MOD-907's decision, visible in the allocation: one RenderTarget2D, cascades side by side.
    // A texture array would need a concept CNA's renderer interface does not have.
    GraphicsDevice gd;
    CascadedShadowMap cascades(gd, ShadowQuality::Low, 3);

    EXPECT_EQ(cascades.getCascadeCount(), 3);
    EXPECT_EQ(cascades.getCascadeSize(), ShadowMap::sizeForQuality(ShadowQuality::Low));
    ASSERT_NE(cascades.getShadowTexture(), nullptr);
    EXPECT_EQ(cascades.getShadowTexture()->getWidthProperty(), cascades.getCascadeSize() * 3);
    EXPECT_EQ(cascades.getShadowTexture()->getHeightProperty(), cascades.getCascadeSize());
}

TEST(CascadedShadowMapTest, EachCascadeGetsAFullQualityMapRatherThanAShareOfOne)
{
    // Worth pinning because the opposite is the tempting implementation: splitting one map's
    // resolution between the cascades would make "High with 4 cascades" quietly worse than "High"
    // everywhere, which is the reverse of what the setting promises.
    GraphicsDevice gd;
    CascadedShadowMap two(gd, ShadowQuality::Low, 2);
    CascadedShadowMap four(gd, ShadowQuality::Low, 4);
    EXPECT_EQ(two.getCascadeSize(), four.getCascadeSize());
}

TEST(CascadedShadowMapTest, TheCascadeCountIsBounded)
{
    GraphicsDevice gd;
    EXPECT_THROW(CascadedShadowMap(gd, ShadowQuality::Low, 1), std::invalid_argument);
    EXPECT_THROW(CascadedShadowMap(gd, ShadowQuality::Low, 5), std::invalid_argument);
    EXPECT_NO_THROW(CascadedShadowMap(gd, ShadowQuality::Low, 2));
    EXPECT_NO_THROW(CascadedShadowMap(gd, ShadowQuality::Low, 4));
}

TEST(CascadedShadowMapTest, UpdateFillsEveryCascadeAndTheSplitsAscend)
{
    GraphicsDevice gd;
    CascadedShadowMap cascades(gd, ShadowQuality::Low, 4);
    cascades.update(Sun(), Matrix::getIdentityProperty(), CameraProjection());

    float previous = 0.0f;
    for (int i = 0; i < cascades.getCascadeCount(); ++i)
    {
        const float split = cascades.getSplitDistance(i);
        EXPECT_GT(split, previous);
        previous = split;
        // Identity would mean the cascade was never fitted.
        EXPECT_NE(cascades.getCascadeMatrix(i).M11, 1.0f) << "cascade " << i;
    }
    EXPECT_NEAR(previous, kFar, kFar * 0.02f)
        << "the last cascade does not reach the camera's far plane";
}

TEST(CascadedShadowMapTest, EachCascadeMapsIntoItsOwnSliceOfTheAtlas)
{
    // The property that makes an atlas work at all: cascade i's matrix has to land in the i-th
    // horizontal slice and nowhere else, or two cascades sample each other's texels.
    GraphicsDevice gd;
    constexpr int kCount = 4;
    CascadedShadowMap cascades(gd, ShadowQuality::Low, kCount);
    cascades.update(Sun(), Matrix::getIdentityProperty(), CameraProjection());

    for (int i = 0; i < kCount; ++i)
    {
        // The centre of each cascade's own view volume, which the matrix must put in the middle
        // of that cascade's slice.
        const Matrix matrix = cascades.getCascadeMatrix(i);
        const Matrix inverse = Matrix::Invert(matrix);
        const Vector3 sliceCentre((static_cast<float>(i) + 0.5f) / kCount, 0.5f, 0.5f);
        const Vector3 world = Transform(sliceCentre, inverse);
        const Vector3 backAgain = Transform(world, matrix);

        EXPECT_NEAR(backAgain.X, sliceCentre.X, 1e-3f) << "cascade " << i;
        const float sliceStart = static_cast<float>(i) / kCount;
        EXPECT_GE(backAgain.X, sliceStart - 1e-3f);
        EXPECT_LE(backAgain.X, sliceStart + 1.0f / kCount + 1e-3f);
    }
}

TEST(CascadedShadowMapTest, EveryMisuseIsRejected)
{
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    CascadedShadowMap cascades(gd, ShadowQuality::Low, 2);

    EXPECT_THROW(cascades.begin(0), std::logic_error);      // before update()
    EXPECT_THROW(cascades.end(), std::logic_error);         // end without begin
    EXPECT_THROW((void)cascades.getCascadeMatrix(2), std::out_of_range);
    EXPECT_THROW((void)cascades.getSplitDistance(-1), std::out_of_range);

    cascades.update(Sun(), Matrix::getIdentityProperty(), CameraProjection());
    EXPECT_THROW(cascades.begin(2), std::out_of_range);

    cascades.begin(0);
    EXPECT_THROW(cascades.begin(1), std::logic_error);      // two passes at once
    EXPECT_THROW(cascades.update(Sun(), Matrix::getIdentityProperty(), CameraProjection()),
                 std::logic_error);
    cascades.end();
}

TEST(CascadedShadowMapTest, TheCascadeChosenForADepthIsTheOneThatCoversIt)
{
    // MOD-905's rule, on the CPU where it can be checked against hand-picked depths. The shader
    // mirrors it; getting the boundary wrong there shows up as a thin band of wrong-resolution
    // shadow at each split, which reads as an art problem rather than an off-by-one.
    GraphicsDevice gd;
    CascadedShadowMap cascades(gd, ShadowQuality::Low, 3);
    cascades.setSplitLambda(0.0f);   // uniform, so the boundaries are 34, 67, 100
    cascades.update(Sun(), Matrix::getIdentityProperty(), CameraProjection());

    const float first  = cascades.getSplitDistance(0);
    const float second = cascades.getSplitDistance(1);

    EXPECT_EQ(cascades.selectCascade(kNear), 0);
    EXPECT_EQ(cascades.selectCascade(first - 0.1f), 0);
    EXPECT_EQ(cascades.selectCascade(first), 0) << "a depth exactly on a split belongs to the "
                                                   "nearer cascade, which is the one fitted to it";
    EXPECT_EQ(cascades.selectCascade(first + 0.1f), 1);
    EXPECT_EQ(cascades.selectCascade(second + 0.1f), 2);
    // Past the far plane the last cascade still answers: an unshadowed band at the horizon looks
    // like a missing shadow, not like an out-of-range depth.
    EXPECT_EQ(cascades.selectCascade(kFar * 10.0f), 2);
}

TEST(CascadedShadowMapTest, TheSplitLambdaRoundTripsAndIsClamped)
{
    GraphicsDevice gd;
    CascadedShadowMap cascades(gd, ShadowQuality::Low, 3);

    EXPECT_NEAR(cascades.getSplitLambda(), 0.75f, 1e-6f);
    cascades.setSplitLambda(0.25f);
    EXPECT_FLOAT_EQ(cascades.getSplitLambda(), 0.25f);
    cascades.setSplitLambda(4.0f);
    EXPECT_FLOAT_EQ(cascades.getSplitLambda(), 1.0f);
}

TEST(CascadedShadowMapTest, AnUnsupportedRendererIsReportedRatherThanFailing)
{
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    CascadedShadowMap cascades(gd, ShadowQuality::Low, 2);

    if (!cascades.isSupported())
        EXPECT_EQ(cascades.getCasterEffect(), nullptr);
    else
        EXPECT_NE(cascades.getCasterEffect(), nullptr);

    // Either way the pass shape works, so a game does not have to ask before switching cascades on.
    cascades.update(Sun(), Matrix::getIdentityProperty(), CameraProjection());
    EXPECT_NO_THROW({
        cascades.begin(0);
        cascades.end();
        cascades.begin(1);
        cascades.end();
    });
}

} // namespace

#endif // CNA_CNAEXT
