// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-1005..MOD-1007: point and spot lights that are actually lit and shadowed.
//
// PointSpotShadowMapTests pins the generation matrices. This pins what no matrix assertion can:
// that the light reaches the surface at all, that its shadow darkens the right part of it, and
// that a draw which was never told about a punctual light renders exactly as it did before they
// existed. The last one is the property that makes the whole addition safe, and it is the one a
// visual check would never think to make.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/CubeShadowMap.hpp"
#include "CNA/Graphics/PointLightEXT.hpp"
#include "CNA/Graphics/ShadowQuality.hpp"
#include "CNA/Graphics/SpotLightEXT.hpp"
#include "CNA/Graphics/SpotShadowMap.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/PunctualLightEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

#include <array>
#include <vector>

namespace {

using CNA::GraphicsCapability;
using CNA::Graphics::CubeShadowMap;
using CNA::Graphics::PointLightEXT;
using CNA::Graphics::ShadowQuality;
using CNA::Graphics::SpotLightEXT;
using CNA::Graphics::SpotShadowMap;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::BasicEffect;
using Microsoft::Xna::Framework::Graphics::BlendState;
using Microsoft::Xna::Framework::Graphics::DepthFormat;
using Microsoft::Xna::Framework::Graphics::DepthStencilState;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::PrimitiveType;
using Microsoft::Xna::Framework::Graphics::PunctualLightEXT;
using Microsoft::Xna::Framework::Graphics::PunctualLightKindEXT;
using Microsoft::Xna::Framework::Graphics::RasterizerState;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::VertexPositionNormalTexture;

constexpr int   kFrame       = 64;
constexpr float kGroundHalf  = 8.0f;
constexpr float kCasterHalf  = 2.0f;
constexpr float kCasterHigh  = 3.0f;
constexpr float kLightHeight = 7.0f;
constexpr float kRange       = 30.0f;

std::array<VertexPositionNormalTexture, 6> Quad(float y, float halfExtent)
{
    const Vector3 up(0.0f, 1.0f, 0.0f);
    const float e = halfExtent;
    const auto v = [&](float x, float z) {
        return VertexPositionNormalTexture(Vector3(x, y, z), up, Vector2(0.0f, 0.0f));
    };
    return {v(-e, -e), v(e, -e), v(e, e), v(-e, -e), v(e, e), v(-e, e)};
}

Matrix TopDownView()
{
    return Matrix::CreateLookAt(Vector3(0.0f, 20.0f, 0.0f), Vector3::Zero,
                                Vector3(0.0f, 0.0f, 1.0f));
}

Matrix FitToGround()
{
    return Matrix::CreateOrthographic(kGroundHalf * 2.0f, kGroundHalf * 2.0f, 0.1f, 60.0f);
}

struct Frame
{
    std::vector<Color> pixels;
    [[nodiscard]] int At(int x, int y) const
    {
        return pixels[static_cast<std::size_t>(y) * kFrame + static_cast<std::size_t>(x)]
            .getRProperty();
    }
};

Frame Capture(RenderTarget2D& target)
{
    Frame frame;
    frame.pixels.assign(static_cast<std::size_t>(kFrame) * kFrame, Color::Transparent);
    const Rectangle region(0, 0, kFrame, kFrame);
    target.GetData(0, &region, frame.pixels.data(), 0, static_cast<int>(frame.pixels.size()));
    return frame;
}

/// Unlit apart from a small ambient floor, so anything brighter than the floor came from the
/// punctual light and nothing else. The three directional slots are switched off deliberately:
/// with one of them on, a point light contributing nothing would still look plausible.
void ConfigureAmbientOnly(BasicEffect& effect)
{
    effect.setLightingEnabledProperty(true);
    effect.setPreferPerPixelLightingProperty(true);
    effect.setTextureEnabledProperty(false);
    effect.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
    effect.setAmbientLightColorProperty(Vector3(0.1f, 0.1f, 0.1f));
    effect.setSpecularColorProperty(Vector3::Zero);
    effect.setEmissiveColorProperty(Vector3::Zero);
    effect.getDirectionalLight0Property().setEnabledProperty(false);
    effect.getDirectionalLight1Property().setEnabledProperty(false);
    effect.getDirectionalLight2Property().setEnabledProperty(false);
    effect.setWorldProperty(Matrix::getIdentityProperty());
    effect.setViewProperty(TopDownView());
    effect.setProjectionProperty(FitToGround());
}

void DrawGround(GraphicsDevice& device, BasicEffect& effect, RenderTarget2D& target)
{
    const auto ground = Quad(0.0f, kGroundHalf);
    device.setRasterizerStateProperty(RasterizerState::CullNone);
    device.setDepthStencilStateProperty(DepthStencilState::Default);
    device.setBlendStateProperty(BlendState::Opaque);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    effect.Apply();
    device.DrawUserPrimitives(PrimitiveType::TriangleList, ground.data(), 0, 2);
    device.SetRenderTarget(nullptr);
}

class PunctualShadowVisibilityTest : public ::testing::Test
{
protected:
    GraphicsDevice device;

