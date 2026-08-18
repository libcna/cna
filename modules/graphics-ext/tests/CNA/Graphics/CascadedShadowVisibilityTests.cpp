// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-905, MOD-906, MOD-908..MOD-910: cascades that are actually sampled.
//
// CascadedShadowMapTests pins the arithmetic; this pins the half that arithmetic cannot reach.
// Every one of these renders, because the failures worth catching here all look like correct code:
// a cascade matrix uploaded to the wrong slot, a fragment reading the neighbouring cascade's
// texels at an atlas seam, or a receiver that quietly keeps using the single-map path because the
// cascade count never made it into GpuDrawParams. None of those throw, and all of them produce a
// frame.
//
// The scene is a long corridor of ground stretching away from the camera with a caster hanging
// over it, so that different parts of the same surface fall in different cascades -- which is the
// only arrangement where "the right cascade was chosen" means anything at all.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/CascadedShadowMap.hpp"
#include "CNA/Graphics/DirectionalLightEXT.hpp"
#include "CNA/Graphics/ShadowQuality.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
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
#include "Microsoft/Xna/Framework/Graphics/ShadowCascadeStateEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace {

using CNA::GraphicsCapability;
using CNA::Graphics::CascadedShadowMap;
using CNA::Graphics::DirectionalLightEXT;
using CNA::Graphics::ShadowQuality;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::MathHelper;
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
using Microsoft::Xna::Framework::Graphics::ShadowCascadeStateEXT;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::VertexPositionNormalTexture;

constexpr int   kFrame       = 64;
constexpr float kGroundHalf  = 30.0f;
constexpr float kCasterHalf  = 6.0f;
constexpr float kCasterHigh  = 4.0f;
constexpr float kNear        = 1.0f;
constexpr float kFar         = 80.0f;

std::array<VertexPositionNormalTexture, 6> Quad(float y, float halfExtent)
{
    const Vector3 up(0.0f, 1.0f, 0.0f);
    const float e = halfExtent;
    const auto vertex = [&](float x, float z) {
        return VertexPositionNormalTexture(Vector3(x, y, z), up, Vector2(0.0f, 0.0f));
    };
    return {vertex(-e, -e), vertex(e, -e), vertex(e, e),
            vertex(-e, -e), vertex(e, e),  vertex(-e, e)};
}

/// Straight down, so a shadow lands directly under its caster and the geometry stays readable.
DirectionalLightEXT Sun()
{
    DirectionalLightEXT sun;
    sun.Direction = Vector3(0.0f, -1.0f, 0.0f);
    return sun;
}

/// Overhead, looking straight down. Not a corridor view -- an overhead one, because the cascades
/// are still selected by *view depth*, and a camera looking down at a plane gives every pixel of
/// that plane nearly the same depth, which would put the whole surface in one cascade and prove
/// nothing. So the camera is tilted instead: near ground at the bottom of the frame, far ground at
/// the top, and the split runs across the middle.
Matrix TiltedView()
{
    return Matrix::CreateLookAt(Vector3(0.0f, 18.0f, 34.0f), Vector3(0.0f, 0.0f, -6.0f),
                                Vector3(0.0f, 1.0f, 0.0f));
}

Matrix Projection()
{
    return Matrix::CreatePerspectiveFieldOfView(MathHelper::PiOver4, 1.0f, kNear, kFar);
}

struct Frame
{
    std::vector<Color> pixels;

