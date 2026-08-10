// SPDX-License-Identifier: MS-PL
// Task 754: verify BlendState::NonPremultiplied implements the raw-alpha blend equation on Bgfx.
//
// Bgfx-specific copy of examples/easygl_blendstate_nonpremultiplied_test.cpp (Task 305, already
// reused verbatim on Vulkan). Not a verbatim reuse: that file calls the legacy
// GraphicsDevice::SetDepthTestEnabled(false) convenience method, which throws on Bgfx (a
// pre-existing, deliberate stub, not a bug introduced by this task). Replaced with the equivalent
// DepthStencilState-based call. Everything else is identical -- 1 Draw + 1 GetBackBufferData read
// per frame, so Bgfx's "first read per rendered frame" quirk (Task 406) does not apply.
//
// NonPremultiplied (colorSourceBlend=alphaSourceBlend=SourceAlpha, colorDestinationBlend=
// alphaDestinationBlend=InverseSourceAlpha) multiplies a RAW (non-premultiplied) source colour by
// alpha itself, unlike BlendState::AlphaBlend, which expects an already-premultiplied source.
//
// Source colour: Color(255, 0, 0, 128) — RAW (non-premultiplied) red at ~50% opacity. Background:
// green (0,255,0,255).
//
// Expected output, applying NonPremultiplied's equation directly:
//   R = fragR*(fragA/255) + destR*(1-fragA/255) = 255*0.502 + 0*0.498   ≈ 128
//   G = fragG*(fragA/255) + destG*(1-fragA/255) = 0*0.502   + 255*0.498 ≈ 127
//   B ≈ 0
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

class BgfxBlendStateNonPremultipliedTest : public Game
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

        dev.Clear(Color(0, 255, 0, 255)); // green background
        DepthStencilState ds;
        ds.setDepthBufferEnableProperty(false);
        dev.setDepthStencilStateProperty(ds);
        dev.setBlendStateProperty(BlendState::NonPremultiplied);

        const Color kRawRed(255, 0, 0, 128); // raw red at ~50% opacity, NOT premultiplied
        const VertexPositionColor verts[6] = {
            { Vector3(-1.0f,  1.0f, 0.0f), kRawRed },
            { Vector3(-1.0f, -1.0f, 0.0f), kRawRed },
            { Vector3( 1.0f, -1.0f, 0.0f), kRawRed },
            { Vector3(-1.0f,  1.0f, 0.0f), kRawRed },
            { Vector3( 1.0f, -1.0f, 0.0f), kRawRed },
            { Vector3( 1.0f,  1.0f, 0.0f), kRawRed },
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

        const bool rInBand = got.getRProperty() >= 110 && got.getRProperty() <= 145;
        const bool gInBand = got.getGProperty() >= 110 && got.getGProperty() <= 145;
        const bool pass = rInBand && gInBand;

        if (pass)
        {
            std::printf("[PASS] BlendState::NonPremultiplied raw-alpha blend: centre=(%d,%d,%d), expected ~(128,127,0)\n",
                        got.getRProperty(), got.getGProperty(), got.getBProperty());
            result_ = 0;
        }
        else
        {
            std::printf("[FAIL] BlendState::NonPremultiplied: centre=(%d,%d,%d), expected R~128 G~127 B~0.\n",
                        got.getRProperty(), got.getGProperty(), got.getBProperty());
        }
        Exit();
    }

public:
    int getResult() const { return result_; }
};

int main()
{
    BgfxBlendStateNonPremultipliedTest game;
    game.Run();
    return game.getResult();
}
