// SPDX-License-Identifier: MS-PL
//
// The one thing 94 passing SpriteBatch tests do not check: that a sprite actually appears.
//
// Every existing SpriteBatch suite verifies the API contract -- batching, sort order, state,
// lifetime, numeric input, sub-pixel destinations. All of them pass on a renderer that accepts
// every call and rasterizes nothing, because none of them ever looks at a pixel. That gap is how
// `cna_demo_2d` came to run on native Windows with Direct3D 11 showing an animated clear colour
// and not one of its fifty sprites.
//
// So this draws an opaque sprite over a known background and reads the pixel back. It is
// deliberately the smallest possible statement of "the renderer drew something where I asked".

#include <gtest/gtest.h>

#include <array>
#include <vector>

#include "CNA/RendererTestGate.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

using namespace CNA::Testing::Renderers;
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    // Wider than the narrowest captioned window Windows will create. A smaller back buffer makes
    // the window wider than the buffer, and the presentation layer letterboxes the logical buffer
    // inside it -- a separate path with its own readback questions, which these cases are not
    // about and should not be at the mercy of.
    constexpr int kSize = 256;

    GraphicsDevice MakeDevice(const DepthFormat depthFormat = DepthFormat::None)
    {
        PresentationParameters parameters;
        parameters.setBackBufferWidthProperty(kSize);
        parameters.setBackBufferHeightProperty(kSize);
        parameters.setDepthStencilFormatProperty(depthFormat);
        return GraphicsDevice(GraphicsAdapter::getDefaultAdapterProperty(),
                              GraphicsProfile::HiDef, parameters);
    }

    /** @brief An opaque white texture, so the drawn colour is whatever the sprite tint says. */
    Texture2D MakeWhiteTexture(GraphicsDevice& device, const int size = 8)
    {
        Texture2D texture(device, size, size);
        std::vector<Color> pixels(static_cast<std::size_t>(size * size), Color::White);
        texture.SetData(pixels.data(), 0, static_cast<int>(pixels.size()));
        return texture;
    }

    Color ReadPixel(GraphicsDevice& device, const int x, const int y)
    {
        Color pixel = Color::Transparent;
        const Rectangle rectangle(x, y, 1, 1);
        device.GetBackBufferData(&rectangle, &pixel, 0, 1);
        return pixel;
    }
}

// The whole point, in one assertion: clear to black, cover the middle with an opaque red sprite,
// and look. A renderer that accepts the batch and draws nothing leaves the pixel black, and every
// other SpriteBatch test in this repository still passes while it does.
TEST(SpriteBatchRasterizationTest, AnOpaqueSpriteActuallyChangesThePixelsItCovers)
{
    // Gated to the renderers that actually rasterize and can hand a pixel back. The list
    // deliberately includes DirectX11, which every existing readback suite omits -- which is
    // why a D3D11 SpriteBatch that draws nothing has never failed a test here.
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGL4, OpenGLES3, DirectX11, DirectX12);

    GraphicsDevice device = MakeDevice();
    Texture2D texture = MakeWhiteTexture(device);

    device.Clear(Color::Black);
    ASSERT_EQ(ReadPixel(device, kSize / 2, kSize / 2), Color::Black)
        << "the background was not cleared, so nothing below this can be interpreted";

    SpriteBatch spriteBatch(device);
    spriteBatch.Begin();
    spriteBatch.Draw(texture, Rectangle(8, 8, kSize - 16, kSize - 16), Color::Red);
    spriteBatch.End();

    EXPECT_EQ(ReadPixel(device, kSize / 2, kSize / 2), Color::Red)
        << "SpriteBatch accepted the draw and the covered pixel is unchanged -- the sprite was "
           "never rasterized";
}

