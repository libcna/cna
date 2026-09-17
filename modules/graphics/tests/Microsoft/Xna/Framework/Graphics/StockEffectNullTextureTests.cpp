// SPDX-License-Identifier: MS-PL
// plans/plan_graphics_shared_cleanup.md GSC-0004: what a classic stock effect samples from an unbound
// texture slot.
//
// Measured on Microsoft XNA 4.0 (HiDef) through tools/xna-oracle with the scenes in
// tools/xna-oracle/scenes/null-texture/; the references are tools/xna-oracle/reference/null-texture/.
// Every classic stock effect reads an unbound texture as opaque black (0,0,0,255) -- BasicEffect with
// TextureEnabled, SkinnedEffect, AlphaTestEffect and both EnvironmentMapEffect slots here, both
// DualTextureEffect slots in DualTextureEffectNullSamplerTest. The expected values below are those
// references' centre pixels. A white fallback (GLTF-386's, which DirectX11 and DirectX12 carried for
// SkinnedEffect and BasicEffect) and a null view (transparent black, DirectX11's EnvironmentMapEffect)
// both fail them.
//
// Each case first draws with a real non-black texture, so a stale binding cannot pass for the rule.

#include <gtest/gtest.h>

#include <array>
#include <cstdlib>
#include <optional>
#include <vector>

#include "CNA/RendererTestGate.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/IEffectLights.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTextureSkinned.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

