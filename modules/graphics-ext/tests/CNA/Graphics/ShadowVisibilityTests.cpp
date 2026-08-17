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
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

#include <array>
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
using Microsoft::Xna::Framework::Graphics::BasicEffect;
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

/// Fills @p shadowMap with the caster, then renders the ground plane into @p target with
/// @p effect. Whether the effect has been told about the map is the caller's business -- that is
/// the single variable every test here changes.
void RenderScene(GraphicsDevice& device, ShadowMap& shadowMap, BasicEffect& effect,
                 RenderTarget2D& target)
{
    const auto caster = Quad(kCasterHeight, kCasterHalfExtent);
    const auto ground = Quad(0.0f, kGroundHalfExtent);

    device.setRasterizerStateProperty(RasterizerState::CullNone);
    device.setDepthStencilStateProperty(DepthStencilState::Default);
    device.setBlendStateProperty(BlendState::Opaque);

    shadowMap.begin(Sun(), SceneBounds());
    device.DrawUserPrimitives(PrimitiveType::TriangleList, caster.data(), 0, 2);
    shadowMap.end();

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

} // namespace

#endif // CNA_CNAEXT