    void SetUp() override
    {
        if (!device.SupportsCapability(GraphicsCapability::ThreeD))
            GTEST_SKIP() << "this renderer does not raster 3D triangles";
        if (!device.SupportsCapability(GraphicsCapability::CustomEffects))
            GTEST_SKIP() << "this renderer cannot compile the punctual caster's shader";
        // MOD-1699: compiling the caster's shader is not the same promise as SAMPLING the shadow.
        // The Vulkan renderer answers true to the first (its ShaderEffect exists) and false to the
        // second (its lit shaders ignore the state), so without this the cascade case did not skip
        // there -- it failed, describing a feature that renderer never claimed to have.
        if (!device.SupportsShadowSamplingEXT())
            GTEST_SKIP() << "this renderer's lit shaders do not sample shadow maps";
    }
};

TEST_F(PunctualShadowVisibilityTest, ADrawWithNoPunctualLightIsUnchanged)
{
    // The property the whole addition rests on. `Kind == None` is the default, and an effect that
    // never hears about punctual lights must fill GpuDrawParams exactly as it did before they
    // existed -- otherwise every game that does not use them pays for them.
    BasicEffect effect(device);
    ConfigureAmbientOnly(effect);

    CNA::Internal::Renderers::GpuDrawParams params;
    effect.FillGpuDrawParams(params);

    EXPECT_EQ(params.punctualKind, 0);
    EXPECT_EQ(params.punctualShadowCube, nullptr);
    EXPECT_EQ(params.punctualShadowMap, nullptr);
    EXPECT_EQ(effect.getPunctualLightEXT().Kind, PunctualLightKindEXT::None);
}

TEST_F(PunctualShadowVisibilityTest, APointLightBrightensTheGroundBeneathIt)
{
    // Before the shadow: the light has to reach the surface at all. A point light that contributes
    // nothing would leave every later assertion about its shadow passing for the wrong reason.
    RenderTarget2D target(device, kFrame, kFrame, false, SurfaceFormat::Color,
                          DepthFormat::Depth24);

    BasicEffect effect(device);
    ConfigureAmbientOnly(effect);
    DrawGround(device, effect, target);
    const int ambientOnly = Capture(target).At(kFrame / 2, kFrame / 2);

    PunctualLightEXT lamp;
    lamp.Kind         = PunctualLightKindEXT::Point;
    lamp.Position     = Vector3(0.0f, kLightHeight, 0.0f);
    lamp.DiffuseColor = Vector3(60.0f, 60.0f, 60.0f);   // inverse-square, so a lamp needs power
    lamp.Range        = kRange;
    effect.setPunctualLightEXT(lamp);
    DrawGround(device, effect, target);
    const Frame lit = Capture(target);

    EXPECT_GT(lit.At(kFrame / 2, kFrame / 2), ambientOnly)
        << "the point light did not reach the ground under it";
    // And it falls off: directly underneath is nearer than the corner, so it must be brighter.
    EXPECT_GT(lit.At(kFrame / 2, kFrame / 2), lit.At(2, 2))
        << "the light did not fall off with distance";
}

TEST_F(PunctualShadowVisibilityTest, APointLightsCubeShadowDarkensWhatItOccludes)
{
    // MOD-1006, through the whole seam: six faces generated, sampled by direction, compared as
    // distance over range.
    CubeShadowMap cube(device, ShadowQuality::Medium);
    if (!cube.isSupported())
        GTEST_SKIP() << "this renderer cannot compile the cube caster";

    RenderTarget2D target(device, kFrame, kFrame, false, SurfaceFormat::Color,
                          DepthFormat::Depth24);

    PointLightEXT light;
    light.Position = Vector3(0.0f, kLightHeight, 0.0f);
    light.Range    = kRange;

    const auto caster = Quad(kCasterHigh, kCasterHalf);
    cube.update(light);
    for (int face = 0; face < CubeShadowMap::kFaceCount; ++face)
    {
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        cube.begin(face);
        device.DrawUserPrimitives(PrimitiveType::TriangleList, caster.data(), 0, 2);
        cube.end();
    }

    BasicEffect effect(device);
    ConfigureAmbientOnly(effect);
    PunctualLightEXT lamp;
    lamp.Kind         = PunctualLightKindEXT::Point;
    lamp.Position     = light.Position;
    lamp.DiffuseColor = Vector3(60.0f, 60.0f, 60.0f);
    lamp.Range        = light.Range;
    lamp.ShadowCube   = cube.getShadowTexture();
    lamp.ShadowDepthBias = cube.getDepthBias();
    effect.setPunctualLightEXT(lamp);

    DrawGround(device, effect, target);
    const Frame frame = Capture(target);

    const int underCaster = frame.At(kFrame / 2, kFrame / 2);
    const int besideIt    = frame.At(kFrame / 2 + 22, kFrame / 2);
    EXPECT_LT(underCaster, besideIt)
        << "the ground directly under the caster is not darker than the ground beside it: "
        << underCaster << " vs " << besideIt;
}

TEST_F(PunctualShadowVisibilityTest, ASpotLightIsConfinedToItsCone)
{
    // The cone test, which is what separates a spot light from a point light with extra fields.
    RenderTarget2D target(device, kFrame, kFrame, false, SurfaceFormat::Color,
                          DepthFormat::Depth24);

    BasicEffect effect(device);
    ConfigureAmbientOnly(effect);

    PunctualLightEXT lamp;
    lamp.Kind         = PunctualLightKindEXT::Spot;
    lamp.Position     = Vector3(0.0f, kLightHeight, 0.0f);
    lamp.Direction    = Vector3(0.0f, -1.0f, 0.0f);
    lamp.DiffuseColor = Vector3(60.0f, 60.0f, 60.0f);
    lamp.Range        = kRange;
    lamp.InnerAngle   = 0.20f;
    lamp.OuterAngle   = 0.30f;
    effect.setPunctualLightEXT(lamp);

    DrawGround(device, effect, target);
    const Frame frame = Capture(target);

    const int inside  = frame.At(kFrame / 2, kFrame / 2);
    const int outside = frame.At(4, kFrame / 2);
    EXPECT_GT(inside, outside + 8)
        << "the cone did not confine the light: centre " << inside << ", edge " << outside;
}

TEST_F(PunctualShadowVisibilityTest, ASpotLightsShadowDarkensWhatItOccludes)
{
    SpotShadowMap spot(device, ShadowQuality::Medium);
    if (!spot.isSupported())
        GTEST_SKIP() << "this renderer cannot compile the spot caster";

    RenderTarget2D target(device, kFrame, kFrame, false, SurfaceFormat::Color,
                          DepthFormat::Depth24);

    SpotLightEXT light;
    light.Position   = Vector3(0.0f, kLightHeight, 0.0f);
    light.Direction  = Vector3(0.0f, -1.0f, 0.0f);
    light.Range      = kRange;
    light.OuterAngle = 0.6f;

    // A smaller caster than the point-light case uses, and the size is the whole design of this
    // scene: the cone reaches +-7*tan(0.6) = +-4.8 on the ground, and a half-extent-2 caster at
    // height 3 would throw a shadow to +-4.7 -- leaving no lit ground inside the cone to compare
    // against. Half-extent 1 casts to +-2.3 and leaves a wide lit ring.
    constexpr float kSpotCasterHalf = 1.0f;
    const auto caster = Quad(kCasterHigh, kSpotCasterHalf);
    device.setRasterizerStateProperty(RasterizerState::CullNone);
    device.setDepthStencilStateProperty(DepthStencilState::Default);
    spot.begin(light);
    device.DrawUserPrimitives(PrimitiveType::TriangleList, caster.data(), 0, 2);
    spot.end();

    BasicEffect effect(device);
    ConfigureAmbientOnly(effect);
    PunctualLightEXT lamp;
    lamp.Kind                 = PunctualLightKindEXT::Spot;
    lamp.Position             = light.Position;
    lamp.Direction            = light.Direction;
    lamp.DiffuseColor         = Vector3(60.0f, 60.0f, 60.0f);
    lamp.Range                = light.Range;
    lamp.InnerAngle           = 0.5f;
    lamp.OuterAngle           = light.OuterAngle;
    lamp.ShadowMap            = spot.getShadowTexture();
    lamp.ShadowViewProjection = spot.getLightViewProjection();
    lamp.ShadowDepthBias      = spot.getDepthBias();
    effect.setPunctualLightEXT(lamp);

    DrawGround(device, effect, target);
    const Frame frame = Capture(target);

    // Inside the cone and outside the shadow: world x = 3.5, between the shadow's edge at 2.3 and
    // the cone's at 4.8. Sampling at the frame's own edge instead would read ground the cone never
    // reaches, which is dark for a reason that has nothing to do with the shadow.
    const int underCaster = frame.At(kFrame / 2, kFrame / 2);
    const int besideIt    = frame.At(static_cast<int>((3.5f / (kGroundHalf * 2.0f) + 0.5f) * kFrame),
                                     kFrame / 2);
    EXPECT_LT(underCaster, besideIt)
        << "the spot's shadow did not darken the ground under the caster: " << underCaster
        << " vs " << besideIt;
}

TEST_F(PunctualShadowVisibilityTest, ALightWithNoMapIsLitButUnshadowed)
{
    // A fill light rarely needs a shadow, and attaching none must not mean attaching a black one.
    RenderTarget2D target(device, kFrame, kFrame, false, SurfaceFormat::Color,
                          DepthFormat::Depth24);

    BasicEffect effect(device);
    ConfigureAmbientOnly(effect);
    DrawGround(device, effect, target);
    const int ambientOnly = Capture(target).At(kFrame / 2, kFrame / 2);

    PunctualLightEXT lamp;
    lamp.Kind         = PunctualLightKindEXT::Point;
    lamp.Position     = Vector3(0.0f, kLightHeight, 0.0f);
    lamp.DiffuseColor = Vector3(60.0f, 60.0f, 60.0f);
    lamp.Range        = kRange;
    lamp.ShadowCube   = nullptr;
    effect.setPunctualLightEXT(lamp);

    DrawGround(device, effect, target);
    EXPECT_GT(Capture(target).At(kFrame / 2, kFrame / 2), ambientOnly);
}

TEST_F(PunctualShadowVisibilityTest, TheLightStateReachesGpuDrawParamsIntact)
{
    CubeShadowMap cube(device, ShadowQuality::Low);
    PointLightEXT light;
    light.Range = kRange;
    cube.update(light);

    BasicEffect effect(device);
    PunctualLightEXT lamp;
    lamp.Kind            = PunctualLightKindEXT::Point;
    lamp.Position        = Vector3(1.0f, 2.0f, 3.0f);
    lamp.DiffuseColor    = Vector3(0.25f, 0.5f, 0.75f);
    lamp.Range           = kRange;
    lamp.ShadowCube      = cube.getShadowTexture();
    lamp.ShadowDepthBias = 0.02f;
    effect.setPunctualLightEXT(lamp);

    CNA::Internal::Renderers::GpuDrawParams params;
    effect.FillGpuDrawParams(params);

    EXPECT_EQ(params.punctualKind, 1);
    EXPECT_FLOAT_EQ(params.punctualPosition[1], 2.0f);
    EXPECT_FLOAT_EQ(params.punctualDiffuse[2], 0.75f);
    EXPECT_FLOAT_EQ(params.punctualRange, kRange);
    EXPECT_FLOAT_EQ(params.punctualShadowBias, 0.02f);
    EXPECT_NE(params.punctualShadowCube, nullptr);
    EXPECT_EQ(params.punctualShadowMap, nullptr);
    // The cone cosines are precomputed rather than left to the shader.
    EXPECT_LE(params.punctualCosOuter, params.punctualCosInner);
}

} // namespace

#endif // CNA_CNAEXT
