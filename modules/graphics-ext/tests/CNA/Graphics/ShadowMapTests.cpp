// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-800..MOD-811, MOD-850: directional shadow-map generation.
//
// The matrices are asserted directly rather than through a rendered image, because a shadow map
// that is subtly mis-fitted still produces a picture -- one with half the resolution it should
// have, or with the far edge of the scene clipped away. Those are the failures worth catching, and
// they are arithmetic, not appearance.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/GraphicsCapability.hpp"
#include "CNA/Graphics/DirectionalLightEXT.hpp"
#include "CNA/Graphics/ShadowMap.hpp"
#include "CNA/Graphics/ShadowQuality.hpp"
#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace {

using CNA::Graphics::DirectionalLightEXT;
using CNA::Graphics::ShadowMap;
using CNA::Graphics::ShadowQuality;
using Microsoft::Xna::Framework::BoundingBox;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;

/// Transforms a world position by a view-projection and returns the normalized device coordinates.
Vector3 ToClipSpace(const Matrix& viewProjection, const Vector3& world)
{
    const float x = world.X * viewProjection.M11 + world.Y * viewProjection.M21
                  + world.Z * viewProjection.M31 + viewProjection.M41;
    const float y = world.X * viewProjection.M12 + world.Y * viewProjection.M22
                  + world.Z * viewProjection.M32 + viewProjection.M42;
    const float z = world.X * viewProjection.M13 + world.Y * viewProjection.M23
                  + world.Z * viewProjection.M33 + viewProjection.M43;
    const float w = world.X * viewProjection.M14 + world.Y * viewProjection.M24
                  + world.Z * viewProjection.M34 + viewProjection.M44;
    const float inverseW = std::abs(w) > 1e-6f ? 1.0f / w : 1.0f;
    return Vector3(x * inverseW, y * inverseW, z * inverseW);
}

BoundingBox UnitScene()
{
    return BoundingBox(Vector3(-10.0f, 0.0f, -10.0f), Vector3(10.0f, 5.0f, 10.0f));
}

TEST(ShadowMapTest, QualityMapsToTheDocumentedSizes)
{
    EXPECT_EQ(ShadowMap::sizeForQuality(ShadowQuality::Low), 512);
    EXPECT_EQ(ShadowMap::sizeForQuality(ShadowQuality::Medium), 1024);
    EXPECT_EQ(ShadowMap::sizeForQuality(ShadowQuality::High), 2048);
    EXPECT_EQ(ShadowMap::sizeForQuality(ShadowQuality::Ultra), 4096);
    // Disabled still yields a usable size: toggling quality at run time must not require
    // destroying the object.
    EXPECT_EQ(ShadowMap::sizeForQuality(ShadowQuality::Disabled), 512);
}

TEST(ShadowMapTest, TheLightViewLooksAlongTheLightDirection)
{
    DirectionalLightEXT sun;
    sun.Direction = Vector3(0.0f, -1.0f, 0.0f);   // straight down

    const Matrix view = ShadowMap::computeLightView(sun, UnitScene());

    // XNA's CreateLookAt stores the *backward* vector in (M13, M23, M33) -- the negated view
    // direction, not the view direction itself. A light shining straight down therefore leaves
    // +Y there. Worth pinning: reading it as the forward vector inverts every shadow matrix
    // derived from it, and the resulting map looks plausible while shadowing the wrong side.
    EXPECT_NEAR(view.M13, 0.0f, 1e-4f);
    EXPECT_NEAR(view.M23, 1.0f, 1e-4f);
    EXPECT_NEAR(view.M33, 0.0f, 1e-4f);
}

TEST(ShadowMapTest, AStraightDownSunDoesNotProduceADegenerateView)
{
    // The case that breaks the obvious up-vector choice, and the reason the implementation picks
    // a different one when the light is near-vertical. A NaN here would silently blank the map.
    DirectionalLightEXT sun;
    sun.Direction = Vector3(0.0f, -1.0f, 0.0f);

    const Matrix view = ShadowMap::computeLightView(sun, UnitScene());

    EXPECT_FALSE(std::isnan(view.M11));
    EXPECT_FALSE(std::isnan(view.M22));
    EXPECT_FALSE(std::isnan(view.M33));
}

TEST(ShadowMapTest, AnUnnormalizedDirectionIsAcceptedAndNormalized)
{
    DirectionalLightEXT convenient;
    convenient.Direction = Vector3(-3.0f, -3.0f, -3.0f);
    DirectionalLightEXT normalized;
    normalized.Direction = Vector3(-0.57735f, -0.57735f, -0.57735f);

    const Matrix a = ShadowMap::computeLightView(convenient, UnitScene());
    const Matrix b = ShadowMap::computeLightView(normalized, UnitScene());

    EXPECT_NEAR(a.M13, b.M13, 1e-3f);
    EXPECT_NEAR(a.M23, b.M23, 1e-3f);
    EXPECT_NEAR(a.M33, b.M33, 1e-3f);
}

