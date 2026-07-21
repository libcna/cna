// SPDX-License-Identifier: MS-PL
// Blend-mode compositing tests for the DX8 (real Direct3D 8 via DXVK) graphics backend.
//
// Unlike DX2-DX7 (DirectDraw-based, no programmable blend hardware -- ApplyBlendState there
// detects which of the 4 XNA BlendState *presets* a caller's factors match and falls back to a
// hardcoded AlphaBlend CPU formula for anything else), DX8 has REAL Direct3D 8 blend hardware:
// ApplyBlendState sets D3DRS_SRCBLEND/D3DRS_DESTBLEND directly from the caller's own factors, so
// ANY BlendState -- preset or custom -- produces its own genuine hardware blend result. There is
// no preset-detection/fallback step to test here at all.
//
// All 6 checks draw the SAME source pixel (200, 0, 0, 100) over the SAME background
// (0, 50, 0, 255), varying only the BlendState:
//   Opaque:           (200,   0) -- direct overwrite, alpha irrelevant.
//   AlphaBlend:       (200,  30) -- premultiplied convention: src used as-is, dst*(1-srcAlpha).
//   NonPremultiplied: ( 78,  30) -- straight alpha: src*srcAlpha, dst*(1-srcAlpha).
//   Additive:         ( 78,  50) -- src*srcAlpha, dst passes through unattenuated.
// Check E -- a custom (non-preset) BlendState (ColorSourceBlend=SourceColor,
//   ColorDestinationBlend=One) produces the real formula's own result (src*src + dst*1 =
//   (157, 50)), not any of the 4 presets' results -- proving factors are applied directly, not
//   matched against a preset table.
// Check F -- D3D8 has no configurable blend EQUATION (no D3DRS_BLENDOP -- that's a D3D9
//   addition), only blend FACTORS -- so a BlendState with Opaque's exact factors (One, Zero) but
//   BlendFunction::Subtract is genuinely indistinguishable from real Opaque on this hardware and
//   correctly produces Opaque's own result (200, 0), not a different "custom" result. This is a
//   real, documented DirectX-8-era hardware boundary (ApplyBlendState's own doc comment), not a
//   detection bug.
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

class Dx8BlendTest : public Game
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
            check(r == 200 && g == 0, "Opaque: direct overwrite, ignores source alpha (DX2-40)");
        }
        {
            const auto [r, g] = DrawAndReadRG(dev, sb, BlendState::AlphaBlend);
            check(r == 200 && g == 30, "AlphaBlend: premultiplied formula (src + dst*(1-srcAlpha)) (DX2-41)");
        }
        {
            const auto [r, g] = DrawAndReadRG(dev, sb, BlendState::NonPremultiplied);
            check(r == 78 && g == 30, "NonPremultiplied: straight-alpha formula (src*srcAlpha + dst*(1-srcAlpha)) (DX2-42)");
        }
        {
            const auto [r, g] = DrawAndReadRG(dev, sb, BlendState::Additive);
            check(r == 78 && g == 50, "Additive: saturating add, dst unattenuated (DX2-43)");
        }
        {
            BlendState custom;
            custom.setColorSourceBlendProperty(Blend::SourceColor);
            custom.setColorDestinationBlendProperty(Blend::One);
            custom.setAlphaSourceBlendProperty(Blend::SourceColor);
            custom.setAlphaDestinationBlendProperty(Blend::One);
            const auto [r, g] = DrawAndReadRG(dev, sb, custom);
            check(r == 157 && g == 50,
                  "Custom (non-preset) BlendState applies its own real factors "
                  "(SourceColor, One) directly, not a preset fallback");
        }
        {
            // D3D8 has no configurable blend equation (no D3DRS_BLENDOP), so a BlendState with
            // Opaque's exact factors (One, Zero) but BlendFunction::Subtract is genuinely
            // indistinguishable from real Opaque on this hardware -- correctly produces (200, 0).
            BlendState opaqueFactorsSubtractFunc;
            opaqueFactorsSubtractFunc.setColorSourceBlendProperty(Blend::One);
            opaqueFactorsSubtractFunc.setColorDestinationBlendProperty(Blend::Zero);
            opaqueFactorsSubtractFunc.setAlphaSourceBlendProperty(Blend::One);
            opaqueFactorsSubtractFunc.setAlphaDestinationBlendProperty(Blend::Zero);
            opaqueFactorsSubtractFunc.setColorBlendFunctionProperty(BlendFunction::Subtract);
            opaqueFactorsSubtractFunc.setAlphaBlendFunctionProperty(BlendFunction::Subtract);
            const auto [r, g] = DrawAndReadRG(dev, sb, opaqueFactorsSubtractFunc);
            check(r == 200 && g == 0,
                  "Opaque-matching factors with BlendFunction::Subtract genuinely produce "
                  "Opaque's own result -- D3D8 has no configurable blend equation to tell them apart");
        }

        std::printf("=== %d/%d PASS ===\n", passCount_, kTotal);
        result_ = (passCount_ == kTotal) ? 0 : 1;
        Exit();
    }

public:
    Dx8BlendTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kCanvasSize);
        gdm_->setPreferredBackBufferHeightProperty(kCanvasSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    Dx8BlendTest game;
    game.Run();
    return game.getResult();
}