// The destination rectangle has to mean something too: a renderer that drew the sprite over the
// whole target would pass the test above and still be wrong.
TEST(SpriteBatchRasterizationTest, TheDestinationRectangleBoundsWhatIsDrawn)
{
    // Gated to the renderers that actually rasterize and can hand a pixel back. The list
    // deliberately includes DirectX11, which every existing readback suite omits -- which is
    // why a D3D11 SpriteBatch that draws nothing has never failed a test here.
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGL4, OpenGLES3, DirectX11, DirectX12);

    GraphicsDevice device = MakeDevice();
    Texture2D texture = MakeWhiteTexture(device);

    device.Clear(Color::Black);
    SpriteBatch spriteBatch(device);
    spriteBatch.Begin();
    spriteBatch.Draw(texture, Rectangle(0, 0, kSize / 2, kSize / 2), Color::Red);
    spriteBatch.End();

    EXPECT_EQ(ReadPixel(device, kSize / 4, kSize / 4), Color::Red) << "inside the destination";
    EXPECT_EQ(ReadPixel(device, kSize - 4, kSize - 4), Color::Black)
        << "outside the destination -- the sprite covered more than it was given";
}

// Is the fault SpriteBatch's, or the renderer's altogether? A full-screen triangle pair through
// BasicEffect/DrawUserPrimitives shares the device, the render target, the viewport and the
// rasterizer state with the sprite path, and shares none of its shader, input layout or vertex
// buffer. If this draws and the sprite above does not, the fault is in the sprite path; if neither
// draws, nothing about SpriteBatch is implicated.
TEST(SpriteBatchRasterizationTest, APlainPrimitiveDrawAlsoReachesTheBackBuffer)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGL4, OpenGLES3, DirectX11, DirectX12);

    GraphicsDevice device = MakeDevice();
    device.Clear(Color::Black);
    // CullNone for the same reason the existing readback suites set it: a full-screen pair whose
    // winding the renderer disagrees with is culled, and the test then measures nothing.
    device.setRasterizerStateProperty(RasterizerState::CullNone);

    BasicEffect effect(device);
    effect.VertexColorEnabled = true;
    effect.setLightingEnabledProperty(false);
    effect.setWorldProperty(Matrix::getIdentityProperty());
    effect.setViewProperty(Matrix::getIdentityProperty());
    effect.setProjectionProperty(Matrix::getIdentityProperty());
    effect.Apply();

    const std::array vertices = {
        VertexPositionColor(Vector3(-1.0f,  1.0f, 0.0f), Color::Red),
        VertexPositionColor(Vector3(-1.0f, -1.0f, 0.0f), Color::Red),
        VertexPositionColor(Vector3( 1.0f, -1.0f, 0.0f), Color::Red),
        VertexPositionColor(Vector3(-1.0f,  1.0f, 0.0f), Color::Red),
        VertexPositionColor(Vector3( 1.0f, -1.0f, 0.0f), Color::Red),
        VertexPositionColor(Vector3( 1.0f,  1.0f, 0.0f), Color::Red),
    };
    device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices.data(), 0, 2);

    EXPECT_EQ(ReadPixel(device, kSize / 2, kSize / 2), Color::Red)
        << "not even a plain primitive reaches the back buffer on this renderer";
}

// Narrowing the sprite failure: the same draw, with culling switched off.
//
// SpriteBatch's default rasterizer state is CullCounterClockwise. D3D11 treats clockwise as
// front-facing (FrontCounterClockwise = FALSE) while the GL path's projection flips Y and with it
// the effective winding, so one quad vertex order can be front-facing on one and back-facing on
// the other. If the sprite appears with CullNone and not without it, that is the whole defect.
TEST(SpriteBatchRasterizationTest, TheDefaultRasterizerStateDoesNotCullTheSprite)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGL4, OpenGLES3, DirectX11, DirectX12);

    GraphicsDevice device = MakeDevice();
    Texture2D texture = MakeWhiteTexture(device);

    device.Clear(Color::Black);
    SpriteBatch spriteBatch(device);
    RasterizerState cullNone = RasterizerState::CullNone;
    spriteBatch.Begin(SpriteSortMode::Deferred, nullptr, nullptr, nullptr, &cullNone, nullptr);
    spriteBatch.Draw(texture, Rectangle(8, 8, kSize - 16, kSize - 16), Color::Red);
    spriteBatch.End();

    EXPECT_EQ(ReadPixel(device, kSize / 2, kSize / 2), Color::Red)
        << "the sprite does not rasterize even with culling disabled, so winding is not the cause";
}

