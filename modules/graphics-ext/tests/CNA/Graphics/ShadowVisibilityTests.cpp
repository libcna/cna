// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-835, MOD-838, MOD-840, MOD-852: shadows that are actually visible.
//
// Every other shadow test in this branch checks a value on its way somewhere -- the fitted
// matrices, the state each lit effect hands to GpuDrawParams. All of them can pass while the
// picture is unchanged, because the last step, the renderer sampling the map, is the one no
// value-level assertion reaches. So this renders a scene twice and compares pixels.
//
// The scene is deliberately the simplest one that can show a shadow: a ground plane lit from
// straight overhead, and a smaller quad floating above its centre. The camera looks straight down,
// so the caster's shadow is a square in the middle of the frame and the plane's own lighting is
// uniform everywhere else. A single pixel comparison then separates "shadow" from "some other
// reason the middle is darker" -- there is no other reason available.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/DirectionalLightEXT.hpp"
#include "CNA/Graphics/ShadowMap.hpp"
#include "CNA/Graphics/ShadowQuality.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "CNA/RendererTestGate.hpp"
#include "Microsoft/Xna/Framework/BoundingBox.hpp"
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
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedPbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <type_traits>
#include <vector>

using namespace CNA::Testing::Renderers;

namespace {

using CNA::GraphicsCapability;
using CNA::Graphics::DirectionalLightEXT;
using CNA::Graphics::ShadowMap;
using CNA::Graphics::ShadowQuality;
using Microsoft::Xna::Framework::BoundingBox;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Vector4;
using Microsoft::Xna::Framework::Graphics::BasicEffect;
using Microsoft::Xna::Framework::Graphics::Effect;
using Microsoft::Xna::Framework::Graphics::PbrEffect;
using Microsoft::Xna::Framework::Graphics::SkinnedEffect;
using Microsoft::Xna::Framework::Graphics::SkinnedPbrEffect;
using Microsoft::Xna::Framework::Graphics::VertexBuffer;
using Microsoft::Xna::Framework::Graphics::BlendState;
using Microsoft::Xna::Framework::Graphics::DepthFormat;
using Microsoft::Xna::Framework::Graphics::DepthStencilState;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::PrimitiveType;
using Microsoft::Xna::Framework::Graphics::RasterizerState;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::VertexPositionNormalTexture;

constexpr int kFrame = 64;

/// Half-extent of the ground plane. Chosen so the top-down camera below sees exactly the plane
/// and nothing else, which is what lets a dark pixel mean "shadow" rather than "background".
constexpr float kGroundHalfExtent = 10.0f;
/// Half-extent of the caster, and its height above the plane.
constexpr float kCasterHalfExtent = 3.0f;
constexpr float kCasterHeight     = 3.0f;

/// An axis-aligned quad at height @p y, facing straight up, as two triangles.
std::array<VertexPositionNormalTexture, 6> Quad(float y, float halfExtent)
{
    const Vector3 up(0.0f, 1.0f, 0.0f);
    const float e = halfExtent;
    const auto vertex = [&](float x, float z, float u, float v) {
        return VertexPositionNormalTexture(Vector3(x, y, z), up, Vector2(u, v));
    };
    return {
        vertex(-e, -e, 0.0f, 0.0f), vertex(e, -e, 1.0f, 0.0f), vertex(e, e, 1.0f, 1.0f),
        vertex(-e, -e, 0.0f, 0.0f), vertex(e, e, 1.0f, 1.0f),  vertex(-e, e, 0.0f, 1.0f),
    };
}

/// The scene's bounds, padded in Y past both the plane and the caster. The padding is not
/// cosmetic: without it the plane sits exactly on the light's far plane, where its own depth
/// rounds to the edge of the comparison and the result depends on the driver.
BoundingBox SceneBounds()
{
    return BoundingBox(Vector3(-kGroundHalfExtent, -1.0f, -kGroundHalfExtent),
                       Vector3(kGroundHalfExtent, kCasterHeight + 1.0f, kGroundHalfExtent));
}

/// Straight down, so the shadow lands directly under the caster and the geometry of the test is
/// readable from the pixel coordinates alone.
DirectionalLightEXT Sun()
{
    DirectionalLightEXT sun;
    sun.Direction = Vector3(0.0f, -1.0f, 0.0f);
    return sun;
}

Matrix TopDownView()
{
    // Up is +Z because the view direction is -Y; any other choice is parallel to it.
    return Matrix::CreateLookAt(Vector3(0.0f, 20.0f, 0.0f), Vector3(0.0f, 0.0f, 0.0f),
                                Vector3(0.0f, 0.0f, 1.0f));
}

Matrix FitToGround()
{
    return Matrix::CreateOrthographic(kGroundHalfExtent * 2.0f, kGroundHalfExtent * 2.0f,
                                      0.1f, 60.0f);
}

struct Frame
{
    std::vector<Color> pixels;

    [[nodiscard]] Color At(int x, int y) const
    {
        return pixels[static_cast<std::size_t>(y) * kFrame + static_cast<std::size_t>(x)];
    }

