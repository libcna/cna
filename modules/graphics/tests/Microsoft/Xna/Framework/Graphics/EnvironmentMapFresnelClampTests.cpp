// SPDX-License-Identifier: MS-PL
//
// SAMPLE-037 RimLighting_4_0 -- EnvironmentMapEffect's fresnel term is a COLOR register, and
// Direct3D 9 clamps those.
//
// THE AUTHORITATIVE CONTRACT, cited rather than summarised:
//
//   * FNA `Structures.fxh:153-160` -- `VSOutputTxEnvMap` declares `float4 Specular : COLOR1;`.
//     The fresnel scalar travels to the pixel shader in that register, not a TEXCOORD.
//   * FNA `EnvironmentMapEffect.fx:61-66` -- `ComputeFresnelFactor` returns
//     `pow(max(1 - abs(viewAngle), 0), FresnelFactor) * EnvironmentMapAmount`, and line 86 takes
//     the `useFresnel == false` branch as `EnvironmentMapAmount` outright. Neither is bounded:
//     `EnvironmentMapAmount` is an ordinary float property whose XNA range runs past 1, and
//     RimLighting_4_0's own slidebar reaches 5.
//   * FNA `EnvironmentMapEffect.fx:129` -- `color.rgb = lerp(color.rgb, envmap.rgb,
//     pin.Specular.rgb);`. That register IS the interpolation weight.
//   * Direct3D 9 saturates a vertex shader's colour output registers to [0,1] BEFORE the
//     rasterizer interpolates them. So on the hardware this effect was written for, the weight
//     reaching that lerp is at most 1 and the environment map can replace the base colour but
//     never be extrapolated past.
//
// EasyGL implements the stock effect as its own GLSL, where the equivalent varying is an ordinary
// float and nothing clamps it. Without the clamp, `EnvironmentMapAmount > 1` extrapolates: on
// RimLighting_4_0 at amount 5 that turned the original's orange rim yellow-white and cost 4.5 %
// of the frame.
//
// THE ORACLE. FresnelFactor = 0 makes the weight exactly `EnvironmentMapAmount`, with no
// dependence on geometry or normals -- FNA's own `useFresnel` is `fresnelFactor != 0`
// (EnvironmentMapEffect.cs), so this is the branch the shipped effect takes, not a contrivance.
// With a WHITE base and a MID-GREY environment map:
//
//   amount 1 (weight 1)          -> the environment map itself, 128
//   amount 2, clamped (correct)  -> the environment map itself, 128   -- clamp makes them equal
//   amount 2, unclamped (bug)    -> 2*128 - 255 = 1, i.e. black
//
// so the two hypotheses are ~127 levels apart in every channel, and the amount-1 leg proves the
// clamp did not simply darken everything.

#include <cstdint>
#include <vector>
#include <gtest/gtest.h>

#include "CNA/RendererTestGate.hpp"

