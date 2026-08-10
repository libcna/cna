// SPDX-License-Identifier: MS-PL
// Task 304: verify BlendState::AlphaBlend implements the premultiplied-alpha blend equation
// literally (colorSourceBlend=alphaSourceBlend=One, colorDestinationBlend=alphaDestinationBlend=
// InverseSourceAlpha) — i.e. it expects an ALREADY-premultiplied source colour and must NOT
// multiply by alpha itself. That's BlendState::NonPremultiplied's job (SourceAlpha/
// InverseSourceAlpha, Task 305), a different equation entirely.
//
// Source colour: Color(128, 0, 0, 128) — a correctly premultiplied 50%-alpha red (raw red
// (255,0,0) at ~50% opacity, premultiplied: 255*0.5≈128). Background: green (0,255,0,255).
//
// Expected output, applying AlphaBlend's equation directly:
//   R = fragR*1 + destR*(1 - fragA/255) = 128*1 + 0*(1-128/255)   ≈ 128
//   G = fragG*1 + destG*(1 - fragA/255) = 0*1   + 255*(1-128/255) ≈ 127
//   B ≈ 0
// Both R and G land in the SAME tight mid-range band (~110-145). Critically, R must be ~128, NOT
// ~64 — a result of ~64 would mean the shader incorrectly multiplied the already-premultiplied
// source by alpha again (i.e. accidentally implemented NonPremultiplied's SourceAlpha equation
// instead of AlphaBlend's plain One). That distinguishing check is this test's actual point.
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

class BlendStateAlphaBlendTest : public Game
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

        dev.Clear(Color(0, 255, 0, 255)); // green background
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::AlphaBlend);

        const Color kPremultipliedRed(128, 0, 0, 128); // raw red at ~50% opacity, premultiplied
        const VertexPositionColor verts[6] = {
            { Vector3(-1.0f,  1.0f, 0.0f), kPremultipliedRed },
            { Vector3(-1.0f, -1.0f, 0.0f), kPremultipliedRed },
            { Vector3( 1.0f, -1.0f, 0.0f), kPremultipliedRed },
            { Vector3(-1.0f,  1.0f, 0.0f), kPremultipliedRed },
            { Vector3( 1.0f, -1.0f, 0.0f), kPremultipliedRed },
            { Vector3( 1.0f,  1.0f, 0.0f), kPremultipliedRed },
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
        const bool notDoubleMultiplied = got.getRProperty() >= 100; // rules out the ~64 double-multiply result

        const bool pass = rInBand && gInBand && notDoubleMultiplied;

        if (pass)
        {
            std::printf("[PASS] BlendState::AlphaBlend premultiplied blend: centre=(%d,%d,%d), expected ~(128,127,0)\n",
                        got.getRProperty(), got.getGProperty(), got.getBProperty());
            result_ = 0;
        }
        else
        {
            std::printf("[FAIL] BlendState::AlphaBlend: centre=(%d,%d,%d), expected R~128 G~127 B~0.\n"
                        "       R<100 would mean the source was incorrectly multiplied by alpha again\n"
                        "       (NonPremultiplied's equation applied instead of AlphaBlend's).\n",
                        got.getRProperty(), got.getGProperty(), got.getBProperty());
        }
        Exit();
    }

public:
    int getResult() const { return result_; }
};

int main()
{
    BlendStateAlphaBlendTest game;
    game.Run();
    return game.getResult();
}
