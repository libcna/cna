// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-1000..MOD-1004, MOD-1008: point and spot shadow generation.
//
// The failures worth catching here are orientations and factors of two, and all of them render a
// perfectly convincing shadow of the wrong thing: a cube face whose up vector is inverted mirrors
// that sixth of the world, a spot projection built from the half-angle instead of the full one
// leaves the cone's rim permanently lit. Neither throws and neither looks broken in isolation, so
// the matrices are asserted directly.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/CubeShadowMap.hpp"
#include "CNA/Graphics/PointLightEXT.hpp"
#include "CNA/Graphics/ShadowMap.hpp"
#include "CNA/Graphics/ShadowQuality.hpp"
#include "CNA/Graphics/SpotLightEXT.hpp"
#include "CNA/Graphics/SpotShadowMap.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"

#include <cmath>
#include <stdexcept>

namespace {

using CNA::Graphics::CubeShadowMap;
using CNA::Graphics::PointLightEXT;
using CNA::Graphics::ShadowMap;
using CNA::Graphics::ShadowQuality;
using CNA::Graphics::SpotLightEXT;
using CNA::Graphics::SpotShadowMap;
using Microsoft::Xna::Framework::MathHelper;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::CubeMapFace;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;

Vector3 ToClip(const Matrix& m, const Vector3& world)
{
    const float x = world.X * m.M11 + world.Y * m.M21 + world.Z * m.M31 + m.M41;
    const float y = world.X * m.M12 + world.Y * m.M22 + world.Z * m.M32 + m.M42;
    const float z = world.X * m.M13 + world.Y * m.M23 + world.Z * m.M33 + m.M43;
    const float w = world.X * m.M14 + world.Y * m.M24 + world.Z * m.M34 + m.M44;
    const float inverseW = std::abs(w) > 1e-9f ? 1.0f / w : 1.0f;
    return Vector3(x * inverseW, y * inverseW, z * inverseW);
}

// =====================================================================================
// Cube faces (MOD-1002)
// =====================================================================================

TEST(CubeShadowMapTest, EachFaceLooksDownItsOwnAxis)
{
    // CreateLookAt stores the BACKWARD vector in (M13, M23, M33) -- the negated view direction --
    // so a face looking down +X leaves -1 there. Reading it as forward inverts every face and
    // produces a shadow of the world seen from inside out.
    const Vector3 origin(0.0f, 0.0f, 0.0f);
    struct Expectation { CubeMapFace face; Vector3 backward; };
    const Expectation expectations[] = {
        {CubeMapFace::PositiveX, Vector3(-1.0f,  0.0f,  0.0f)},
        {CubeMapFace::NegativeX, Vector3( 1.0f,  0.0f,  0.0f)},
        {CubeMapFace::PositiveY, Vector3( 0.0f, -1.0f,  0.0f)},
        {CubeMapFace::NegativeY, Vector3( 0.0f,  1.0f,  0.0f)},
        {CubeMapFace::PositiveZ, Vector3( 0.0f,  0.0f, -1.0f)},
        {CubeMapFace::NegativeZ, Vector3( 0.0f,  0.0f,  1.0f)},
    };

    for (const Expectation& e : expectations)
    {
        const Matrix view = CubeShadowMap::computeFaceView(e.face, origin);
        EXPECT_NEAR(view.M13, e.backward.X, 1e-4f) << "face " << static_cast<int>(e.face);
        EXPECT_NEAR(view.M23, e.backward.Y, 1e-4f) << "face " << static_cast<int>(e.face);
        EXPECT_NEAR(view.M33, e.backward.Z, 1e-4f) << "face " << static_cast<int>(e.face);
    }
}

TEST(CubeShadowMapTest, NoFaceIsDegenerate)
{
    // The two Y faces are the ones a naive implementation breaks: their view direction is parallel
    // to the obvious up vector, and CreateLookAt then produces NaNs that blank a sixth of the cube.
    for (int i = 0; i < CubeShadowMap::kFaceCount; ++i)
    {
        const Matrix view =
            CubeShadowMap::computeFaceView(static_cast<CubeMapFace>(i), Vector3(3.0f, 4.0f, 5.0f));
        EXPECT_FALSE(std::isnan(view.M11)) << "face " << i;
        EXPECT_FALSE(std::isnan(view.M22)) << "face " << i;
        EXPECT_FALSE(std::isnan(view.M33)) << "face " << i;
    }
}

TEST(CubeShadowMapTest, TheSixFacesCoverEveryDirection)
{
    // The property that makes a cube a cube: every direction from the light lands inside exactly
    // one face's frustum. A gap is a wedge of the world that casts no shadow at all.
    const Vector3 light(1.0f, 2.0f, -3.0f);
    const Matrix projection = CubeShadowMap::computeFaceProjection(50.0f);

    const Vector3 directions[] = {
        { 1.0f,  0.2f,  0.1f}, {-1.0f,  0.3f, -0.2f}, { 0.1f,  1.0f,  0.2f},
        { 0.2f, -1.0f,  0.1f}, { 0.3f,  0.1f,  1.0f}, {-0.2f,  0.2f, -1.0f},
        { 0.6f,  0.6f,  0.5f}, {-0.5f, -0.6f,  0.6f},
    };

    for (const Vector3& direction : directions)
    {
        const Vector3 point(light.X + direction.X * 10.0f, light.Y + direction.Y * 10.0f,
                            light.Z + direction.Z * 10.0f);
        int covering = 0;
        for (int i = 0; i < CubeShadowMap::kFaceCount; ++i)
        {
            const Matrix viewProjection =
                CubeShadowMap::computeFaceView(static_cast<CubeMapFace>(i), light) * projection;
            const Vector3 clip = ToClip(viewProjection, point);
            if (clip.X >= -1.0f && clip.X <= 1.0f && clip.Y >= -1.0f && clip.Y <= 1.0f &&
                clip.Z >= 0.0f && clip.Z <= 1.0f)
                ++covering;
        }
        EXPECT_GE(covering, 1) << "no face covers direction (" << direction.X << ", "
                               << direction.Y << ", " << direction.Z << ")";
    }
}

TEST(CubeShadowMapTest, TheFaceProjectionIsNinetyDegreesAndSquare)
{
    // Six 90-degree square frusta tile a sphere exactly. Anything else leaves gaps or overlaps,
    // and an overlap wastes the resolution the whole six-pass cost was paid for.
    const Matrix projection = CubeShadowMap::computeFaceProjection(20.0f);
    // At 90 degrees the half-width at any depth equals that depth, so a point at (d, d, -d) in
    // view space lands exactly on the right edge of clip space.
    const Vector3 onTheEdge(5.0f, 0.0f, -5.0f);
    const Vector3 clip = ToClip(projection, onTheEdge);
    EXPECT_NEAR(clip.X, 1.0f, 1e-3f);
}

TEST(CubeShadowMapTest, TheFaceSizeIsCappedWhateverTheQualityAsks)
{
    // Six faces at 4096 is 100 million texels for one light. The quality table was written for a
    // single 2D map and means something different multiplied by six.
    EXPECT_EQ(CubeShadowMap::sizeForQuality(ShadowQuality::Low), 512);
    EXPECT_EQ(CubeShadowMap::sizeForQuality(ShadowQuality::Medium), 1024);
    EXPECT_EQ(CubeShadowMap::sizeForQuality(ShadowQuality::High), 1024);
    EXPECT_EQ(CubeShadowMap::sizeForQuality(ShadowQuality::Ultra), 1024);
    EXPECT_LE(CubeShadowMap::sizeForQuality(ShadowQuality::Ultra),
              ShadowMap::sizeForQuality(ShadowQuality::Ultra));
}

TEST(CubeShadowMapTest, TheCubeIsAllocatedAndTheLightRoundTrips)
{
    GraphicsDevice gd;
    CubeShadowMap cube(gd, ShadowQuality::Low);

    EXPECT_EQ(cube.getSize(), 512);
    EXPECT_EQ(cube.getQuality(), ShadowQuality::Low);
    ASSERT_NE(cube.getShadowTexture(), nullptr);
    EXPECT_EQ(cube.getShadowTexture()->getSizeProperty(), 512);
    EXPECT_GT(cube.getDepthBias(), 0.0f);

    PointLightEXT light;
    light.Position = Vector3(2.0f, 3.0f, 4.0f);
    light.Range    = 25.0f;
    cube.update(light);

    EXPECT_FLOAT_EQ(cube.getLightPosition().X, 2.0f);
    EXPECT_FLOAT_EQ(cube.getLightRange(), 25.0f);
}

TEST(CubeShadowMapTest, EveryMisuseIsRejected)
{
    GraphicsDevice gd;
    CubeShadowMap cube(gd, ShadowQuality::Low);
    PointLightEXT light;

    EXPECT_THROW(cube.begin(0), std::logic_error);        // before update()
    EXPECT_THROW(cube.end(), std::logic_error);           // end without begin

    // A zero range would divide the stored distance by zero: every texel a NaN, and a NaN compares
    // false, so the whole world would come out lit and nobody would look at the range.
    light.Range = 0.0f;
    EXPECT_THROW(cube.update(light), std::invalid_argument);

    light.Range = 10.0f;
    cube.update(light);
    EXPECT_THROW(cube.begin(6), std::out_of_range);
    EXPECT_THROW(cube.begin(-1), std::out_of_range);

    cube.begin(0);
    EXPECT_THROW(cube.begin(1), std::logic_error);        // two faces at once
    EXPECT_THROW(cube.update(light), std::logic_error);
    cube.end();
}

TEST(CubeShadowMapTest, EveryFaceCanBeOpenedAndClosed)
{
    GraphicsDevice gd;
    CubeShadowMap cube(gd, ShadowQuality::Low);
    PointLightEXT light;
    light.Range = 15.0f;
    cube.update(light);

    for (int face = 0; face < CubeShadowMap::kFaceCount; ++face)
        EXPECT_NO_THROW({
            cube.begin(face);
            cube.end();
        }) << "face " << face;
}

// =====================================================================================
// Spot (MOD-1004)
// =====================================================================================

TEST(SpotShadowMapTest, TheViewLooksAlongTheConeDirection)
{
    SpotLightEXT light;
    light.Position  = Vector3(0.0f, 10.0f, 0.0f);
    light.Direction = Vector3(0.0f, -1.0f, 0.0f);

    const Matrix view = SpotShadowMap::computeLightView(light);

    // Backward vector again: a cone pointing down leaves +Y in (M13, M23, M33).
    EXPECT_NEAR(view.M13, 0.0f, 1e-4f);
    EXPECT_NEAR(view.M23, 1.0f, 1e-4f);
    EXPECT_NEAR(view.M33, 0.0f, 1e-4f);
    EXPECT_FALSE(std::isnan(view.M11)) << "a straight-down cone produced a degenerate view";
}

TEST(SpotShadowMapTest, AnUnnormalizedDirectionIsAcceptedAndNormalized)
{
    SpotLightEXT convenient;
    convenient.Direction = Vector3(0.0f, -4.0f, 0.0f);
    SpotLightEXT exact;
    exact.Direction = Vector3(0.0f, -1.0f, 0.0f);

    EXPECT_NEAR(SpotShadowMap::computeLightView(convenient).M23,
                SpotShadowMap::computeLightView(exact).M23, 1e-4f);
}

TEST(SpotShadowMapTest, TheProjectionCoversTheWholeConeAndNotHalfOfIt)
{
    // The factor of two this row exists to pin. A projection built from the half-angle covers
    // half the cone, and the missing half is its rim -- which reads as the light not reaching as
    // far as it should rather than as a shadow bug.
    SpotLightEXT light;
    light.Position   = Vector3(0.0f, 0.0f, 0.0f);
    light.Direction  = Vector3(0.0f, 0.0f, -1.0f);
    light.Range      = 20.0f;
    light.OuterAngle = 0.5f;   // half-angle, radians

    const Matrix viewProjection =
        SpotShadowMap::computeLightView(light) * SpotShadowMap::computeLightProjection(light);

    // A point exactly on the cone's rim, ten units out: it must land on the edge of clip space,
    // inside it rather than beyond.
    const float distance = 10.0f;
    const float radius   = distance * std::tan(light.OuterAngle);
    const Vector3 onTheRim(0.0f, radius, -distance);
    const Vector3 clip = ToClip(viewProjection, onTheRim);

    EXPECT_NEAR(clip.Y, 1.0f, 0.02f) << "the cone's rim does not land on the edge of the map";
    EXPECT_LE(clip.Y, 1.02f);

    // And a point comfortably inside the cone lands comfortably inside the map.
    const Vector3 inside(0.0f, radius * 0.4f, -distance);
    EXPECT_LT(std::abs(ToClip(viewProjection, inside).Y), 0.6f);
}

TEST(SpotShadowMapTest, TheMapIsAllocatedAtTheFullQualitySize)
{
    // Not the cube's capped table: there is one map here, not six.
    GraphicsDevice gd;
    SpotShadowMap spot(gd, ShadowQuality::High);
    EXPECT_EQ(spot.getSize(), ShadowMap::sizeForQuality(ShadowQuality::High));
    EXPECT_NE(spot.getShadowTexture(), nullptr);
    EXPECT_GT(spot.getDepthBias(), 0.0f);
}

TEST(SpotShadowMapTest, BeginComputesTheMatrixAndEndRestoresTheBackBuffer)
{
    GraphicsDevice gd;
    SpotShadowMap spot(gd, ShadowQuality::Low);

    SpotLightEXT light;
    light.Position  = Vector3(0.0f, 8.0f, 0.0f);
    light.Direction = Vector3(0.0f, -1.0f, 0.0f);
    light.Range     = 30.0f;

    spot.begin(light);
    const Matrix viewProjection = spot.getLightViewProjection();
    spot.end();

    EXPECT_NE(viewProjection.M11, 1.0f) << "identity means begin() computed nothing";
    EXPECT_FLOAT_EQ(spot.getLightRange(), 30.0f);
    EXPECT_FLOAT_EQ(spot.getLightPosition().Y, 8.0f);

    // The point directly under the light lands in the middle of the map.
    const Vector3 clip = ToClip(viewProjection, Vector3(0.0f, 0.0f, 0.0f));
    EXPECT_NEAR(clip.X, 0.0f, 1e-3f);
    EXPECT_NEAR(clip.Y, 0.0f, 1e-3f);
}

TEST(SpotShadowMapTest, EveryMisuseIsRejected)
{
    GraphicsDevice gd;
    SpotShadowMap spot(gd, ShadowQuality::Low);
    SpotLightEXT light;

    EXPECT_THROW(spot.end(), std::logic_error);

    light.Range = 0.0f;
    EXPECT_THROW(spot.begin(light), std::invalid_argument);

    light.Range = 10.0f;
    light.OuterAngle = 0.0f;
    EXPECT_THROW(spot.begin(light), std::invalid_argument);
    // At or past a right angle the cone is a hemisphere; a perspective projection covering it
    // would need an infinite map, so this refuses rather than producing a silently clipped one.
    light.OuterAngle = MathHelper::PiOver2;
    EXPECT_THROW(spot.begin(light), std::invalid_argument);

    light.OuterAngle = 0.5f;
    spot.begin(light);
    EXPECT_THROW(spot.begin(light), std::logic_error);
    spot.end();
}

TEST(SpotShadowMapTest, AnUnsupportedRendererIsReportedRatherThanFailing)
{
    GraphicsDevice gd;
    SpotShadowMap spot(gd, ShadowQuality::Low);
    CubeShadowMap cube(gd, ShadowQuality::Low);

    const bool canRaster  = gd.SupportsCapability(CNA::GraphicsCapability::ThreeD);
    const bool canCompile = gd.SupportsCapability(CNA::GraphicsCapability::CustomEffects);
    const bool expected = canRaster && canCompile;

    EXPECT_EQ(spot.isSupported(), expected ? spot.getCasterEffect() != nullptr : false);
    EXPECT_EQ(cube.isSupported(), expected ? cube.getCasterEffect() != nullptr : false);

    // Whatever the answer, a pass opens and closes -- a game may switch these on without asking.
    SpotLightEXT light;
    EXPECT_NO_THROW({
        spot.begin(light);
        spot.end();
    });
}

} // namespace

#endif // CNA_CNAEXT