    /// Perceived brightness is not needed here -- the light is white and the surface is grey, so
    /// the red channel alone orders the pixels exactly as luminance would.
    [[nodiscard]] int BrightnessAt(int x, int y) const { return At(x, y).getRProperty(); }
};

Frame Capture(RenderTarget2D& target)
{
    Frame frame;
    frame.pixels.assign(static_cast<std::size_t>(kFrame) * kFrame, Color::Transparent);
    const Rectangle region(0, 0, kFrame, kFrame);
    target.GetData(0, &region, frame.pixels.data(), 0, static_cast<int>(frame.pixels.size()));
    return frame;
}

/// Fills the map with the caster and leaves the device ready for the caller's own ground draw.
/// Split out of RenderScene so the skinned and PBR cases, whose ground plane needs a vertex
/// buffer rather than a user-primitive array, share the generating half exactly.
void FillShadowMap(GraphicsDevice& device, ShadowMap& shadowMap)
{
    const auto caster = Quad(kCasterHeight, kCasterHalfExtent);

    device.setRasterizerStateProperty(RasterizerState::CullNone);
    device.setDepthStencilStateProperty(DepthStencilState::Default);
    device.setBlendStateProperty(BlendState::Opaque);

    device.SetVertexBuffer(nullptr);
    shadowMap.begin(Sun(), SceneBounds());
    device.DrawUserPrimitives(PrimitiveType::TriangleList, caster.data(), 0, 2);
    shadowMap.end();
}

/// Fills @p shadowMap with the caster, then renders the ground plane into @p target with
/// @p effect. Whether the effect has been told about the map is the caller's business -- that is
/// the single variable every test here changes.
void RenderScene(GraphicsDevice& device, ShadowMap& shadowMap, BasicEffect& effect,
                 RenderTarget2D& target)
{
    const auto ground = Quad(0.0f, kGroundHalfExtent);

    FillShadowMap(device, shadowMap);

    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    effect.Apply();
    device.DrawUserPrimitives(PrimitiveType::TriangleList, ground.data(), 0, 2);
    device.SetRenderTarget(nullptr);
}

/// A plain lit BasicEffect, with no shadow state of its own.
void ConfigureLighting(BasicEffect& effect)
{
    effect.setLightingEnabledProperty(true);
    effect.setTextureEnabledProperty(false);
    effect.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
    // Low enough that the difference between lit and shadowed is unmistakable, high enough that a
    // shadowed pixel is still shaded rather than black -- which is itself the property MOD-838
    // asserts, so it has to be observable.
    effect.setAmbientLightColorProperty(Vector3(0.15f, 0.15f, 0.15f));
    effect.setSpecularColorProperty(Vector3(0.0f, 0.0f, 0.0f));
    effect.setEmissiveColorProperty(Vector3(0.0f, 0.0f, 0.0f));

    auto& light = effect.getDirectionalLight0Property();
    light.setEnabledProperty(true);
    light.setDirectionProperty(Vector3(0.0f, -1.0f, 0.0f));
    light.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
    light.setSpecularColorProperty(Vector3(0.0f, 0.0f, 0.0f));
    effect.getDirectionalLight1Property().setEnabledProperty(false);
    effect.getDirectionalLight2Property().setEnabledProperty(false);

    effect.setViewProperty(TopDownView());
    effect.setProjectionProperty(FitToGround());
    effect.setWorldProperty(Matrix::getIdentityProperty());
}

class ShadowVisibilityTest : public ::testing::Test
{
protected:
    GraphicsDevice device;