using namespace CNA::Testing::Renderers;
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 8;
    const Color kOpaqueBlack(0, 0, 0, 255);
    const Color kRed(255, 0, 0, 255);

    class StockEffectNullTextureTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            if (!CNA_RENDERER_IS(Software, OpenGL33, OpenGLES3, DirectX11, DirectX12))
                GTEST_SKIP() << "needs a stock-effect raster path whose missing slots are pinned";

            PresentationParameters parameters;
            parameters.setBackBufferWidthProperty(kSize);
            parameters.setBackBufferHeightProperty(kSize);
            parameters.setDepthStencilFormatProperty(DepthFormat::None);
            device_.emplace(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                            parameters);
            red_.emplace(*device_, 1, 1, false, SurfaceFormat::Color);
            red_->SetData(&kRed, 1);
        }

        GraphicsDevice& Device() { return *device_; }
        Texture2D& RedTexture() { return *red_; }

        // Clears, runs `draw` (which applies its effect) over the whole target, and returns the centre.
        template <typename Draw>
        Color Render(Draw&& draw)
        {
            GraphicsDevice& device = *device_;
            device.setRasterizerStateProperty(RasterizerState::CullNone);
            device.setDepthStencilStateProperty(DepthStencilState::None);
            device.setBlendStateProperty(BlendState::Opaque);
            device.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
            device.getSamplerStatesProperty()[1] = SamplerState::PointClamp;
            device.Clear(Color(7, 199, 53, 255));
            draw();
            Color pixel = Color::Transparent;
            const Rectangle centre(kSize / 2, kSize / 2, 1, 1);
            device.GetBackBufferData(&centre, &pixel, 0, 1);
            return pixel;
        }

        template <typename Vertex>
        void DrawQuad(const std::array<Vertex, 6>& vertices)
        {
            device_->DrawUserPrimitives(PrimitiveType::TriangleList, vertices.data(), 0, 2);
        }

        // Skinned vertices go through a vertex buffer bound before Apply, the route SkinnedEffect's
        // contract tests use.
        void DrawSkinnedQuad(Effect& effect)
        {
            const std::array<VertexPositionNormalTextureSkinned, 6> vertices = SkinnedQuad();
            VertexBuffer buffer(*device_, VertexPositionNormalTextureSkinned::getVertexDeclarationStatic(), 6,
                                BufferUsage::None);
            buffer.SetData(vertices.data(), 6);
            device_->SetVertexBuffer(&buffer);
            effect.Apply();
            device_->DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            device_->SetVertexBuffer(nullptr);
        }

        static std::array<VertexPositionTexture, 6> TexturedQuad()
        {
            const auto v = [](float x, float y, float u, float t) {
                return VertexPositionTexture(Vector3(x, y, 0.0f), Vector2(u, t));
            };
            return {v(-1, 1, 0, 0), v(1, 1, 1, 0), v(-1, -1, 0, 1),
                    v(1, 1, 1, 0), v(1, -1, 1, 1), v(-1, -1, 0, 1)};
        }

        static std::array<VertexPositionNormalTexture, 6> NormalQuad()
        {
            const auto v = [](float x, float y, float u, float t) {
                return VertexPositionNormalTexture(Vector3(x, y, 0.0f), Vector3(0.0f, 0.0f, 1.0f),
                                                   Vector2(u, t));
            };
            return {v(-1, 1, 0, 0), v(1, 1, 1, 0), v(-1, -1, 0, 1),
                    v(1, 1, 1, 0), v(1, -1, 1, 1), v(-1, -1, 0, 1)};
        }

        static std::array<VertexPositionNormalTextureSkinned, 6> SkinnedQuad()
        {
            const auto v = [](float x, float y, float u, float t) {
                return VertexPositionNormalTextureSkinned(
                    Vector3(x, y, 0.0f), Vector3(0.0f, 0.0f, 1.0f), Vector2(u, t),
                    Vector4(1.0f, 0.0f, 0.0f, 0.0f), {0, 0, 0, 0});
            };
            return {v(-1, 1, 0, 0), v(1, 1, 1, 0), v(-1, -1, 0, 1),
                    v(1, 1, 1, 0), v(1, -1, 1, 1), v(-1, -1, 0, 1)};
        }

        static void AmbientOnly(IEffectLights& effect)
        {
            effect.setAmbientLightColorProperty(Vector3(1.0f, 1.0f, 1.0f));
            effect.getDirectionalLight0Property().setEnabledProperty(false);
            effect.getDirectionalLight1Property().setEnabledProperty(false);
            effect.getDirectionalLight2Property().setEnabledProperty(false);
        }

        static void ExpectNear(const Color& actual, const Color& expected, const int tolerance,
                               const char* what)
        {
            EXPECT_LE(std::abs(actual.getRProperty() - expected.getRProperty()), tolerance) << what;
            EXPECT_LE(std::abs(actual.getGProperty() - expected.getGProperty()), tolerance) << what;
            EXPECT_LE(std::abs(actual.getBProperty() - expected.getBProperty()), tolerance) << what;
            EXPECT_EQ(actual.getAProperty(), expected.getAProperty()) << what;
        }

    private:
        std::optional<GraphicsDevice> device_;
        std::optional<Texture2D> red_;
    };
}

TEST_F(StockEffectNullTextureTest, BasicEffectWithTextureEnabledSamplesOpaqueBlack)
{
    // tools/xna-oracle/reference/null-texture/basic_textureenabled_null.png
    BasicEffect effect(Device());
    effect.setTextureEnabledProperty(true);
    effect.setTextureProperty(&RedTexture());
    ASSERT_EQ(Render([&] { effect.Apply(); DrawQuad(TexturedQuad()); }), kRed);

    effect.setTextureProperty(nullptr);
    EXPECT_EQ(Render([&] { effect.Apply(); DrawQuad(TexturedQuad()); }), kOpaqueBlack);
}

TEST_F(StockEffectNullTextureTest, BasicEffectWithoutTextureEnabledNeverReadsTheSlot)
{
    // The rule is about sampling, not about the slot: a BasicEffect that does not texture must draw its
    // material colour whatever is (or is not) bound -- unlit and lit, per-vertex and per-pixel.
    BasicEffect effect(Device());
    effect.setTextureEnabledProperty(false);
    effect.setDiffuseColorProperty(Vector3(0.0f, 0.0f, 1.0f));
    const Color blue(0, 0, 255, 255);
    EXPECT_EQ(Render([&] { effect.Apply(); DrawQuad(TexturedQuad()); }), blue) << "unlit";

    effect.setLightingEnabledProperty(true);
    AmbientOnly(effect);
    for (const bool perPixel : {false, true})
    {
        effect.setPreferPerPixelLightingProperty(perPixel);
        EXPECT_EQ(Render([&] { effect.Apply(); DrawQuad(NormalQuad()); }), blue)
            << (perPixel ? "lit per pixel" : "lit per vertex");
    }
}