    [[nodiscard]] Color At(int x, int y) const
    {
        return pixels[static_cast<std::size_t>(y) * kFrame + static_cast<std::size_t>(x)];
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

void ConfigureLighting(BasicEffect& effect)
{
    effect.setLightingEnabledProperty(true);
    effect.setTextureEnabledProperty(false);
    effect.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
    effect.setAmbientLightColorProperty(Vector3(0.15f, 0.15f, 0.15f));
    effect.setSpecularColorProperty(Vector3::Zero);
    effect.setEmissiveColorProperty(Vector3::Zero);

    auto& light = effect.getDirectionalLight0Property();
    light.setEnabledProperty(true);
    light.setDirectionProperty(Vector3(0.0f, -1.0f, 0.0f));
    light.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
    light.setSpecularColorProperty(Vector3::Zero);
    effect.getDirectionalLight1Property().setEnabledProperty(false);
    effect.getDirectionalLight2Property().setEnabledProperty(false);

    effect.setWorldProperty(Matrix::getIdentityProperty());
    effect.setViewProperty(TiltedView());
    effect.setProjectionProperty(Projection());
}

/// Renders one frame: every cascade filled with the caster, then the ground shaded from the atlas.
void RenderScene(GraphicsDevice& device, CascadedShadowMap& cascades, BasicEffect& effect,
                 RenderTarget2D& target)
{
    const auto caster = Quad(kCasterHigh, kCasterHalf);
    const auto ground = Quad(0.0f, kGroundHalf);

    device.setRasterizerStateProperty(RasterizerState::CullNone);
    device.setDepthStencilStateProperty(DepthStencilState::Default);
    device.setBlendStateProperty(BlendState::Opaque);

    cascades.update(Sun(), TiltedView(), Projection());
    for (int i = 0; i < cascades.getCascadeCount(); ++i)
    {
        cascades.begin(i);
        device.DrawUserPrimitives(PrimitiveType::TriangleList, caster.data(), 0, 2);
        cascades.end();
    }

    cascades.applyToReceiver(effect);
    effect.setShadowsEnabledEXT(true);

    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    effect.Apply();
    device.DrawUserPrimitives(PrimitiveType::TriangleList, ground.data(), 0, 2);
    device.SetRenderTarget(nullptr);
}

/// Ground pixels that came out darker than the lit ground. The tilted camera sees sky above the
/// horizon, and the frame is cleared to black, so "darker than the brightest pixel" on its own
/// would count the background as one enormous shadow. A shadowed ground pixel still carries the
/// ambient term and is never pure black, which is exactly what separates the two.
int ShadowedPixelCount(const Frame& frame)
{
    int brightest = 0;
    for (const Color& pixel : frame.pixels)
        brightest = std::max(brightest, static_cast<int>(pixel.getRProperty()));

    int count = 0;
    for (const Color& pixel : frame.pixels)
    {
        const int value = static_cast<int>(pixel.getRProperty());
        if (value > 0 && value < brightest - 24)
            ++count;
    }
    return count;
}

/// Ground pixels of any brightness -- everything the plane covers.
int GroundPixelCount(const Frame& frame)
{
    int count = 0;
    for (const Color& pixel : frame.pixels)
        if (pixel.getRProperty() > 0)
            ++count;
    return count;
}

class CascadedShadowVisibilityTest : public ::testing::Test
{
protected:
    GraphicsDevice device;

    void SetUp() override
    {
        if (!device.SupportsCapability(GraphicsCapability::ThreeD))
            GTEST_SKIP() << "this renderer does not raster 3D triangles";
        if (!device.SupportsCapability(GraphicsCapability::CustomEffects))
            GTEST_SKIP() << "this renderer cannot compile the cascade caster's shader";
        // MOD-1699: compiling the caster's shader is not the same promise as SAMPLING the shadow.
        // The Vulkan renderer answers true to the first (its ShaderEffect exists) and false to the
        // second (its lit shaders ignore the state), so without this the cascade case did not skip
        // there -- it failed, describing a feature that renderer never claimed to have.
        if (!device.SupportsShadowSamplingEXT())
            GTEST_SKIP() << "this renderer's lit shaders do not sample shadow maps";
    }
};

TEST_F(CascadedShadowVisibilityTest, TheCasterIsVisibleThroughTheCascadeAtlas)
{
    // MOD-908/909: the whole seam, from the atlas the cascades render into to the shader that
    // samples it. The single-map path is untouched by this, so nothing else in the suite covers it.
    CascadedShadowMap cascades(device, ShadowQuality::Medium, 3);
    RenderTarget2D target(device, kFrame, kFrame, false, SurfaceFormat::Color,
                          DepthFormat::Depth24);

    BasicEffect effect(device);
    ConfigureLighting(effect);

    RenderScene(device, cascades, effect, target);
    const Frame frame = Capture(target);

    const int ground   = GroundPixelCount(frame);
    const int shadowed = ShadowedPixelCount(frame);
    ASSERT_GT(ground, kFrame * kFrame / 8) << "the ground plane barely reached the frame";
    EXPECT_GT(shadowed, 20) << "the cascade atlas produced no shadow on the ground at all";
    EXPECT_LT(shadowed, ground / 2)
        << "most of the ground is dark, which is what a lookup landing outside every cascade's "
           "slice would produce -- a shadow everywhere rather than under the caster ("
        << shadowed << " of " << ground << ")";
}

TEST_F(CascadedShadowVisibilityTest, EveryCascadeCountProducesTheSameShadow)
{
    // The number of cascades is a quality knob, not a visual one: two and four must put the shadow
    // in the same place. Splitting differently and landing somewhere else means a cascade matrix
    // reached the wrong slot, which one count alone cannot show.
    RenderTarget2D target(device, kFrame, kFrame, false, SurfaceFormat::Color,
                          DepthFormat::Depth24);
    std::array<int, 3> shadowed{};

    for (int count = 2; count <= 4; ++count)
    {
        CascadedShadowMap cascades(device, ShadowQuality::Medium, count);
        BasicEffect effect(device);
        ConfigureLighting(effect);
        RenderScene(device, cascades, effect, target);
        shadowed[static_cast<std::size_t>(count - 2)] = ShadowedPixelCount(Capture(target));
    }

    for (const int count : shadowed)
        EXPECT_GT(count, 20);
    // Within a fifth of each other: different splits genuinely give slightly different edges, but
    // not a different shadow.
    const int smallest = *std::min_element(shadowed.begin(), shadowed.end());
    const int largest  = *std::max_element(shadowed.begin(), shadowed.end());
    EXPECT_LE(largest - smallest, largest / 5)
        << "2/3/4 cascades disagree about the shadow: " << shadowed[0] << ", " << shadowed[1]
        << ", " << shadowed[2];
}

TEST_F(CascadedShadowVisibilityTest, ACascadeSetWithCountZeroIsTheSingleMapPath)
{
    // The property that keeps this addition free for everyone not using it: an effect handed an
    // empty cascade set must fill GpuDrawParams exactly as it did before cascades existed.
    BasicEffect effect(device);
    ConfigureLighting(effect);

    ShadowCascadeStateEXT none;
    effect.setShadowCascadesEXT(none);

    CNA::Internal::Renderers::GpuDrawParams params;
    effect.FillGpuDrawParams(params);
    EXPECT_EQ(params.cascadeCount, 0);
    EXPECT_EQ(effect.getShadowCascadesEXT().Count, 0);
}

TEST_F(CascadedShadowVisibilityTest, TheCascadeStateReachesGpuDrawParamsIntact)
{
    CascadedShadowMap cascades(device, ShadowQuality::Low, 3);
    cascades.setBlendBand(2.5f);
    cascades.setDebugTintEnabled(true);
    cascades.update(Sun(), TiltedView(), Projection());

    BasicEffect effect(device);
    ConfigureLighting(effect);
    cascades.applyToReceiver(effect);
    effect.setShadowsEnabledEXT(true);

    CNA::Internal::Renderers::GpuDrawParams params;
    effect.FillGpuDrawParams(params);

    ASSERT_EQ(params.cascadeCount, 3);
    EXPECT_FLOAT_EQ(params.cascadeBlendBand, 2.5f);
    EXPECT_TRUE(params.cascadeDebugTint);
    for (int i = 0; i < 3; ++i)
    {
        EXPECT_FLOAT_EQ(params.cascadeSplits[i], cascades.getSplitDistance(i)) << "split " << i;
        // Each cascade's matrix must land in its own 16-float slot; a shifted copy is the classic
        // way for cascade 1 to sample cascade 0's slice and look almost right.
        EXPECT_FLOAT_EQ(params.cascadeMatricesColMajor[i * 16],
                        cascades.getCascadeMatrix(i).M11) << "matrix " << i;
    }
    // The view matrix's third column, which is how the shader recovers a fragment's view depth.
    EXPECT_FLOAT_EQ(params.cascadeViewZRow[0], TiltedView().M13);
    EXPECT_FLOAT_EQ(params.cascadeViewZRow[3], TiltedView().M43);
}

TEST_F(CascadedShadowVisibilityTest, TheBlendBandChangesTheSeamAndNothingElse)
{
    // MOD-906. A cross-fade widens the transition, so it can only add partially-shadowed pixels --
    // it must not move the shadow or change how much of the ground is fully in it.
    RenderTarget2D target(device, kFrame, kFrame, false, SurfaceFormat::Color,
                          DepthFormat::Depth24);

    CascadedShadowMap hard(device, ShadowQuality::Medium, 3);
    BasicEffect hardEffect(device);
    ConfigureLighting(hardEffect);
    RenderScene(device, hard, hardEffect, target);
    const int withoutBlend = ShadowedPixelCount(Capture(target));

    CascadedShadowMap soft(device, ShadowQuality::Medium, 3);
    soft.setBlendBand(6.0f);
    BasicEffect softEffect(device);
    ConfigureLighting(softEffect);
    RenderScene(device, soft, softEffect, target);
    const int withBlend = ShadowedPixelCount(Capture(target));

    EXPECT_GT(withBlend, 0);
    EXPECT_LE(std::abs(withBlend - withoutBlend), std::max(withoutBlend / 4, 4))
        << "the blend band moved the shadow rather than softening a seam: " << withoutBlend
        << " -> " << withBlend;
}

TEST_F(CascadedShadowVisibilityTest, TheDebugTintColoursTheFrameAndIsOffByDefault)
{
    // MOD-910. A tint is a debugging aid, so the thing worth asserting is that it is *visible* when
    // asked for and completely absent when not -- a tint that leaked into ordinary rendering would
    // be a colour bug nobody would think to look for here.
    RenderTarget2D target(device, kFrame, kFrame, false, SurfaceFormat::Color,
                          DepthFormat::Depth24);

    CascadedShadowMap plain(device, ShadowQuality::Medium, 3);
    BasicEffect plainEffect(device);
    ConfigureLighting(plainEffect);
    RenderScene(device, plain, plainEffect, target);
    const Frame untinted = Capture(target);

    CascadedShadowMap tinted(device, ShadowQuality::Medium, 3);
    tinted.setDebugTintEnabled(true);
    BasicEffect tintedEffect(device);
    ConfigureLighting(tintedEffect);
    RenderScene(device, tinted, tintedEffect, target);
    const Frame withTint = Capture(target);

    // Off: the light and the surface are both white, so every lit pixel is grey.
    int untintedColoured = 0;
    int tintedColoured   = 0;
    for (int y = 0; y < kFrame; ++y)
        for (int x = 0; x < kFrame; ++x)
        {
            const Color a = untinted.At(x, y);
            const Color b = withTint.At(x, y);
            if (a.getRProperty() != a.getGProperty() || a.getGProperty() != a.getBProperty())
                ++untintedColoured;
            if (b.getRProperty() != b.getGProperty() || b.getGProperty() != b.getBProperty())
                ++tintedColoured;
        }

    EXPECT_EQ(untintedColoured, 0) << "the debug tint leaked into an ordinary render";
    EXPECT_GT(tintedColoured, kFrame * kFrame / 4) << "the debug tint coloured almost nothing";
}

TEST_F(CascadedShadowVisibilityTest, ApplyingBeforeUpdateIsRejected)
{
    CascadedShadowMap cascades(device, ShadowQuality::Low, 2);
    BasicEffect effect(device);
    // Handing over identity matrices and zero splits would put every fragment in cascade 0 and
    // shadow the whole world; refusing says what actually went wrong.
    EXPECT_THROW(cascades.applyToReceiver(effect), std::logic_error);
}


} // namespace

#endif // CNA_CNAEXT