    void SetUp() override
    {
        if (!device.SupportsCapability(GraphicsCapability::ThreeD))
            GTEST_SKIP() << "this renderer does not raster 3D triangles";
        // CustomEffects, not CompiledEffects: the caster is GLSL source compiled at run time by
        // ShaderEffect, which is a different facility from the Effect Framework bytecode
        // CompiledEffects names.
        if (!device.SupportsCapability(GraphicsCapability::CustomEffects))
            GTEST_SKIP() << "this renderer cannot compile the shadow caster's shader";
    }
};

TEST_F(ShadowVisibilityTest, TheCastersShadowIsVisibleOnTheGround)
{
    // MOD-835/MOD-838, and the reason the whole phase exists.
    ShadowMap shadowMap(device, ShadowQuality::Medium);
    RenderTarget2D target(device, kFrame, kFrame, false, SurfaceFormat::Color,
                          DepthFormat::Depth24);

    BasicEffect effect(device);
    ConfigureLighting(effect);
    effect.setShadowMapEXT(shadowMap.getShadowTexture());
    effect.setLightViewProjectionEXT(shadowMap.getLightViewProjection());
    effect.setShadowsEnabledEXT(true);

    RenderScene(device, shadowMap, effect, target);
    // begin() computes the light matrix, so the effect can only be told about it after a pass has
    // run. The first frame therefore carries an identity matrix; render a second one now that the
    // real matrix exists. A game does the same thing -- it sets the matrix each frame from the
    // pass it just ran.
    effect.setLightViewProjectionEXT(shadowMap.getLightViewProjection());
    RenderScene(device, shadowMap, effect, target);

    const Frame frame = Capture(target);
    const int shadowed = frame.BrightnessAt(kFrame / 2, kFrame / 2);
    const int lit      = frame.BrightnessAt(3, 3);

    EXPECT_LT(shadowed, lit)
        << "the centre of the plane, directly under the caster, is not darker than its corner: "
        << shadowed << " vs " << lit;
    EXPECT_GT(lit - shadowed, 32)
        << "there is a difference, but too small to be a shadow: " << shadowed << " vs " << lit;
}

TEST_F(ShadowVisibilityTest, AShadowedSurfaceKeepsItsAmbientLight)
{
    // MOD-838. Shadow attenuates the light that comes from the light; ambient is the light that
    // comes from everywhere, and a shadowed surface that goes black has darkened the wrong term.
    ShadowMap shadowMap(device, ShadowQuality::Medium);
    RenderTarget2D target(device, kFrame, kFrame, false, SurfaceFormat::Color,
                          DepthFormat::Depth24);

    BasicEffect effect(device);
    ConfigureLighting(effect);
    effect.setShadowMapEXT(shadowMap.getShadowTexture());
    effect.setShadowsEnabledEXT(true);

    RenderScene(device, shadowMap, effect, target);
    effect.setLightViewProjectionEXT(shadowMap.getLightViewProjection());
    RenderScene(device, shadowMap, effect, target);

    const Frame frame = Capture(target);
    const int shadowed = frame.BrightnessAt(kFrame / 2, kFrame / 2);

    EXPECT_GT(shadowed, 0) << "a fully shadowed surface went black, so ambient was attenuated too";
}

TEST_F(ShadowVisibilityTest, AnEffectWithoutAShadowMapRendersTheSameFrameAsBefore)
{
    // The isolation property. Shadow support is opt-in, and an effect that was never given a map
    // has to produce the frame it produced before any of this existed -- uniformly lit, centre
    // indistinguishable from corner.
    ShadowMap shadowMap(device, ShadowQuality::Medium);
    RenderTarget2D target(device, kFrame, kFrame, false, SurfaceFormat::Color,
                          DepthFormat::Depth24);

    BasicEffect effect(device);
    ConfigureLighting(effect);

    RenderScene(device, shadowMap, effect, target);

    const Frame frame = Capture(target);
    EXPECT_EQ(frame.BrightnessAt(kFrame / 2, kFrame / 2), frame.BrightnessAt(3, 3));
}

TEST_F(ShadowVisibilityTest, TurningShadowsOffRestoresTheUnshadowedFrame)
{
    // The same effect object, the same attached map, one flag apart. This is what a game toggling
    // a graphics setting does, and a renderer that cached the shadow state per program rather
    // than per draw would fail here while passing the test above.
    ShadowMap shadowMap(device, ShadowQuality::Medium);
    RenderTarget2D target(device, kFrame, kFrame, false, SurfaceFormat::Color,
                          DepthFormat::Depth24);

    BasicEffect effect(device);
    ConfigureLighting(effect);
    effect.setShadowMapEXT(shadowMap.getShadowTexture());
    effect.setShadowsEnabledEXT(true);

    RenderScene(device, shadowMap, effect, target);
    effect.setLightViewProjectionEXT(shadowMap.getLightViewProjection());
    RenderScene(device, shadowMap, effect, target);
    const int shadowed = Capture(target).BrightnessAt(kFrame / 2, kFrame / 2);

    effect.setShadowsEnabledEXT(false);
    RenderScene(device, shadowMap, effect, target);
    const Frame off = Capture(target);

    EXPECT_GT(off.BrightnessAt(kFrame / 2, kFrame / 2), shadowed);
    EXPECT_EQ(off.BrightnessAt(kFrame / 2, kFrame / 2), off.BrightnessAt(3, 3));
}

TEST_F(ShadowVisibilityTest, ShadowsSurviveTheDefaultPerVertexLighting)
{
    // MOD-840. XNA's BasicEffect defaults PreferPerPixelLighting to false, and a shadow evaluated
    // at four corners and interpolated across the plane is a gradient rather than a shadow. The
    // renderer therefore has to route a receiving draw through the per-pixel program regardless.
    ShadowMap shadowMap(device, ShadowQuality::Medium);
    RenderTarget2D target(device, kFrame, kFrame, false, SurfaceFormat::Color,
                          DepthFormat::Depth24);

    BasicEffect effect(device);
    ConfigureLighting(effect);
    ASSERT_FALSE(effect.getPreferPerPixelLightingProperty()) << "XNA's own default changed";
    effect.setShadowMapEXT(shadowMap.getShadowTexture());
    effect.setShadowsEnabledEXT(true);

    RenderScene(device, shadowMap, effect, target);
    effect.setLightViewProjectionEXT(shadowMap.getLightViewProjection());
    RenderScene(device, shadowMap, effect, target);

    const Frame frame = Capture(target);
    EXPECT_LT(frame.BrightnessAt(kFrame / 2, kFrame / 2), frame.BrightnessAt(3, 3));
}

// =====================================================================================
// The other three lit effects (MOD-837, MOD-838, MOD-839)
//
// Their ground plane goes through a VertexBuffer rather than a user-primitive array, because the
// stride is what selects the shader family and only the raw path can state one that has no
// built-in vertex type.
// =====================================================================================

/// The stream the skinned program declares: position, normal, uv, four weights, four indices.
struct GpuSkinnedVertex
{
    float x, y, z;
    float nx, ny, nz;
    float u, v;
    float w0, w1, w2, w3;
    std::uint8_t i0, i1, i2, i3;
};
static_assert(sizeof(GpuSkinnedVertex) == 52, "the skinned program requires a stride of 52");

/// The stream the PBR program declares: position, normal, tangent, uv.
struct GpuPbrVertex
{
    float x, y, z;
    float nx, ny, nz;
    float tx, ty, tz, tw;
    float u, v;
};
static_assert(sizeof(GpuPbrVertex) == 48, "the PBR program requires a stride of 48");

/// The stream the skinned PBR program declares: the PBR one, plus weights and indices.
struct GpuSkinnedPbrVertex
{
    float x, y, z;
    float nx, ny, nz;
    float tx, ty, tz, tw;
    float u, v;
    float w0, w1, w2, w3;
    std::uint8_t i0, i1, i2, i3;
};
static_assert(sizeof(GpuSkinnedPbrVertex) == 68,
              "the skinned PBR program requires a stride of 68");

/// The same ground plane the BasicEffect cases use, in whichever stream the caller's shader
/// family reads. Bound to a single identity bone in the skinned case, so the geometry is
/// unchanged and the only variable left is the shadow.
template <typename VertexT>
std::array<VertexT, 6> GroundStream()
{
    const auto quad = Quad(0.0f, kGroundHalfExtent);
    std::array<VertexT, 6> out{};
    for (std::size_t i = 0; i < out.size(); ++i)
    {
        const Vector3& p = quad[i].Position;
        if constexpr (std::is_same_v<VertexT, GpuSkinnedVertex>)
            out[i] = GpuSkinnedVertex{p.X, p.Y, p.Z, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
                                      1.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0};
        else if constexpr (std::is_same_v<VertexT, GpuSkinnedPbrVertex>)
            out[i] = GpuSkinnedPbrVertex{p.X, p.Y, p.Z, 0.0f, 1.0f, 0.0f,
                                         1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                                         1.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0};
        else
            out[i] = GpuPbrVertex{p.X, p.Y, p.Z, 0.0f, 1.0f, 0.0f,
                                  1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    }
    return out;
}

template <typename VertexT>
void DrawGroundFromBuffer(GraphicsDevice& device, RenderTarget2D& target, Effect& effect)
{
    const auto stream = GroundStream<VertexT>();
    VertexBuffer buffer(device, 6);
    buffer.SetDataRaw(stream.data(), 6, static_cast<int>(sizeof(VertexT)));

    device.SetVertexBuffer(&buffer);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    effect.Apply();
    device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
    device.SetRenderTarget(nullptr);
    device.SetVertexBuffer(nullptr);
}

TEST_F(ShadowVisibilityTest, SkinnedEffectReceivesTheShadow)
{
    // MOD-837. The skinned program has no ambient uniform of its own -- ambient is folded into
    // emissive before it reaches the shader -- so this is also the case that would go wrong if the
    // shadow were applied to the whole lighting sum rather than to the direct term.
    ShadowMap shadowMap(device, ShadowQuality::Medium);
    RenderTarget2D target(device, kFrame, kFrame, false, SurfaceFormat::Color,
                          DepthFormat::Depth24);

    SkinnedEffect effect(device);
    effect.SetBoneTransforms(std::vector<Matrix>(1, Matrix::getIdentityProperty()));
    effect.setWeightsPerVertexProperty(1);
    effect.setPreferPerPixelLightingProperty(true);
    effect.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
    effect.setSpecularColorProperty(Vector3(0.0f, 0.0f, 0.0f));
    effect.setAmbientLightColorProperty(Vector3(0.15f, 0.15f, 0.15f));
    auto& light = effect.getDirectionalLight0Property();
    light.setEnabledProperty(true);
    light.setDirectionProperty(Vector3(0.0f, -1.0f, 0.0f));
    light.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
    light.setSpecularColorProperty(Vector3(0.0f, 0.0f, 0.0f));
    effect.getDirectionalLight1Property().setEnabledProperty(false);
    effect.getDirectionalLight2Property().setEnabledProperty(false);
    effect.setViewProperty(TopDownView());
    effect.setProjectionProperty(FitToGround());
    effect.setWorldProperty(Matrix::getIdentityProperty());

    FillShadowMap(device, shadowMap);
    effect.setShadowMapEXT(shadowMap.getShadowTexture());
    effect.setLightViewProjectionEXT(shadowMap.getLightViewProjection());
    effect.setShadowsEnabledEXT(true);
    DrawGroundFromBuffer<GpuSkinnedVertex>(device, target, effect);

    const Frame lit = Capture(target);
    EXPECT_LT(lit.BrightnessAt(kFrame / 2, kFrame / 2), lit.BrightnessAt(3, 3));
    EXPECT_GT(lit.BrightnessAt(kFrame / 2, kFrame / 2), 0)
        << "the shadowed centre went black, so ambient was attenuated too";
}

TEST_F(ShadowVisibilityTest, PbrEffectReceivesTheShadowOnItsDirectTermOnly)
{
    // MOD-838. PBR's ambient term stands for the rest of the environment, which one occluder
    // between the surface and one light does not block.
    ShadowMap shadowMap(device, ShadowQuality::Medium);
    RenderTarget2D target(device, kFrame, kFrame, false, SurfaceFormat::Color,
                          DepthFormat::Depth24);

    PbrEffect effect(device);
    effect.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
    effect.setMetallicFactorProperty(0.0f);
    effect.setRoughnessFactorProperty(1.0f);
    effect.setAmbientLightColorProperty(Vector3(0.15f, 0.15f, 0.15f));
    auto& light = effect.getDirectionalLight0Property();
    light.setEnabledProperty(true);
    light.setDirectionProperty(Vector3(0.0f, -1.0f, 0.0f));
    light.setDiffuseColorProperty(Vector3(3.0f, 3.0f, 3.0f));
    effect.getDirectionalLight1Property().setEnabledProperty(false);
    effect.getDirectionalLight2Property().setEnabledProperty(false);
    effect.setViewProperty(TopDownView());
    effect.setProjectionProperty(FitToGround());
    effect.setWorldProperty(Matrix::getIdentityProperty());

    FillShadowMap(device, shadowMap);
    effect.setShadowMapEXT(shadowMap.getShadowTexture());
    effect.setLightViewProjectionEXT(shadowMap.getLightViewProjection());
    effect.setShadowsEnabledEXT(true);
    DrawGroundFromBuffer<GpuPbrVertex>(device, target, effect);

    const Frame frame = Capture(target);
    EXPECT_LT(frame.BrightnessAt(kFrame / 2, kFrame / 2), frame.BrightnessAt(3, 3));
    EXPECT_GT(frame.BrightnessAt(kFrame / 2, kFrame / 2), 0)
        << "the shadowed centre went black, so the ambient/IBL term was attenuated too";
}

TEST_F(ShadowVisibilityTest, SkinnedPbrEffectReceivesTheShadow)
{
    // MOD-839. The skinned PBR program is built from the same fragment source as the unskinned
    // one, so this is the case that catches the two drifting apart -- a shared source is only
    // shared until someone edits one copy.
    ShadowMap shadowMap(device, ShadowQuality::Medium);
    RenderTarget2D target(device, kFrame, kFrame, false, SurfaceFormat::Color,
                          DepthFormat::Depth24);

    SkinnedPbrEffect effect(device);
    effect.SetBoneTransforms(std::vector<Matrix>(1, Matrix::getIdentityProperty()));
    effect.setWeightsPerVertexProperty(1);
    effect.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
    effect.setMetallicFactorProperty(0.0f);
    effect.setRoughnessFactorProperty(1.0f);
    effect.setAmbientLightColorProperty(Vector3(0.15f, 0.15f, 0.15f));
    auto& light = effect.getDirectionalLight0Property();
    light.setEnabledProperty(true);
    light.setDirectionProperty(Vector3(0.0f, -1.0f, 0.0f));
    light.setDiffuseColorProperty(Vector3(3.0f, 3.0f, 3.0f));
    effect.getDirectionalLight1Property().setEnabledProperty(false);
    effect.getDirectionalLight2Property().setEnabledProperty(false);
    effect.setViewProperty(TopDownView());
    effect.setProjectionProperty(FitToGround());
    effect.setWorldProperty(Matrix::getIdentityProperty());

    FillShadowMap(device, shadowMap);
    effect.setShadowMapEXT(shadowMap.getShadowTexture());
    effect.setLightViewProjectionEXT(shadowMap.getLightViewProjection());
    effect.setShadowsEnabledEXT(true);
    DrawGroundFromBuffer<GpuSkinnedPbrVertex>(device, target, effect);

    const Frame frame = Capture(target);
    EXPECT_LT(frame.BrightnessAt(kFrame / 2, kFrame / 2), frame.BrightnessAt(3, 3));
    EXPECT_GT(frame.BrightnessAt(kFrame / 2, kFrame / 2), 0);
}

TEST_F(ShadowVisibilityTest, NothingOutsideTheLightVolumeIsShadowed)
{
    // MOD-841. The map covers only what the light was fitted to. Everything beyond it was never
    // rendered into the map, so nothing there can be occluding -- and a lookup that let the
    // sampler clamp to the edge texel instead would smear the caster's own silhouette outward as
    // four dark bands reaching the corners of the frame.
    ShadowMap shadowMap(device, ShadowQuality::Medium);
    RenderTarget2D target(device, kFrame, kFrame, false, SurfaceFormat::Color,
                          DepthFormat::Depth24);

    BasicEffect effect(device);
    ConfigureLighting(effect);
    effect.setShadowMapEXT(shadowMap.getShadowTexture());
    effect.setShadowsEnabledEXT(true);

    // A light fitted to the caster alone: most of the ground plane now falls outside the volume.
    const BoundingBox tight(Vector3(-kCasterHalfExtent, 0.0f, -kCasterHalfExtent),
                            Vector3(kCasterHalfExtent, kCasterHeight + 1.0f, kCasterHalfExtent));
    const auto caster = Quad(kCasterHeight, kCasterHalfExtent);
    const auto ground = Quad(0.0f, kGroundHalfExtent);

    for (int pass = 0; pass < 2; ++pass)
    {
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        device.setBlendStateProperty(BlendState::Opaque);

        shadowMap.begin(Sun(), tight);
        device.DrawUserPrimitives(PrimitiveType::TriangleList, caster.data(), 0, 2);
        shadowMap.end();
        effect.setLightViewProjectionEXT(shadowMap.getLightViewProjection());

        device.SetRenderTarget(&target);
        device.Clear(Color::Black);
        effect.Apply();
        device.DrawUserPrimitives(PrimitiveType::TriangleList, ground.data(), 0, 2);
        device.SetRenderTarget(nullptr);
    }

    const Frame frame = Capture(target);
    const int lit = frame.BrightnessAt(1, 1);
    EXPECT_LT(frame.BrightnessAt(kFrame / 2, kFrame / 2), lit);
    // Every pixel along the frame's own border is outside the light volume, and every one of them
    // has to be fully lit.
    for (int i = 0; i < kFrame; ++i)
    {
        EXPECT_EQ(frame.BrightnessAt(i, 0), lit) << "top edge, column " << i;
        EXPECT_EQ(frame.BrightnessAt(i, kFrame - 1), lit) << "bottom edge, column " << i;
        EXPECT_EQ(frame.BrightnessAt(0, i), lit) << "left edge, row " << i;
        EXPECT_EQ(frame.BrightnessAt(kFrame - 1, i), lit) << "right edge, row " << i;
    }
}

TEST_F(ShadowVisibilityTest, TheFilterRadiusChangesHowSoftTheEdgeIs)
{
    // MOD-840. Radius 0 is a single tap and produces a hard edge: every pixel along it is either
    // fully lit or fully shadowed. A wider kernel produces intermediate values, and counting them
    // is a more honest test than comparing two images, which would also differ if the shadow had
    // merely moved.
    ShadowMap shadowMap(device, ShadowQuality::Medium);
    RenderTarget2D target(device, kFrame, kFrame, false, SurfaceFormat::Color,
                          DepthFormat::Depth24);

    BasicEffect effect(device);
    ConfigureLighting(effect);
    effect.setShadowMapEXT(shadowMap.getShadowTexture());
    effect.setShadowsEnabledEXT(true);

    const auto countPartials = [&](int radius) {
        effect.setShadowFilterRadiusEXT(radius);
        RenderScene(device, shadowMap, effect, target);
        effect.setLightViewProjectionEXT(shadowMap.getLightViewProjection());
        RenderScene(device, shadowMap, effect, target);

        const Frame frame = Capture(target);
        const int litValue = frame.BrightnessAt(3, 3);
        const int shadowValue = frame.BrightnessAt(kFrame / 2, kFrame / 2);
        int partial = 0;
        for (int y = 0; y < kFrame; ++y)
            for (int x = 0; x < kFrame; ++x)
            {
                const int value = frame.BrightnessAt(x, y);
                if (value > shadowValue + 2 && value < litValue - 2)
                    ++partial;
            }
        return partial;
    };

    EXPECT_EQ(countPartials(0), 0) << "a single tap cannot produce a partially shadowed pixel";
    EXPECT_GT(countPartials(2), 0) << "a 5x5 kernel produced no soft edge at all";
}

/// Where the shadowed pixels are, as a centroid in frame coordinates. Returns false when nothing
/// is shadowed at all, which is a different failure from a shadow in the wrong place.
bool ShadowCentroid(const Frame& frame, float& x, float& y)
{
    const int lit = frame.BrightnessAt(0, 0);
    double sumX = 0.0, sumY = 0.0;
    int count = 0;
    for (int py = 0; py < kFrame; ++py)
        for (int px = 0; px < kFrame; ++px)
            if (frame.BrightnessAt(px, py) < lit - 16)
            {
                sumX += px;
                sumY += py;
                ++count;
            }
    if (count == 0)
        return false;
    x = static_cast<float>(sumX / count);
    y = static_cast<float>(sumY / count);
    return true;
}

/// The frame pixel a world position lands on, computed the way the rasterizer will. Independent of
/// anything under test: it uses the camera matrices only, never the light's.
void ExpectedPixel(const Vector3& world, float& x, float& y)
{
    const Matrix viewProjection = TopDownView() * FitToGround();
    const float cx = world.X * viewProjection.M11 + world.Y * viewProjection.M21
                   + world.Z * viewProjection.M31 + viewProjection.M41;
    const float cy = world.X * viewProjection.M12 + world.Y * viewProjection.M22
                   + world.Z * viewProjection.M32 + viewProjection.M42;
    const float w  = world.X * viewProjection.M14 + world.Y * viewProjection.M24
                   + world.Z * viewProjection.M34 + viewProjection.M44;
    const float inverseW = std::abs(w) > 1e-6f ? 1.0f / w : 1.0f;
    x = (cx * inverseW * 0.5f + 0.5f) * kFrame;
    // Row 0 of a readback is the top of the frame, which is where clip-space +Y lands.
    y = (0.5f - cy * inverseW * 0.5f) * kFrame;
}

TEST_F(ShadowVisibilityTest, TheShadowLandsWhereTheCasterIs)
{
    // MOD-842, and the case the centred scene above structurally cannot fail: with the caster over
    // the middle of the frame, a shadow lookup that flipped V, or swapped the two texture axes,
    // produces exactly the same square. Real shadow-mapping code gets both of those wrong at least
    // once -- the XNA sample this renderer's own easygl_shadowmapping example ports has an explicit
    // `ShadowTexCoord.y = 1 - y` in it, correct there and wrong here, because a CNA render target's
    // texel memory already matches the clip space it was rendered in.
    //
    // So the caster is moved off centre along each world axis in turn and the shadow's centroid is
    // compared against where the camera alone says that point lands.
    ShadowMap shadowMap(device, ShadowQuality::Medium);
    RenderTarget2D target(device, kFrame, kFrame, false, SurfaceFormat::Color,
                          DepthFormat::Depth24);

    BasicEffect effect(device);
    ConfigureLighting(effect);
    effect.setShadowMapEXT(shadowMap.getShadowTexture());
    effect.setShadowsEnabledEXT(true);

    const auto ground = Quad(0.0f, kGroundHalfExtent);

    struct Offset { float dx, dz; const char* name; };
    const Offset offsets[] = {
        {5.0f, 0.0f, "+X"}, {-5.0f, 0.0f, "-X"}, {0.0f, 5.0f, "+Z"}, {0.0f, -5.0f, "-Z"},
    };

    for (const Offset& offset : offsets)
    {
        auto caster = Quad(kCasterHeight, kCasterHalfExtent);
        for (auto& vertex : caster)
        {
            vertex.Position.X += offset.dx;
            vertex.Position.Z += offset.dz;
        }

        // Two passes: begin() computes the light matrix, so the first frame is the one that
        // produces it and the second is the one that uses it.
        for (int pass = 0; pass < 2; ++pass)
        {
            device.setRasterizerStateProperty(RasterizerState::CullNone);
            device.setDepthStencilStateProperty(DepthStencilState::Default);
            device.setBlendStateProperty(BlendState::Opaque);

            shadowMap.begin(Sun(), SceneBounds());
            device.DrawUserPrimitives(PrimitiveType::TriangleList, caster.data(), 0, 2);
            shadowMap.end();
            effect.setLightViewProjectionEXT(shadowMap.getLightViewProjection());

            device.SetRenderTarget(&target);
            device.Clear(Color::Black);
            effect.Apply();
            device.DrawUserPrimitives(PrimitiveType::TriangleList, ground.data(), 0, 2);
            device.SetRenderTarget(nullptr);
        }

        float gotX = 0.0f, gotY = 0.0f;
        ASSERT_TRUE(ShadowCentroid(Capture(target), gotX, gotY))
            << "no shadow at all with the caster at " << offset.name;

        float wantX = 0.0f, wantY = 0.0f;
        ExpectedPixel(Vector3(offset.dx, 0.0f, offset.dz), wantX, wantY);

        // Two pixels of slack for the PCF skirt and the map's own resolution; a flipped or
        // swapped axis is off by tens of pixels, not by two.
        EXPECT_NEAR(gotX, wantX, 2.0f) << "caster at " << offset.name << ": shadow column";
        EXPECT_NEAR(gotY, wantY, 2.0f) << "caster at " << offset.name << ": shadow row";
    }
}

TEST_F(ShadowVisibilityTest, AnAttachedButDisabledMapChangesNoPixel)
{
    // MOD-853. Not "looks the same" -- every one of the 4096 pixels, because the interesting
    // failure is a lookup that runs anyway and returns something very close to 1.0.
    ShadowMap shadowMap(device, ShadowQuality::Medium);
    RenderTarget2D target(device, kFrame, kFrame, false, SurfaceFormat::Color,
                          DepthFormat::Depth24);

    BasicEffect untouched(device);
    ConfigureLighting(untouched);
    RenderScene(device, shadowMap, untouched, target);
    const Frame before = Capture(target);

    BasicEffect disabled(device);
    ConfigureLighting(disabled);
    disabled.setShadowMapEXT(shadowMap.getShadowTexture());
    disabled.setLightViewProjectionEXT(shadowMap.getLightViewProjection());
    disabled.setShadowsEnabledEXT(false);
    RenderScene(device, shadowMap, disabled, target);
    const Frame after = Capture(target);

    int differing = 0;
    for (int y = 0; y < kFrame; ++y)
        for (int x = 0; x < kFrame; ++x)
            if (!(before.At(x, y) == after.At(x, y)))
                ++differing;

    EXPECT_EQ(differing, 0) << differing << " pixels changed with shadows merely disabled";
}

/// Renders the acne scene: a sun at 45 degrees, and a ground plane that is drawn into the shadow
/// map as well as shaded from it, which is what makes a surface able to shadow itself. Returns the
/// fraction of the ground that came out darker than fully lit.
float SelfShadowedFraction(GraphicsDevice& device, ShadowMap& shadowMap, RenderTarget2D& target,
                           BasicEffect& effect, float bias)
{
    DirectionalLightEXT slanted;
    slanted.Direction = Vector3(-0.7071f, -0.7071f, 0.0f);

    effect.setShadowDepthBiasEXT(bias);
    const auto ground = Quad(0.0f, kGroundHalfExtent);
    const auto caster = Quad(kCasterHeight, kCasterHalfExtent);

    for (int pass = 0; pass < 2; ++pass)
    {
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        device.setBlendStateProperty(BlendState::Opaque);

        shadowMap.begin(slanted, SceneBounds());
        device.DrawUserPrimitives(PrimitiveType::TriangleList, ground.data(), 0, 2);
        device.DrawUserPrimitives(PrimitiveType::TriangleList, caster.data(), 0, 2);
        shadowMap.end();
        effect.setLightViewProjectionEXT(shadowMap.getLightViewProjection());

        device.SetRenderTarget(&target);
        device.Clear(Color::Black);
        effect.Apply();
        device.DrawUserPrimitives(PrimitiveType::TriangleList, ground.data(), 0, 2);
        device.SetRenderTarget(nullptr);
    }

    const Frame frame = Capture(target);
    int darkened = 0;
    for (int y = 0; y < kFrame; ++y)
        for (int x = 0; x < kFrame; ++x)
            if (frame.BrightnessAt(x, y) < 250)
                ++darkened;
    return static_cast<float>(darkened) / static_cast<float>(kFrame * kFrame);
}

TEST_F(ShadowVisibilityTest, TheDefaultBiasSitsBetweenAcneAndPeterPanning)
{
    // MOD-855, as measurements rather than a committed image pair -- a golden PNG of acne records
    // that it looked wrong on the machine that made it, while a fraction records how wrong and
    // lets the threshold be argued with. Deviation from the plan's "golden pair", recorded there.
    //
    // The scene is the one that can go wrong in both directions: a slanted sun, and a ground plane
    // that is written into the map as well as read from it, so the plane's own recorded depth sits
    // a fraction of a texel away from the depth each of its pixels computes.
    ShadowMap shadowMap(device, ShadowQuality::Medium);
    RenderTarget2D target(device, kFrame, kFrame, false, SurfaceFormat::Color,
                          DepthFormat::Depth24);

    BasicEffect effect(device);
    ConfigureLighting(effect);
    effect.setShadowMapEXT(shadowMap.getShadowTexture());
    effect.setShadowsEnabledEXT(true);
    effect.setShadowFilterRadiusEXT(0);   // one tap, so the number is acne and not a soft edge

    const float withoutBias = SelfShadowedFraction(device, shadowMap, target, effect, 0.0f);
    const float withDefault = SelfShadowedFraction(device, shadowMap, target, effect, 0.0015f);
    const float withTooMuch = SelfShadowedFraction(device, shadowMap, target, effect, 0.2f);

    // Printed because the numbers are the evidence this row asks for, and a passing test that
    // hides them justifies the default no better than taste would.
    std::cout << "[  SHADOW  ] self-shadowed area -- bias 0: " << withoutBias
              << ", default 0.0015: " << withDefault
              << ", 0.2: " << withTooMuch << std::endl;

    // Zero bias: the plane shadows itself over a large part of its own area. That is acne.
    EXPECT_GT(withoutBias, 0.2f) << "no acne at bias 0, so this scene no longer demonstrates it";
    // The default: the caster's shadow survives, the self-shadowing does not.
    EXPECT_LT(withDefault, withoutBias * 0.5f)
        << "the default bias did not remove most of the acne";
    EXPECT_GT(withDefault, 0.02f) << "the caster's own shadow disappeared along with the acne";
    // Far too much bias: the shadow detaches from its caster and shrinks away. That is
    // peter-panning, and it is the cost of treating a larger bias as free.
    EXPECT_LT(withTooMuch, withDefault)
        << "a 130x bias did not shrink the shadow, so the trade-off this default sits on is not "
           "the one documented";
}

TEST_F(ShadowVisibilityTest, ASkinnedCasterCastsThePoseItIsIn)
{
    // MOD-810. Drawn with the rigid caster a skinned mesh records its bind pose, which is the
    // shadow of a character standing still under one that is running -- and it looks like a
    // correct shadow, just of the wrong thing. So the bone here is a pure translation, far enough
    // that the bind-pose shadow and the posed one cannot be confused, and the test asks which of
    // the two positions the shadow actually landed in.
    ShadowMap shadowMap(device, ShadowQuality::Medium);
    if (shadowMap.getSkinnedCasterEffect() == nullptr)
        GTEST_SKIP() << "this renderer cannot compile the skinned caster";

    RenderTarget2D target(device, kFrame, kFrame, false, SurfaceFormat::Color,
                          DepthFormat::Depth24);

    constexpr float kBoneOffsetX = 4.0f;
    std::vector<Matrix> bones{Matrix::CreateTranslation(kBoneOffsetX, 0.0f, 0.0f)};

    const auto quad = Quad(kCasterHeight, kCasterHalfExtent);
    std::array<GpuSkinnedVertex, 6> caster{};
    for (std::size_t i = 0; i < caster.size(); ++i)
    {
        const Vector3& p = quad[i].Position;
        caster[i] = GpuSkinnedVertex{p.X, p.Y, p.Z, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
                                     1.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0};
    }

    BasicEffect effect(device);
    ConfigureLighting(effect);
    effect.setShadowMapEXT(shadowMap.getShadowTexture());
    effect.setShadowsEnabledEXT(true);

    const auto ground = Quad(0.0f, kGroundHalfExtent);
    VertexBuffer casterBuffer(device, 6);
    casterBuffer.SetDataRaw(caster.data(), 6, static_cast<int>(sizeof(GpuSkinnedVertex)));

    for (int pass = 0; pass < 2; ++pass)
    {
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        device.setBlendStateProperty(BlendState::Opaque);

        shadowMap.begin(Sun(), SceneBounds());
        shadowMap.applySkinnedCaster(bones, 1);
        device.SetVertexBuffer(&casterBuffer);
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        device.SetVertexBuffer(nullptr);
        shadowMap.end();
        effect.setLightViewProjectionEXT(shadowMap.getLightViewProjection());

        device.SetRenderTarget(&target);
        device.Clear(Color::Black);
        effect.Apply();
        device.DrawUserPrimitives(PrimitiveType::TriangleList, ground.data(), 0, 2);
        device.SetRenderTarget(nullptr);
    }

    float gotX = 0.0f, gotY = 0.0f;
    ASSERT_TRUE(ShadowCentroid(Capture(target), gotX, gotY))
        << "the skinned caster left no shadow at all";

    float posedX = 0.0f, posedY = 0.0f;
    ExpectedPixel(Vector3(kBoneOffsetX, 0.0f, 0.0f), posedX, posedY);
    float bindPoseX = 0.0f, bindPoseY = 0.0f;
    ExpectedPixel(Vector3(0.0f, 0.0f, 0.0f), bindPoseX, bindPoseY);

    EXPECT_NEAR(gotX, posedX, 2.0f) << "the shadow is at the bind pose (" << bindPoseX
                                    << ") rather than the posed position (" << posedX << ")";
    EXPECT_NEAR(gotY, posedY, 2.0f);
}

TEST_F(ShadowVisibilityTest, TheSkinnedCasterRejectsAPaletteItCannotUse)
{
    ShadowMap shadowMap(device, ShadowQuality::Low);
    const std::vector<Matrix> one{Matrix::getIdentityProperty()};

    // Outside a pass there is nothing to apply to, and silently doing nothing would leave a game
    // drawing casters with whatever effect happened to be current.
    EXPECT_THROW(shadowMap.applySkinnedCaster(one, 1), std::logic_error);
    EXPECT_THROW(shadowMap.applyCaster(), std::logic_error);

    shadowMap.begin(Sun(), SceneBounds());
    // XNA's own range. 3 is not a typo a shader can absorb -- it would silently blend two weights.
    EXPECT_THROW(shadowMap.applySkinnedCaster(one, 3), std::invalid_argument);
    EXPECT_THROW(shadowMap.applySkinnedCaster({}, 1), std::invalid_argument);
    EXPECT_THROW(shadowMap.applySkinnedCaster(std::vector<Matrix>(73), 1), std::invalid_argument);
    EXPECT_NO_THROW(shadowMap.applySkinnedCaster(one, 4));
    EXPECT_NO_THROW(shadowMap.applyCaster());
    shadowMap.end();
}

TEST_F(ShadowVisibilityTest, TheQualityToRadiusTableIsWhatTheEffectIsGiven)
{
    // The mapping lives on ShadowMap rather than in a renderer so two renderers reading the same
    // quality reach the same kernel.
    EXPECT_EQ(ShadowMap::filterRadiusForQuality(ShadowQuality::Disabled), 0);
    EXPECT_EQ(ShadowMap::filterRadiusForQuality(ShadowQuality::Low), 0);
    EXPECT_EQ(ShadowMap::filterRadiusForQuality(ShadowQuality::Medium), 1);
    EXPECT_EQ(ShadowMap::filterRadiusForQuality(ShadowQuality::High), 2);
    EXPECT_EQ(ShadowMap::filterRadiusForQuality(ShadowQuality::Ultra), 2);

    ShadowMap high(device, ShadowQuality::High);
    EXPECT_EQ(high.getFilterRadius(), 2);
}

} // namespace

#endif // CNA_CNAEXT