TEST_F(StockEffectNullTextureTest, SkinnedEffectSamplesOpaqueBlack)
{
    // tools/xna-oracle/reference/null-texture/skinned_null.png
    SkinnedEffect effect(Device());
    AmbientOnly(effect);
    effect.setWeightsPerVertexProperty(1);
    effect.SetBoneTransforms({Matrix::getIdentityProperty()});
    for (const bool perPixel : {false, true})
    {
        // plans/plan_graphics_shared_cleanup.md follow-up GSC-F1: EasyGL's per-pixel SkinnedEffect program
        // draws this red-textured, ambient-only quad white (per-vertex and every other renderer here
        // draw red), which is a lighting defect of that program rather than the missing-texture rule.
        if (perPixel && CNA_RENDERER_IS(OpenGL33, OpenGLES3))
            continue;
        SCOPED_TRACE(perPixel ? "per pixel" : "per vertex");
        effect.setPreferPerPixelLightingProperty(perPixel);
        effect.setTextureProperty(&RedTexture());
        ASSERT_EQ(Render([&] { DrawSkinnedQuad(effect); }), kRed);

        effect.setTextureProperty(nullptr);
        EXPECT_EQ(Render([&] { DrawSkinnedQuad(effect); }), kOpaqueBlack);
    }
}

TEST_F(StockEffectNullTextureTest, AlphaTestEffectSamplesOpaqueBlack)
{
    // tools/xna-oracle/reference/null-texture/alphatest_null.png
    AlphaTestEffect effect(Device());
    effect.setAlphaFunctionProperty(CompareFunction::Always);
    effect.setTextureProperty(&RedTexture());
    ASSERT_EQ(Render([&] { effect.Apply(); DrawQuad(TexturedQuad()); }), kRed);

    effect.setTextureProperty(nullptr);
    EXPECT_EQ(Render([&] { effect.Apply(); DrawQuad(TexturedQuad()); }), kOpaqueBlack);
}

TEST_F(StockEffectNullTextureTest, EnvironmentMapEffectSamplesOpaqueBlackForEitherSlot)
{
    TextureCube cube(Device(), 1, false, SurfaceFormat::Color);
    const Color cubeColour(200, 100, 50, 255);
    for (int face = 0; face < 6; ++face)
        cube.SetData(static_cast<CubeMapFace>(face), &cubeColour, 1);

    EnvironmentMapEffect effect(Device());
    AmbientOnly(effect);
    effect.setEnvironmentMapAmountProperty(0.5f);
    effect.setFresnelFactorProperty(0.0f);

    // tools/xna-oracle/reference/null-texture/envmap_texture_null.png: lerp(black, cube, 0.5).
    effect.setEnvironmentMapProperty(&cube);
    effect.setTextureProperty(&RedTexture());
    ExpectNear(Render([&] { effect.Apply(); DrawQuad(NormalQuad()); }), Color(228, 50, 25, 255), 1,
               "both slots bound");
    effect.setTextureProperty(nullptr);
    ExpectNear(Render([&] { effect.Apply(); DrawQuad(NormalQuad()); }), Color(100, 50, 25, 255), 1,
               "Texture null");

    // tools/xna-oracle/reference/null-texture/envmap_cube_null.png: lerp(white, black, 0.5).
    Texture2D white(Device(), 1, 1, false, SurfaceFormat::Color);
    const Color whiteTexel = Color::White;
    white.SetData(&whiteTexel, 1);
    effect.setTextureProperty(&white);
    effect.setEnvironmentMapProperty(nullptr);
    ExpectNear(Render([&] { effect.Apply(); DrawQuad(NormalQuad()); }), Color(128, 128, 128, 255), 1,
               "EnvironmentMap null");
}