using namespace CNA::Testing::Renderers;

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 8;

    /// The renderers whose stock EnvironmentMapEffect rasterizes and whose RenderTarget2D::GetData
    /// reads the result back.
    [[nodiscard]] bool EnvironmentMapRasterizes()
    {
        return CNA_RENDERER_IS(OpenGLES3, OpenGL33, WebGL2, OpenGL4);
    }

    class EnvironmentMapFresnelClampTest : public ::testing::Test
    {
    protected:
        GraphicsDevice device;

        /// Draws one camera-facing quad and returns its centre pixel.
        Color DrawAt(float environmentMapAmount)
        {
            device.setRasterizerStateProperty(RasterizerState::CullNone);
            device.setDepthStencilStateProperty(DepthStencilState::None);
            device.setBlendStateProperty(BlendState::Opaque);

            // A white base texture, so the lerp's "from" end is 1 in every channel.
            Texture2D white(device, 1, 1, false, SurfaceFormat::Color);
            const Color whitePixel = Color::White;
            white.SetData(&whitePixel, 1);

            // A mid-grey environment map, so the lerp's "to" end is 0.5 in every channel.
            TextureCube cube(device, 1, false, SurfaceFormat::Color);
            const Color grey(128, 128, 128, 255);
            for (int face = 0; face < 6; ++face)
                cube.SetData(static_cast<CubeMapFace>(face), &grey, 0, 1);

            EnvironmentMapEffect effect(device);
            effect.setWorldProperty(Matrix::getIdentityProperty());
            effect.setViewProperty(
                Matrix::CreateLookAt(Vector3(0, 0, 2), Vector3::Zero, Vector3::Up));
            effect.setProjectionProperty(
                Matrix::CreateOrthographic(2.0f, 2.0f, 0.1f, 10.0f));
            effect.setTextureProperty(&white);
            effect.setEnvironmentMapProperty(&cube);
            effect.setDiffuseColorProperty(Vector3(1, 1, 1));
            effect.setEmissiveColorProperty(Vector3(1, 1, 1));
            effect.setAmbientLightColorProperty(Vector3(1, 1, 1));
            effect.getDirectionalLight0Property().setEnabledProperty(false);
            effect.getDirectionalLight1Property().setEnabledProperty(false);
            effect.getDirectionalLight2Property().setEnabledProperty(false);
            // FNA's own `useFresnel` is `fresnelFactor != 0`, so this selects the branch whose
            // weight is EnvironmentMapAmount outright -- no geometry term in the way.
            effect.setFresnelFactorProperty(0.0f);
            effect.setEnvironmentMapAmountProperty(environmentMapAmount);

            const std::vector<VertexPositionNormalTexture> quad = {
                {Vector3(-1, -1, 0), Vector3(0, 0, 1), Vector2(0, 1)},
                {Vector3(-1,  1, 0), Vector3(0, 0, 1), Vector2(0, 0)},
                {Vector3( 1,  1, 0), Vector3(0, 0, 1), Vector2(1, 0)},
                {Vector3(-1, -1, 0), Vector3(0, 0, 1), Vector2(0, 1)},
                {Vector3( 1,  1, 0), Vector3(0, 0, 1), Vector2(1, 0)},
                {Vector3( 1, -1, 0), Vector3(0, 0, 1), Vector2(1, 1)},
            };

            RenderTarget2D target(device, kSize, kSize, false, SurfaceFormat::Color,
                                  DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
            device.SetRenderTarget(&target);
            device.Clear(Color::Black);
            for (int i = 0; i < effect.getCurrentTechniqueProperty()->getPassesProperty()
                                        .getCountProperty(); ++i)
            {
                effect.getCurrentTechniqueProperty()->getPassesProperty()[i].Apply();
                device.DrawUserPrimitives(PrimitiveType::TriangleList, quad.data(), 0, 2);
            }
            device.SetRenderTarget(nullptr);

            std::vector<Color> pixels(static_cast<std::size_t>(kSize) * kSize, Color::Transparent);
            const Rectangle region(0, 0, kSize, kSize);
            target.GetData(0, &region, pixels.data(), 0, static_cast<int>(pixels.size()));
            return pixels[static_cast<std::size_t>(kSize / 2) * kSize + kSize / 2];
        }
    };
}

TEST_F(EnvironmentMapFresnelClampTest, AmountOneReachesTheEnvironmentMapExactly)
{
    if (!EnvironmentMapRasterizes()) GTEST_SKIP() << "renderer does not rasterize this route";
    const Color pixel = DrawAt(1.0f);
    EXPECT_NEAR(pixel.getRProperty(), 128, 8);
    EXPECT_NEAR(pixel.getGProperty(), 128, 8);
    EXPECT_NEAR(pixel.getBProperty(), 128, 8);
}

TEST_F(EnvironmentMapFresnelClampTest, AmountAboveOneDoesNotExtrapolatePastTheEnvironmentMap)
{
    if (!EnvironmentMapRasterizes()) GTEST_SKIP() << "renderer does not rasterize this route";
    // Unclamped this is 2*128 - 255 = 1 (black); clamped it stays the environment map's own 128.
    const Color pixel = DrawAt(2.0f);
    EXPECT_NEAR(pixel.getRProperty(), 128, 8);
    EXPECT_NEAR(pixel.getGProperty(), 128, 8);
    EXPECT_NEAR(pixel.getBProperty(), 128, 8);
}
