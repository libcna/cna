// SPDX-License-Identifier: MS-PL
// Task 755: verify BlendState::Additive both saturates correctly (no wraparound past 255) and
// ALWAYS adds the full destination regardless of source alpha, on Bgfx.
//
// Bgfx-specific copy of examples/easygl_blendstate_additive_test.cpp (Task 306, already reused
// verbatim on Vulkan). Not a verbatim reuse: that file calls the legacy
// GraphicsDevice::SetDepthTestEnabled(false) convenience method, which throws on Bgfx (a
// pre-existing, deliberate stub, not a bug introduced by this task). Replaced with the equivalent
// DepthStencilState-based call. Everything else is identical -- 1 Draw + 1 GetBackBufferData read
// per frame, so Bgfx's "first read per rendered frame" quirk (Task 406) does not apply.
//
// Additive (colorSourceBlend=alphaSourceBlend=SourceAlpha, colorDestinationBlend=
// alphaDestinationBlend=One) — unlike AlphaBlend/NonPremultiplied, destination factor is always
// One (not InverseSourceAlpha), so the destination is always fully added regardless of alpha.
//
// Background: Color(200, 50, 0, 255). Source (fully opaque): Color(255, 100, 0, 255).
// Expected, applying Additive's equation with alpha=255 (srcAlpha scale = 1.0):
//   R = src*1 + dst*1 = 255 + 200 = 455 -> clamps/saturates to 255 (no overflow/wraparound)
//   G = src*1 + dst*1 = 100 + 50  = 150 (NOT saturated - an exact-value check)
//   B = 0
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class BgfxBlendStateAdditiveTest : public Game
{
    bool done_   = false;
    int  result_ = 1;

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        const auto& vp = dev.getViewportProperty();

        dev.Clear(Color(200, 50, 0, 255)); // background to be added-into, not replaced
        DepthStencilState ds;
        ds.setDepthBufferEnableProperty(false);
        dev.setDepthStencilStateProperty(ds);
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
                        "       the destination was incorrectly dropped.\n",
                        got.getRProperty(), got.getGProperty(), got.getBProperty());
        }
        Exit();
    }

public:
    int getResult() const { return result_; }
};

int main()
{
    BgfxBlendStateAdditiveTest game;
    game.Run();
    return game.getResult();
}
