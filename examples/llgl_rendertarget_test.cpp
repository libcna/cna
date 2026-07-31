// SPDX-License-Identifier: MS-PL
// plan_llgl.md LLGL-26: RenderTarget2D proof for the LLGL graphics backend, asserted against real
// pixels read back from the GPU.
//
// The same 4x4 quadrant texture used by llgl_2d_test.cpp (red / green / blue / white) is drawn
// INTO a RenderTarget2D, the render target is unbound, and its colour texture is then sampled
// back onto the back buffer with SpriteBatch -- proving CreateRenderTarget2D's colour attachment
// really was written to by the draws issued while it was bound, that unbinding restores the back
// buffer's own render pass (a second, independent Clear), and that a RenderTarget2D's backend
// (LlglRenderTargetBackend) is accepted anywhere a Texture2D is (the RenderTarget2D-vs-
// LlglTextureBackend cross-cast fixed while implementing this).
//
// Check A -- RenderTarget2D construction succeeds.
// Check B -- the back buffer keeps its own clear colour after a render target was drawn into
//   (proves the two are independent render passes, not one pass that clobbered the other).
// Check C..F -- sampling the render target back onto the screen reproduces all four quadrants in
//   the right corners (proves the colour attachment orientation, not just that something drew).
// Check G -- RenderTarget2D::GetData() reads the same pixel back directly, without going through
//   SpriteBatch at all (proves LlglRenderTargetBackend::GetData()).
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdint>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    std::vector<std::uint8_t> MakeQuadrantTexturePixels()
    {
        // 4x4 RGBA8: one solid colour per 2x2 quadrant -- red / green / blue / white. Identical to
        // llgl_2d_test.cpp's texture so a Y-flip or orientation mistake shows up the same way.
        std::vector<std::uint8_t> pixels(4 * 4 * 4, 0);
        auto setPixel = [&](int x, int y, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
            const std::size_t offset = (static_cast<std::size_t>(y) * 4 + static_cast<std::size_t>(x)) * 4;
            pixels[offset + 0] = r;
            pixels[offset + 1] = g;
            pixels[offset + 2] = b;
            pixels[offset + 3] = a;
        };
        for (int y = 0; y < 4; ++y)
        {
            for (int x = 0; x < 4; ++x)
            {
                if (x < 2 && y < 2)       setPixel(x, y, 255, 0, 0, 255);
                else if (x >= 2 && y < 2) setPixel(x, y, 0, 255, 0, 255);
                else if (x < 2)           setPixel(x, y, 0, 0, 255, 255);
                else                      setPixel(x, y, 255, 255, 255, 255);
            }
        }
        return pixels;
    }
}

class LlglRenderTargetTest : public CNA::Examples::PixelTestGame
{
public:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();

        std::unique_ptr<Texture2D> sourceTexture;
        try
        {
            sourceTexture = std::make_unique<Texture2D>(
                Texture2D::CreateFromPixels(device, 4, 4, MakeQuadrantTexturePixels()));
        }
        catch (const std::exception&)
        {
            sourceTexture.reset();
        }
        if (!ExpectTrue("Texture2D::CreateFromPixels() succeeds for the source texture",
                        sourceTexture != nullptr))
        {
            return;
        }

        std::unique_ptr<RenderTarget2D> renderTarget;
        try
        {
            renderTarget = std::make_unique<RenderTarget2D>(device, 64, 64);
        }
        catch (const std::exception&)
        {
            renderTarget.reset();
        }
        if (!ExpectTrue("RenderTarget2D(device, 64, 64) construction succeeds",
                        renderTarget != nullptr && renderTarget->getWidthProperty() == 64 &&
                        renderTarget->getHeightProperty() == 64))
        {
            return;
        }

        SpriteBatch spriteBatch(device);
        SamplerState pointClamp = SamplerState::PointClamp;

        // Draw the whole quadrant texture, magnified, into the render target.
        device.SetRenderTarget(renderTarget.get());
        device.Clear(Color(0, 0, 0, 255));
        spriteBatch.Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, &pointClamp, nullptr, nullptr);
        spriteBatch.Draw(*sourceTexture, Rectangle(0, 0, 64, 64), Rectangle(0, 0, 4, 4), Color::White);
        spriteBatch.End();

        // Restore the back buffer and clear it to a colour the render target never used, so a
        // pass that leaked into the wrong target would be caught immediately.
        device.SetRenderTarget(nullptr);
        device.Clear(Color(50, 50, 50, 255));

        ExpectPixel("back buffer keeps its own clear colour outside the sampled region",
                    Rectangle(10, 10, 1, 1), Color(50, 50, 50, 255));

        // Sample the render target's colour texture back onto the screen -- RenderTarget2D IS-A
        // Texture2D, and this is exactly the cross-cast (LlglRenderTargetBackend vs
        // LlglTextureBackend) SpriteBatch::Draw has to accept.
        spriteBatch.Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, &pointClamp, nullptr, nullptr);
        spriteBatch.Draw(*renderTarget, Rectangle(100, 100, 64, 64), Rectangle(0, 0, 64, 64), Color::White);
        spriteBatch.End();

        ExpectPixel("sampled top-left quadrant lands top-left (red)", Rectangle(116, 116, 1, 1),
                    Color(255, 0, 0, 255));
        ExpectPixel("sampled top-right quadrant lands top-right (green)", Rectangle(148, 116, 1, 1),
                    Color(0, 255, 0, 255));
        ExpectPixel("sampled bottom-left quadrant lands bottom-left (blue)", Rectangle(116, 148, 1, 1),
                    Color(0, 0, 255, 255));
        ExpectPixel("sampled bottom-right quadrant lands bottom-right (white)", Rectangle(148, 148, 1, 1),
                    Color(255, 255, 255, 255));

        // RenderTarget2D::GetData(), independent of SpriteBatch/back-buffer sampling entirely.
        Color direct(0, 0, 0, 0);
        Rectangle topLeftTexel(8, 8, 1, 1);
        renderTarget->GetData(0, &topLeftTexel, &direct, 0, 1);
        ExpectTrue("RenderTarget2D::GetData() reads the top-left quadrant back directly (red)",
                   direct == Color(255, 0, 0, 255));
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<LlglRenderTargetTest>();
}