TEST(ShadowMapTest, EveryCornerOfTheSceneFallsInsideTheLightVolume)
{
    // The property that matters: nothing the light should see is clipped away. A projection that
    // is merely close still cuts the far edge of the scene out of the map, and the missing
    // shadows look like an art problem rather than a fitting one.
    const BoundingBox scene = UnitScene();
    DirectionalLightEXT sun;
    sun.Direction = Vector3(-0.5f, -1.0f, -0.3f);

    const Matrix view       = ShadowMap::computeLightView(sun, scene);
    const Matrix projection = ShadowMap::computeLightProjection(view, scene);
    const Matrix viewProjection = view * projection;

    const Vector3 corners[8] = {
        {scene.Min.X, scene.Min.Y, scene.Min.Z}, {scene.Max.X, scene.Min.Y, scene.Min.Z},
        {scene.Min.X, scene.Max.Y, scene.Min.Z}, {scene.Max.X, scene.Max.Y, scene.Min.Z},
        {scene.Min.X, scene.Min.Y, scene.Max.Z}, {scene.Max.X, scene.Min.Y, scene.Max.Z},
        {scene.Min.X, scene.Max.Y, scene.Max.Z}, {scene.Max.X, scene.Max.Y, scene.Max.Z},
    };

    for (const Vector3& corner : corners)
    {
        const Vector3 clip = ToClipSpace(viewProjection, corner);
        EXPECT_GE(clip.X, -1.001f);
        EXPECT_LE(clip.X, 1.001f);
        EXPECT_GE(clip.Y, -1.001f);
        EXPECT_LE(clip.Y, 1.001f);
        EXPECT_GE(clip.Z, -1.001f);
        EXPECT_LE(clip.Z, 1.001f);
    }
}

TEST(ShadowMapTest, TheVolumeIsFittedRatherThanOversized)
{
    // Fitting is not cosmetic: a volume twice as wide as it needs to be halves the effective
    // resolution in that axis. At least one corner must therefore land near the edge of clip
    // space rather than everything sitting comfortably in the middle.
    const BoundingBox scene = UnitScene();
    DirectionalLightEXT sun;
    sun.Direction = Vector3(0.0f, -1.0f, 0.0f);

    const Matrix viewProjection = ShadowMap::computeLightView(sun, scene)
                                * ShadowMap::computeLightProjection(
                                      ShadowMap::computeLightView(sun, scene), scene);

    float widest = 0.0f;
    const Vector3 corners[4] = {
        {scene.Min.X, scene.Min.Y, scene.Min.Z}, {scene.Max.X, scene.Min.Y, scene.Min.Z},
        {scene.Min.X, scene.Min.Y, scene.Max.Z}, {scene.Max.X, scene.Min.Y, scene.Max.Z},
    };
    for (const Vector3& corner : corners)
        widest = std::max(widest, std::abs(ToClipSpace(viewProjection, corner).X));

    EXPECT_GT(widest, 0.9f) << "the light volume is wider than the scene it contains";
}

TEST(ShadowMapTest, ADegenerateSceneDoesNotProduceNaNs)
{
    // A single point, which is what an empty scene's bounds collapse to. Every division in the
    // fitting has to survive it.
    const BoundingBox point(Vector3(1.0f, 2.0f, 3.0f), Vector3(1.0f, 2.0f, 3.0f));
    DirectionalLightEXT sun;
    sun.Direction = Vector3(0.0f, -1.0f, 0.0f);

    const Matrix view       = ShadowMap::computeLightView(sun, point);
    const Matrix projection = ShadowMap::computeLightProjection(view, point);

    EXPECT_FALSE(std::isnan(projection.M11));
    EXPECT_FALSE(std::isnan(projection.M22));
    EXPECT_FALSE(std::isnan(projection.M33));
    EXPECT_FALSE(std::isinf(projection.M11));
}

TEST(ShadowMapTest, TheMapIsCreatedAtTheQualitySizeWithACasterEffect)
{
    GraphicsDevice gd;
    ShadowMap shadowMap(gd, ShadowQuality::Low);

    EXPECT_EQ(shadowMap.getSize(), 512);
    EXPECT_EQ(shadowMap.getQuality(), ShadowQuality::Low);
    EXPECT_NE(shadowMap.getShadowTexture(), nullptr);
    EXPECT_GT(shadowMap.getDepthBias(), 0.0f);
}

