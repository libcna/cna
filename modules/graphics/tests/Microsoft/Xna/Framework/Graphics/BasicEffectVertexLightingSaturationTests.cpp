// SPDX-License-Identifier: MS-PL
//
// plans/plan_apple_m4.md AM4-106 -- BasicEffect's per-vertex lighting reaches the pixel shader through
// COLOR registers, and Direct3D 9 saturates those.
//
// THE CONTRACT:
//
//   * FNA `BasicEffect.fx` -- `VSBasicVertexLighting(Vc)` writes the lit colour to
//     `float4 Diffuse : COLOR0` and the specular term to `float4 Specular : COLOR1`; the Vc variants
//     first apply `vout.Diffuse *= vin.Color`. `PSBasicVertexLighting(Tx)` multiplies the
//     interpolated Diffuse into the texel.
//   * Direct3D 9 saturates a vertex shader's colour output registers to [0,1] before the rasterizer
//     interpolates them, so a lit sum above 1 (any EnableDefaultLighting scene with ambient light)
//     reaches the pixel shader as exactly 1.
//   * EasyGL implements this as FX-123/FX-125; WebGPU did not before AM4-106 and drew brighter.
//
// THE ORACLE. One light straight at the quad (N.L = 1) with DiffuseColor (2,2,2) plus an
// AmbientLightColor of (1,1,1) gives a lit sum of 3. With a mid-grey texel (128):
//
//   saturated (XNA)                 -> 1 * 128                    = 128
//   unclamped                       -> 3 * 128, clipped at the end = 255
//
// and with a mid-grey vertex colour and VertexColorEnabled as well:
//
//   multiply, then saturate (XNA)   -> saturate(3 * 0.5) * 128    = 128
//   unclamped                       -> 3 * 0.5 * 128              = 193
//   saturate, then multiply         -> 1 * 0.5 * 128              =  64
//
// Per-pixel lighting (PreferPerPixelLighting) computes in the pixel shader, where nothing
// saturates, so it is deliberately not asserted here.

#include <cstdint>
#include <vector>
#include <gtest/gtest.h>

#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 8;

    struct PositionNormalColorTexture
    {
        Vector3 position;
        Vector3 normal;
        std::uint32_t color;
        Vector2 uv;
    };

    class BasicEffectVertexLightingSaturationTest : public ::testing::Test
    {
    protected:
        /// Draws one camera-facing quad lit per vertex to a lit sum of 3 and returns its centre pixel.
        static Color Draw(bool vertexColour)
        {
            GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                                  PresentationParameters());
            if (!device.SupportsCapability(CNA::GraphicsCapability::ThreeD)) return Color::Transparent;
            device.setRasterizerStateProperty(RasterizerState::CullNone);
            device.setDepthStencilStateProperty(DepthStencilState::None);
            device.setBlendStateProperty(BlendState::Opaque);

            Texture2D grey(device, 1, 1, false, SurfaceFormat::Color);
            const Color greyPixel(128, 128, 128, 255);
            grey.SetData(&greyPixel, 1);

            BasicEffect effect(device);
            effect.setWorldProperty(Matrix::getIdentityProperty());
            effect.setViewProperty(Matrix::CreateLookAt(Vector3(0, 0, 2), Vector3::Zero, Vector3::Up));
            effect.setProjectionProperty(Matrix::CreateOrthographic(2.0f, 2.0f, 0.1f, 10.0f));
            effect.setLightingEnabledProperty(true);
            effect.setPreferPerPixelLightingProperty(false);
            effect.setTextureEnabledProperty(true);
            effect.setTextureProperty(&grey);
            effect.setVertexColorEnabledProperty(vertexColour);
            effect.setDiffuseColorProperty(Vector3(1, 1, 1));
            effect.setEmissiveColorProperty(Vector3(0, 0, 0));
            effect.setSpecularColorProperty(Vector3(0, 0, 0));
            effect.setAmbientLightColorProperty(Vector3(1, 1, 1));
            effect.getDirectionalLight0Property().setEnabledProperty(true);
            effect.getDirectionalLight0Property().setDirectionProperty(Vector3(0, 0, -1));
            effect.getDirectionalLight0Property().setDiffuseColorProperty(Vector3(2, 2, 2));
            effect.getDirectionalLight0Property().setSpecularColorProperty(Vector3(0, 0, 0));
            effect.getDirectionalLight1Property().setEnabledProperty(false);
            effect.getDirectionalLight2Property().setEnabledProperty(false);

            const std::uint32_t mid = Color(128, 128, 128, 255).getPackedValueProperty();
            const Vector3 n(0, 0, 1);
            const std::vector<PositionNormalColorTexture> quad = {
                {Vector3(-1, -1, 0), n, mid, Vector2(0, 1)},
                {Vector3(-1,  1, 0), n, mid, Vector2(0, 0)},
                {Vector3( 1,  1, 0), n, mid, Vector2(1, 0)},
                {Vector3(-1, -1, 0), n, mid, Vector2(0, 1)},
                {Vector3( 1,  1, 0), n, mid, Vector2(1, 0)},
                {Vector3( 1, -1, 0), n, mid, Vector2(1, 1)},
            };
            const VertexDeclaration declaration(std::vector<VertexElement>{
                VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
                VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
                VertexElement(24, VertexElementFormat::Color, VertexElementUsage::Color, 0),
                VertexElement(28, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0)});

            RenderTarget2D target(device, kSize, kSize, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                  RenderTargetUsage::PreserveContents);
            device.SetRenderTarget(&target);
            device.Clear(Color::Black);
            effect.getCurrentTechniqueProperty()->getPassesProperty()[0]->Apply();
            device.DrawUserPrimitives(PrimitiveType::TriangleList, static_cast<const void*>(quad.data()), 0, 2,
                                      declaration);
            device.SetRenderTarget(nullptr);
            device.getTexturesProperty()(0, nullptr);

            std::vector<Color> pixels(static_cast<std::size_t>(kSize) * kSize, Color::Transparent);
            const Rectangle region(0, 0, kSize, kSize);
            target.GetData(0, &region, pixels.data(), 0, static_cast<int>(pixels.size()));
            return pixels[static_cast<std::size_t>(kSize / 2) * kSize + kSize / 2];
        }

        static bool Renders3D()
        {
            GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                                  PresentationParameters());
            return device.SupportsCapability(CNA::GraphicsCapability::ThreeD);
        }
    };
}

TEST_F(BasicEffectVertexLightingSaturationTest, ALitSumAboveOneIsSaturatedBeforeTheTexel)
{
    if (!Renders3D()) GTEST_SKIP() << "this renderer rasterizes no 3D triangles";
    const Color c = Draw(false);
    EXPECT_NEAR(c.getRProperty(), 128, 2) << "the lit sum of 3 must reach the pixel shader as 1";
    EXPECT_NEAR(c.getGProperty(), 128, 2);
    EXPECT_NEAR(c.getBProperty(), 128, 2);
}

TEST_F(BasicEffectVertexLightingSaturationTest, TheVertexColourScalesTheLitSumBeforeItIsSaturated)
{
    if (!Renders3D()) GTEST_SKIP() << "this renderer rasterizes no 3D triangles";
    const Color c = Draw(true);
    // 193 = unclamped, 64 = saturated before the vertex colour; XNA multiplies, then saturates.
    EXPECT_NEAR(c.getRProperty(), 128, 2) << "saturate(lit * vertex colour) * texel";
    EXPECT_NEAR(c.getGProperty(), 128, 2);
    EXPECT_NEAR(c.getBProperty(), 128, 2);
}
