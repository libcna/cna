// SPDX-License-Identifier: MS-PL
// plans/plan_dx2.md Phase O2 (DX2-12, 2D layer ported from DX1-45/DX1-46): TextureFilter (nearest vs. bilinear) and
// TextureAddressMode (Wrap/Mirror/Clamp) sampling tests for the DIRECTX6 (DirectDraw v4, via
// real DirectDraw v1, run under Wine -- no ../free-direct anywhere in this renderer) graphics renderer.
//
// All draws use BlendState::AlphaBlend with a fully-opaque (alpha=255) source texture: under
// AlphaBlend's premultiplied formula (out = src + dst*(1-srcAlpha)), srcAlpha=255 makes
// (1-srcAlpha)=0, so the result is exactly the sampled source color regardless of the
// destination's prior content -- this isolates the sampling logic itself (DX2-45/46) from any
// blend-formula concern (DX2-40..44, already covered by DirectX6_Blend).
//
// Check A -- TextureFilter::Point (nearest) at a texel boundary yields a pure endpoint color (not
//   a blend of the two neighboring texels).
// Check B -- TextureFilter::Linear (bilinear, the default) at the same boundary yields a genuine
//   in-between value (DX2-45).
// Check C/D/E -- TextureAddressMode::Wrap/Mirror/Clamp each address an out-of-[0,1) source
//   rectangle (2x the texture's own width) differently, producing 3 distinct, predictable
//   Red/Green tiling patterns (DX2-46).
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureAddressMode.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kCanvasSize = 32;

class DirectX6SamplingTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int passCount_ = 0;
    static constexpr int kTotal = 5;
    int result_ = 1;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount_;
    }

    static Color ReadPixel(GraphicsDevice& dev, int x, int y)
    {
        const Rectangle region(x, y, 1, 1);
        Color pixel(0, 0, 0, 0);
        dev.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        SpriteBatch sb(dev);

        // ---- Checks A/B: TextureFilter (DX2-45) ----
        // 2-texel black/white texture, scaled 4x (dest width 8) so the destination pixel at the
        // texel0/texel1 boundary (x=3, the last column still inside texel0's magnified footprint
        // for Point, and the first column close enough to the boundary for Linear to blend) shows
        // a clear difference between the two filter modes.
        {
            Texture2D tex(dev, 2, 1);
            std::vector<Color> px = { Color(0, 0, 0, 255), Color(255, 255, 255, 255) };
            tex.SetData(px.data(), static_cast<int>(px.size()));

            SamplerState point;
            point.setFilterProperty(TextureFilter::Point);
            SamplerState linear;
            linear.setFilterProperty(TextureFilter::Linear);

            dev.Clear(Color(1, 1, 1, 255));
            sb.Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, &point, nullptr, nullptr);
            sb.Draw(tex, Rectangle(0, 0, 8, 1), Rectangle(0, 0, 2, 1), Color(255, 255, 255, 255));
            sb.End();
            const int pointVal = ReadPixel(dev, 3, 0).getRProperty();
            check(pointVal == 0 || pointVal == 255,
                  "TextureFilter::Point samples a pure endpoint color at the texel boundary (DX2-45)");

            dev.Clear(Color(1, 1, 1, 255));
            sb.Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, &linear, nullptr, nullptr);
            sb.Draw(tex, Rectangle(0, 0, 8, 1), Rectangle(0, 0, 2, 1), Color(255, 255, 255, 255));
            sb.End();
            const int linearVal = ReadPixel(dev, 3, 0).getRProperty();
            check(linearVal > 0 && linearVal < 255,
                  "TextureFilter::Linear (bilinear) blends between texels at the boundary (DX2-45)");
        }

        // ---- Checks C/D/E: TextureAddressMode (DX2-46) ----
        // 2-texel Red/Green texture; source rectangle is 2x the texture's own width (4), so u
        // ranges [0, 2) -- entirely outside a single [0,1) tile. TextureFilter::Point keeps each
        // destination pixel's sampled texel unambiguous (no bilinear blending across the wrap/
        // mirror seam). Destination width 4, 1:1 with the oversized source rect.
        {
            Texture2D tex(dev, 2, 1);
            std::vector<Color> px = { Color(255, 0, 0, 255), Color(0, 255, 0, 255) };
            tex.SetData(px.data(), static_cast<int>(px.size()));

            SamplerState point;
            point.setFilterProperty(TextureFilter::Point);

            const auto drawAndSample = [&](TextureAddressMode mode) -> std::vector<int>
            {
                point.setAddressUProperty(mode);
                point.setAddressVProperty(mode);
                dev.Clear(Color(1, 1, 1, 255));
                sb.Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, &point, nullptr, nullptr);
                sb.Draw(tex, Rectangle(0, 0, 4, 1), Rectangle(0, 0, 4, 1), Color(255, 255, 255, 255));
                sb.End();
                std::vector<int> reds;
                for (int x = 0; x < 4; ++x) reds.push_back(ReadPixel(dev, x, 0).getRProperty());
                return reds;
            };

            // Red=(255,0,0) -> R=255; Green=(0,255,0) -> R=0. Expected R-channel patterns:
            const std::vector<int> wrapReds = drawAndSample(TextureAddressMode::Wrap);
            check(wrapReds == std::vector<int>({255, 0, 255, 0}),
                  "TextureAddressMode::Wrap tiles Red/Green/Red/Green (DX2-46)");

            const std::vector<int> mirrorReds = drawAndSample(TextureAddressMode::Mirror);
            check(mirrorReds == std::vector<int>({255, 0, 0, 255}),
                  "TextureAddressMode::Mirror reflects Red/Green/Green/Red (DX2-46)");

            const std::vector<int> clampReds = drawAndSample(TextureAddressMode::Clamp);
            check(clampReds == std::vector<int>({255, 0, 0, 0}),
                  "TextureAddressMode::Clamp holds the edge texel: Red/Green/Green/Green (DX2-46)");
        }

        std::printf("=== %d/%d PASS ===\n", passCount_, kTotal);
        result_ = (passCount_ == kTotal) ? 0 : 1;
        Exit();
    }

public:
    DirectX6SamplingTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kCanvasSize);
        gdm_->setPreferredBackBufferHeightProperty(kCanvasSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    DirectX6SamplingTest game;
    game.Run();
    return game.getResult();
}
