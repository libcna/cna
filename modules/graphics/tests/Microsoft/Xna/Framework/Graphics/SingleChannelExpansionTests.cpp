// SPDX-License-Identifier: MS-PL
//
// SAMPLE-038 ShadowMappingSample_4_0 -- Direct3D 9 expands a texture's missing channels when a
// shader samples it; OpenGL does not, and the two disagree visibly.
//
// THE AUTHORITATIVE CONTRACT:
//
//   * Direct3D 9's texture format expansion gives a shader `(R, 1, 1, 1)` for a one-channel
//     format and `(R, G, 1, 1)` for a two-channel one. That is why XNA's ShadowMapping sample can
//     draw its `SurfaceFormat.Single` shadow map straight to the screen with SpriteBatch and see
//     a white-to-black depth image.
//   * OpenGL expands the same storage to `(R, 0, 0, 1)` and `(R, G, 0, 1)`. The identical draw
//     produces a RED image, which is what CNA rendered before this fix and what FNA renders
//     today -- FNA3D maps SurfaceFormat.Single to GL_R32F/GL_RED (FNA3D_Driver_OpenGL.c:378) and
//     applies no swizzle anywhere.
//
// GL_TEXTURE_SWIZZLE_G/B/A = GL_ONE is exactly the D3D9 rule and is core in GL ES 3.0 and desktop
// GL 3.3. It is deliberately NOT the mechanism here: **WebGL 2 exposes neither the constants nor
// the parameter** -- measured in a real browser, `texParameteri(TEXTURE_SWIZZLE_G, ONE)` raises
// INVALID_ENUM -- and this campaign ships both a native and a WEBGL2 target of every sample, so a
// swizzle-based fix would make them disagree with each other. The expansion is applied in the
// sprite fragment shader instead, where every profile can do it identically.
//
// THE ORACLE. A one-channel texture holding 1.0, drawn with a white tint:
//
//   expanded (correct)   -> (255, 255, 255)
//   unexpanded (the bug) -> (255,   0,   0)
//
// and a Color texture drawn the same way must be untouched, which is what separates "the
// expansion was applied" from "the shader multiplies everything by one".

#include <cstdint>
#include <vector>
#include <gtest/gtest.h>

#include "CNA/RendererTestGate.hpp"

using namespace CNA::Testing::Renderers;

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Vector2;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 8;

    /// The renderers whose SpriteBatch rasterizes and whose RenderTarget2D::GetData reads back.
    [[nodiscard]] bool SpriteBatchRasterizes()
    {
        return CNA_RENDERER_IS(OpenGLES3, OpenGL33, WebGL2, OpenGL4);
    }

    class SingleChannelExpansionTest : public ::testing::Test
    {
    protected:
        GraphicsDevice device;

        /// Draws one texture over the whole target with SpriteBatch and returns a centre pixel.
        Color DrawThroughSpriteBatch(Texture2D& texture)
        {
            RenderTarget2D target(device, kSize, kSize, false, SurfaceFormat::Color,
                                  DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
            SpriteBatch batch(device);
            device.SetRenderTarget(&target);
            device.Clear(Color::Black);
            SamplerState pointClamp = SamplerState::PointClamp;
            batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &pointClamp,
                        nullptr, nullptr);
            batch.Draw(texture, Rectangle(0, 0, kSize, kSize), Color::White);
            batch.End();
            device.SetRenderTarget(nullptr);

            std::vector<Color> pixels(static_cast<std::size_t>(kSize) * kSize, Color::Transparent);
            const Rectangle region(0, 0, kSize, kSize);
            target.GetData(0, &region, pixels.data(), 0, static_cast<int>(pixels.size()));
            return pixels[static_cast<std::size_t>(kSize / 2) * kSize + kSize / 2];
        }
    };
}

TEST_F(SingleChannelExpansionTest, SingleFormatSamplesWithGreenBlueAndAlphaAtOne)
{
    if (!SpriteBatchRasterizes()) GTEST_SKIP() << "renderer does not rasterize this route";

    // EasyGL offers SurfaceFormat.Single as a render-target format, not as an ordinary
    // Texture2D one -- which is exactly how ShadowMappingSample_4_0 obtains its shadow map.
    // Clearing to white leaves R = 1, the value the sample's own cleared map holds.
    RenderTarget2D single(device, kSize, kSize, false, SurfaceFormat::Single,
                          DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
    device.SetRenderTarget(&single);
    device.Clear(Color::White);
    device.SetRenderTarget(nullptr);

    const Color pixel = DrawThroughSpriteBatch(single);
    // Unexpanded this is (255, 0, 0): GL's own (R, 0, 0, 1).
    EXPECT_NEAR(pixel.getRProperty(), 255, 2);
    EXPECT_NEAR(pixel.getGProperty(), 255, 2);
    EXPECT_NEAR(pixel.getBProperty(), 255, 2);
}

TEST_F(SingleChannelExpansionTest, ColorFormatIsLeftAlone)
{
    if (!SpriteBatchRasterizes()) GTEST_SKIP() << "renderer does not rasterize this route";

    // The same draw with a four-channel texture must be untouched by the expansion; a shader
    // that expanded unconditionally would turn this green pixel white.
    Texture2D color(device, 1, 1, false, SurfaceFormat::Color);
    const Color green(0, 200, 0, 255);
    color.SetData(&green, 1);

    const Color pixel = DrawThroughSpriteBatch(color);
    EXPECT_NEAR(pixel.getRProperty(), 0, 2);
    EXPECT_NEAR(pixel.getGProperty(), 200, 3);
    EXPECT_NEAR(pixel.getBProperty(), 0, 2);
}