// Separating "the sprite was never rasterized" from "the sprite was rasterized black".
//
// The two are indistinguishable against a black background, and every case above uses one. So:
// clear to RED, draw a WHITE-tinted sprite with Opaque blending, and read the covered pixel. The
// three outcomes name three different defects.
//
//   red    -> nothing rasterized; the draw never reaches the target
//   white  -> everything works
//   black  -> rasterized, but the texture sampled as zero -- SetData never reached the GPU, or
//             the shader resource view is not the texture that was bound
//
// Opaque rather than the default AlphaBlend deliberately: with alpha blending a texture that
// samples as (0,0,0,0) is invisible, which collapses the third outcome back into the first.
TEST(SpriteBatchRasterizationTest, ASpriteThatDrawsNothingIsToldApartFromOneThatDrawsBlack)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGL4, OpenGLES3, DirectX11, DirectX12);

    GraphicsDevice device = MakeDevice();
    Texture2D texture = MakeWhiteTexture(device);

    device.Clear(Color::Red);
    SpriteBatch spriteBatch(device);
    BlendState opaque = BlendState::Opaque;
    spriteBatch.Begin(SpriteSortMode::Deferred, &opaque, nullptr, nullptr, nullptr, nullptr);
    spriteBatch.Draw(texture, Rectangle(8, 8, kSize - 16, kSize - 16), Color::White);
    spriteBatch.End();

    const Color covered = ReadPixel(device, kSize / 2, kSize / 2);
    EXPECT_NE(covered, Color::Red) << "nothing was rasterized: the draw never reached the target";
    EXPECT_NE(covered, Color::Black)
        << "the sprite rasterized but sampled the texture as zero: SetData never reached the GPU, "
           "or the bound shader resource view is not this texture";
    EXPECT_EQ(covered, Color::White);
}

// A game's device has a depth buffer: GraphicsDeviceManager's PreferredDepthStencilFormat defaults
// to Depth24, and cna_demo_2d gets one. Every case above uses DepthFormat::None, so none of them
// can see a sprite path that leaves depth testing to whatever state the context happens to hold.
TEST(SpriteBatchRasterizationTest, ASpriteStillRasterizesWhenTheBackBufferHasADepthBuffer)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGL4, OpenGLES3, DirectX11, DirectX12);

    GraphicsDevice device = MakeDevice(DepthFormat::Depth24Stencil8);
    Texture2D texture = MakeWhiteTexture(device);

    device.Clear(Color::Black);
    SpriteBatch spriteBatch(device);
    spriteBatch.Begin();
    spriteBatch.Draw(texture, Rectangle(8, 8, kSize - 16, kSize - 16), Color::Red);
    spriteBatch.End();

    EXPECT_EQ(ReadPixel(device, kSize / 2, kSize / 2), Color::Red)
        << "with a depth buffer present the sprite does not rasterize";
}

// cna_demo_2d draws with rotation and an origin at the centre of the SOURCE rectangle, which XNA
// scales by destination/source. The cases above use neither.
TEST(SpriteBatchRasterizationTest, ARotatedSpriteWithACentreOriginLandsWhereXnaPutsIt)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGL4, OpenGLES3, DirectX11, DirectX12);

    GraphicsDevice device = MakeDevice();
    Texture2D texture = MakeWhiteTexture(device, 64);

    device.Clear(Color::Black);
    SpriteBatch spriteBatch(device);
    spriteBatch.Begin();
    // Destination centred at (128,128), 96 px square; origin at the 64 px source's centre.
    spriteBatch.Draw(texture, Rectangle(kSize / 2, kSize / 2, 96, 96), Rectangle(0, 0, 64, 64),
                     Color::Red, 0.3f, Vector2(32.0f, 32.0f), SpriteEffects::None, 0.0f);
    spriteBatch.End();

    EXPECT_EQ(ReadPixel(device, kSize / 2, kSize / 2), Color::Red)
        << "the rotated, origin-centred sprite did not cover its own centre";
}

