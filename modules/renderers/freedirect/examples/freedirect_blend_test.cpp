// SPDX-License-Identifier: MS-PL
// plans/plan_freedirect.md Phase X5 (DX3-40..DX3-44): blend-mode compositing math tests for the DIRECTX3
// (DirectDraw, via the ../free-direct sibling) graphics renderer.
//
// All 4 checks draw the SAME source pixel (200, 0, 0, 100) over the SAME background
// (0, 50, 0, 255), varying only the BlendState. The 4 presets' real, distinct formulas produce
// 4 different (R, G) pairs at that pixel, chosen with comfortable margin from any rounding
// boundary so each check is an exact match, not a tolerance-based one:
//   Opaque:           (200,   0) -- direct overwrite, alpha irrelevant.
//   AlphaBlend:       (200,  30) -- premultiplied convention: src used as-is, dst*(1-srcAlpha).
//   NonPremultiplied: ( 78,  30) -- straight alpha: src*srcAlpha, dst*(1-srcAlpha).
//   Additive:         ( 78,  50) -- src*srcAlpha, dst passes through unattenuated.
// Check E confirms DX3-44's fallback: a custom (non-preset) BlendState combination produces the
// exact same result as AlphaBlend. Check F confirms the fallback also applies when factors alone
// happen to match a preset (Opaque) but the BlendFunction doesn't -- the equation, not just the
// factors, must match for a real preset detection.
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
#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendFunction.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kCanvasSize = 32;

class FreeDirectBlendTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int passCount_ = 0;
    static constexpr int kTotal = 6;
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

    // Draws the fixed (200,0,0,100)-over-(0,50,0,255) scenario with `blendState` and returns the
    // resulting (R, G) at the drawn pixel.
    static std::pair<int, int> DrawAndReadRG(GraphicsDevice& dev, SpriteBatch& sb, const BlendState& blendState)
    {
        dev.Clear(Color(0, 50, 0, 255));

        Texture2D tex(dev, 1, 1);
        std::vector<Color> px(1, Color(200, 0, 0, 100));
        tex.SetData(px.data(), 1);

        sb.Begin(SpriteSortMode::Deferred, blendState);
        sb.Draw(tex, 0.0f, 0.0f);
        sb.End();

        const Color got = ReadPixel(dev, 0, 0);
        return { got.getRProperty(), got.getGProperty() };
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        SpriteBatch sb(dev);

        {
            const auto [r, g] = DrawAndReadRG(dev, sb, BlendState::Opaque);
            check(r == 200 && g == 0, "Opaque: direct overwrite, ignores source alpha (DX3-40)");
        }
        {
            const auto [r, g] = DrawAndReadRG(dev, sb, BlendState::AlphaBlend);
            check(r == 200 && g == 30, "AlphaBlend: premultiplied formula (src + dst*(1-srcAlpha)) (DX3-41)");
        }
        {
            const auto [r, g] = DrawAndReadRG(dev, sb, BlendState::NonPremultiplied);
            check(r == 78 && g == 30, "NonPremultiplied: straight-alpha formula (src*srcAlpha + dst*(1-srcAlpha)) (DX3-42)");
        }
        {
            const auto [r, g] = DrawAndReadRG(dev, sb, BlendState::Additive);
            check(r == 78 && g == 50, "Additive: saturating add, dst unattenuated (DX3-43)");
        }
        {
            BlendState custom;
            custom.setColorSourceBlendProperty(Blend::SourceColor);
            custom.setColorDestinationBlendProperty(Blend::One);
            custom.setAlphaSourceBlendProperty(Blend::SourceColor);
            custom.setAlphaDestinationBlendProperty(Blend::One);
            const auto [r, g] = DrawAndReadRG(dev, sb, custom);
            check(r == 200 && g == 30,
                  "Custom (non-preset) BlendState falls back to AlphaBlend behavior (DX3-44)");
        }
        {
            // A BlendState with Opaque's EXACT factors (One,One,Zero,Zero) but a non-Add
            // BlendFunction is NOT actually equivalent to Opaque -- the blend equation itself
            // differs, not just the factors. Must still fall back to AlphaBlend (r=200,g=30),
            // not be misdetected as Opaque (which would give r=200,g=0).
            BlendState opaqueFactorsSubtractFunc;
            opaqueFactorsSubtractFunc.setColorSourceBlendProperty(Blend::One);
            opaqueFactorsSubtractFunc.setColorDestinationBlendProperty(Blend::Zero);
            opaqueFactorsSubtractFunc.setAlphaSourceBlendProperty(Blend::One);
            opaqueFactorsSubtractFunc.setAlphaDestinationBlendProperty(Blend::Zero);
            opaqueFactorsSubtractFunc.setColorBlendFunctionProperty(BlendFunction::Subtract);
            opaqueFactorsSubtractFunc.setAlphaBlendFunctionProperty(BlendFunction::Subtract);
            const auto [r, g] = DrawAndReadRG(dev, sb, opaqueFactorsSubtractFunc);
            check(r == 200 && g == 30,
                  "Opaque-matching factors with BlendFunction::Subtract still falls back to "
                  "AlphaBlend, not misdetected as Opaque (DX3-44)");
        }

        std::printf("=== %d/%d PASS ===\n", passCount_, kTotal);
        result_ = (passCount_ == kTotal) ? 0 : 1;
        Exit();
    }

public:
    FreeDirectBlendTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kCanvasSize);
        gdm_->setPreferredBackBufferHeightProperty(kCanvasSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    FreeDirectBlendTest game;
    game.Run();
    return game.getResult();
}
