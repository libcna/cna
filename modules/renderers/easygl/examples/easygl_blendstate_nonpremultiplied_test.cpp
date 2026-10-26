// SPDX-License-Identifier: MS-PL
// Task 305: verify BlendState::NonPremultiplied implements the raw-alpha blend equation
// (colorSourceBlend=alphaSourceBlend=SourceAlpha, colorDestinationBlend=alphaDestinationBlend=
// InverseSourceAlpha) — i.e. it multiplies a RAW (non-premultiplied) source colour by alpha
// itself, unlike BlendState::AlphaBlend (Task 304), which expects the caller to have already
// premultiplied the source.
//
// Caveat carried from Task 304/868 (history): the retired VULKAN renderer's ApplyBlendState ignored
// every blend-factor/function request and hardcoded one equation whose COLOR-channel factors
// (SourceAlpha/InverseSourceAlpha) match NonPremultiplied's by coincidence, so a passing Vulkan
// result was never evidence that Task 868 was fixed. See graphics Task 868.
//
// Source colour: Color(255, 0, 0, 128) — RAW (non-premultiplied) red at ~50% opacity. Background:
// green (0,255,0,255).
//
// Expected output, applying NonPremultiplied's equation directly:
//   R = fragR*(fragA/255) + destR*(1-fragA/255) = 255*0.502 + 0*0.498   ≈ 128
//   G = fragG*(fragA/255) + destG*(1-fragA/255) = 0*0.502   + 255*0.498 ≈ 127
//   B ≈ 0
// Numerically similar to Task 304's expected AlphaBlend result — the two tasks together prove
// *which* stage (caller vs. pipeline) performs the alpha multiplication, not just that "some blend
// happened."
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
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

class BlendStateNonPremultipliedTest : public Game
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
    BlendStateNonPremultipliedTest game;
    GraphicsDeviceManager testGraphicsDeviceManager(&game);
    testGraphicsDeviceManager.setGraphicsProfileProperty(GraphicsProfile::HiDef);
    game.Run();
    return game.getResult();
}