// What every game does from its SECOND frame on, and what no case above ever did: draw after a
// Present. Every earlier case reads the back buffer before the first Present, which is exactly the
// one moment a flip-model swap chain is still bound to the output merger.
//
// DXGI_SWAP_EFFECT_FLIP_DISCARD unbinds the back buffer from the pipeline on Present. Clear() does
// not notice, because it names its render target view explicitly; a draw does, because it renders
// into whatever OMSetRenderTargets last bound -- which after a Present is nothing. So a renderer
// that does not rebind shows a correct clear colour every frame and none of its geometry, from the
// second frame onward. That is precisely what cna_demo_2d shows on DirectX11.
TEST(SpriteBatchRasterizationTest, ASpriteStillRasterizesInTheFrameAfterAPresent)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGL4, OpenGLES3, DirectX11, DirectX12);

    GraphicsDevice device = MakeDevice();
    Texture2D texture = MakeWhiteTexture(device);
    SpriteBatch spriteBatch(device);

    // Frame one.
    device.Clear(Color::Black);
    spriteBatch.Begin();
    spriteBatch.Draw(texture, Rectangle(8, 8, kSize - 16, kSize - 16), Color::Red);
    spriteBatch.End();
    device.Present();

    // Frame two.
    device.Clear(Color::Black);
    spriteBatch.Begin();
    spriteBatch.Draw(texture, Rectangle(8, 8, kSize - 16, kSize - 16), Color::Red);
    spriteBatch.End();

    EXPECT_EQ(ReadPixel(device, kSize / 2, kSize / 2), Color::Red)
        << "the sprite rasterized before the first Present and not after it: the back buffer "
           "is no longer bound to the output merger";
}

// The same after a Present with a plain primitive, so the finding is attributed to the right layer:
// if this fails too, it is not SpriteBatch's defect but the renderer's frame loop.
TEST(SpriteBatchRasterizationTest, APlainPrimitiveStillReachesTheBackBufferAfterAPresent)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGL4, OpenGLES3, DirectX11, DirectX12);

    GraphicsDevice device = MakeDevice();
    device.Clear(Color::Black);
    device.Present();

    device.Clear(Color::Black);
    device.setRasterizerStateProperty(RasterizerState::CullNone);
    BasicEffect effect(device);
    effect.VertexColorEnabled = true;
    effect.setLightingEnabledProperty(false);
    effect.setWorldProperty(Matrix::getIdentityProperty());
    effect.setViewProperty(Matrix::getIdentityProperty());
    effect.setProjectionProperty(Matrix::getIdentityProperty());
    effect.Apply();
    const std::array vertices = {
        VertexPositionColor(Vector3(-1.0f,  1.0f, 0.0f), Color::Red),
        VertexPositionColor(Vector3(-1.0f, -1.0f, 0.0f), Color::Red),
        VertexPositionColor(Vector3( 1.0f, -1.0f, 0.0f), Color::Red),
        VertexPositionColor(Vector3(-1.0f,  1.0f, 0.0f), Color::Red),
        VertexPositionColor(Vector3( 1.0f, -1.0f, 0.0f), Color::Red),
        VertexPositionColor(Vector3( 1.0f,  1.0f, 0.0f), Color::Red),
    };
    device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices.data(), 0, 2);

    EXPECT_EQ(ReadPixel(device, kSize / 2, kSize / 2), Color::Red)
        << "a plain primitive after a Present does not reach the back buffer either";
}
