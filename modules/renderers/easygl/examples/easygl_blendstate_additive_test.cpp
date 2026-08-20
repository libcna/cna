// SPDX-License-Identifier: MS-PL
// Task 306: verify BlendState::Additive (colorSourceBlend=alphaSourceBlend=SourceAlpha,
// colorDestinationBlend=alphaDestinationBlend=One) both saturates correctly (no wraparound past
// 255) and — unlike AlphaBlend/NonPremultiplied — ALWAYS adds the full destination regardless of
// source alpha (colorDst=One, not InverseSourceAlpha).
//
// Background: Color(200, 50, 0, 255). Source (fully opaque): Color(255, 100, 0, 255).
// Expected, applying Additive's equation with alpha=255 (srcAlpha scale = 1.0):
//   R = src*1 + dst*1 = 255 + 200 = 455 -> clamps/saturates to 255 (no overflow/wraparound)
//   G = src*1 + dst*1 = 100 + 50  = 150 (NOT saturated - an exact-value check)
//   B = 0
//
// This test also happens to re-expose Task 868 (Vulkan's blend state is almost entirely fake —
// see plans/plan_graphics.md) for real this time, unlike Task 305's NonPremultiplied test, which
// coincidentally passed on Vulkan. Task 868's hardcoded equation uses InverseSourceAlpha for the
// destination factor; at alpha=255 that evaluates to (1 - 1) = 0, meaning the destination would be
// completely dropped instead of fully added. That gives a WRONG G of ~100 (source only) instead of
// the correct 150 (source + destination) — a clean, unambiguous, non-coincidental failure signal.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class BlendStateAdditiveTest : public Game
{
    bool done_   = false;
    int  result_ = 1;

protected:
    void Initialize() override { Game::Initialize(); }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        const auto& vp = dev.getViewportProperty();

        dev.Clear(Color(200, 50, 0, 255)); // background to be added-into, not replaced
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Additive);

        const Color kSource(255, 100, 0, 255);
        const VertexPositionColor verts[6] = {
            { Vector3(-1.0f,  1.0f, 0.0f), kSource },
            { Vector3(-1.0f, -1.0f, 0.0f), kSource },
            { Vector3( 1.0f, -1.0f, 0.0f), kSource },
            { Vector3(-1.0f,  1.0f, 0.0f), kSource },
            { Vector3( 1.0f, -1.0f, 0.0f), kSource },
            { Vector3( 1.0f,  1.0f, 0.0f), kSource },
        };

        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.Apply();

        // Task 896 finding: this quad's winding is CCW/back-facing under CNA's real
        // default RasterizerState — needs CullNone.
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);

        const Rectangle reg(vp.getWidthProperty() / 2, vp.getHeightProperty() / 2, 1, 1);
        Color got(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &got, 0, 1);

        const bool rSaturated  = got.getRProperty() >= 250; // 255+200 must clamp, not wrap
        const bool gAddsFully  = got.getGProperty() >= 140 && got.getGProperty() <= 160; // 100+50=150

        const bool pass = rSaturated && gAddsFully;

        if (pass)
        {
            std::printf("[PASS] BlendState::Additive: centre=(%d,%d,%d), expected ~(255,150,0)\n",
                        got.getRProperty(), got.getGProperty(), got.getBProperty());
            result_ = 0;
        }
        else
        {
            std::printf("[FAIL] BlendState::Additive: centre=(%d,%d,%d), expected R>=250 (saturated)\n"
                        "       and G in [140,160] (100+50, destination fully added). G~100 would mean\n"
                        "       the destination was incorrectly dropped (Task 868's hardcoded equation).\n",
                        got.getRProperty(), got.getGProperty(), got.getBProperty());
        }
        Exit();
    }

public:
    int getResult() const { return result_; }
};

int main()
{
    BlendStateAdditiveTest game;
    game.Run();
    return game.getResult();
}