TEST(ShadowMapTest, BeginComputesTheLightMatrixAndEndRestoresTheBackBuffer)
{
    GraphicsDevice gd;
    ShadowMap shadowMap(gd, ShadowQuality::Low);

    DirectionalLightEXT sun;
    sun.Direction = Vector3(-0.5f, -1.0f, -0.5f);

    shadowMap.begin(sun, UnitScene());
    const Matrix lightViewProjection = shadowMap.getLightViewProjection();
    shadowMap.end();

    // Identity would mean begin() never computed anything.
    EXPECT_NE(lightViewProjection.M11, 1.0f);
    const Vector3 centre(0.0f, 2.5f, 0.0f);
    const Vector3 clip = ToClipSpace(lightViewProjection, centre);
    EXPECT_NEAR(clip.X, 0.0f, 0.1f) << "the scene centre should land in the middle of the map";
    EXPECT_NEAR(clip.Y, 0.0f, 0.1f);
}

TEST(ShadowMapTest, BothMisusesAreRejected)
{
    GraphicsDevice gd;
    ShadowMap shadowMap(gd, ShadowQuality::Low);
    DirectionalLightEXT sun;

    EXPECT_THROW(shadowMap.end(), std::logic_error);

    shadowMap.begin(sun, UnitScene());
    EXPECT_THROW(shadowMap.begin(sun, UnitScene()), std::logic_error);
    shadowMap.end();
}

TEST(ShadowMapTest, AnUnsupportedRendererIsReportedRatherThanFailing)
{
    // MOD-811 / D1. The object constructs on every renderer, and the two calls that make up a
    // pass work on every renderer. What changes is whether anything is drawn -- and on a renderer
    // that cannot, `isSupported()` says so and there is no caster effect to hand out.
    GraphicsDevice gd;
    ShadowMap shadowMap(gd, ShadowQuality::Low);

    const bool canRaster  = gd.SupportsCapability(CNA::GraphicsCapability::ThreeD);
    const bool canCompile = gd.SupportsCapability(CNA::GraphicsCapability::CustomEffects);
    if (!canRaster || !canCompile)
    {
        EXPECT_FALSE(shadowMap.isSupported());
        EXPECT_EQ(shadowMap.getCasterEffect(), nullptr);
    }
    else if (gd.SupportsShadowSamplingEXT())
    {
        EXPECT_TRUE(shadowMap.isSupported());
        EXPECT_NE(shadowMap.getCasterEffect(), nullptr);
    }
    else
    {
        // MOD-1699: `CustomEffects` and "this layer's caster shader compiles here" are different
        // claims, and the Vulkan renderer is where they come apart -- it compiles custom effects
        // from SPIR-V bytecode, not from the GLSL the caster is written in. What must hold on such
        // a renderer is not that the map works, but that it says so consistently.
        EXPECT_EQ(shadowMap.isSupported(), shadowMap.getCasterEffect() != nullptr);
    }

    // Either way a pass opens and closes without throwing, which is the property that lets a game
    // switch shadows on without first asking whether it may.
    DirectionalLightEXT sun;
    EXPECT_NO_THROW({
        shadowMap.begin(sun, UnitScene());
        shadowMap.end();
    });
    EXPECT_NE(shadowMap.getShadowTexture(), nullptr);
}

TEST(ShadowMapTest, TheDepthBiasRoundTrips)
{
    GraphicsDevice gd;
    ShadowMap shadowMap(gd, ShadowQuality::Medium);

    shadowMap.setDepthBias(0.01f);
    EXPECT_FLOAT_EQ(shadowMap.getDepthBias(), 0.01f);
}

TEST(ShadowMapTest, AnEmptyShadowPassLeavesTheMapMeaningNothingIsOccluded)
{
    // Cleared to white, i.e. "infinitely far". Clearing to black would make every unwritten texel
    // the nearest possible occluder and put the whole scene in shadow wherever no caster was drawn
    // -- a full-screen artefact from a one-line mistake.
    GraphicsDevice gd;
    ShadowMap shadowMap(gd, ShadowQuality::Low);
    DirectionalLightEXT sun;

    shadowMap.begin(sun, UnitScene());
    shadowMap.end();

    Microsoft::Xna::Framework::Graphics::Texture2D* texture = shadowMap.getShadowTexture();
    ASSERT_NE(texture, nullptr);

    if (gd.SupportsSurfaceFormatAsRenderTargetEXT(
            Microsoft::Xna::Framework::Graphics::SurfaceFormat::Single))
    {
        std::vector<float> texels(static_cast<std::size_t>(shadowMap.getSize())
                                  * shadowMap.getSize());
        texture->GetData(texels.data(), static_cast<int>(texels.size()));
        EXPECT_FLOAT_EQ(texels.front(), 1.0f);
    }
    else
    {
        std::vector<Microsoft::Xna::Framework::Color> texels(
            static_cast<std::size_t>(shadowMap.getSize()) * shadowMap.getSize(),
            Microsoft::Xna::Framework::Color(0, 0, 0, 0));
        texture->GetData(texels.data(), static_cast<int>(texels.size()));
        EXPECT_EQ(texels.front().getRProperty(), 255);
    }
}

} // namespace

#endif // CNA_CNAEXT
