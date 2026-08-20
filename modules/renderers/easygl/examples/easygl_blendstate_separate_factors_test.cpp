// SPDX-License-Identifier: MS-PL
// Task 308: verify ColorSourceBlend/ColorDestinationBlend operate independently from
// AlphaSourceBlend/AlphaDestinationBlend — mirrors Task 307's approach (which tested
// ColorBlendFunction/AlphaBlendFunction), but for the blend *factors* instead. None of the 4
// static BlendState presets vary colour and alpha factors independently either (each preset uses
// the same factor pair for both channels), so this test builds two custom BlendStates:
//
//   Check A: ColorSourceBlend=One,  ColorDestinationBlend=Zero  (colour = source only)
//            AlphaSourceBlend=Zero, AlphaDestinationBlend=One   (alpha factors swapped vs. colour)
//   Check B: ColorSourceBlend=Zero, ColorDestinationBlend=One   (colour = destination only)
//            AlphaSourceBlend=One,  AlphaDestinationBlend=Zero  (alpha factors swapped again)
//
// Background: Color(50, 200, 0, 255). Source (opaque): Color(200, 50, 0, 255).
//   Check A expects pure SOURCE colour:      (200, 50, 0)
//   Check B expects pure DESTINATION colour: (50, 200, 0)
// The two expected results are deliberately opposite, and the alpha factors are swapped between
// the checks specifically to prove the colour channels only ever track the COLOUR factors — if
// alpha factors ever leaked into the colour computation, one of these two checks would show the
// wrong result.
//
// NOTE: Vulkan's ApplyBlendState (Task 868, see plans/plan_graphics.md) hardcodes colorSrcBlend=
// SourceAlpha, colorDstBlend=InverseSourceAlpha regardless of what's requested. At alpha=255 that
// collapses to "source only" (SourceAlpha=1, InverseSourceAlpha=0) — which happens to match Check
// A's expectation by coincidence, but NOT Check B's (which expects "destination only"). Expect
// Check A to pass and Check B to fail on Vulkan; do not read a passing Check A as evidence Task
// 868 is fixed.
//
// Exit code 0 = both checks PASS, 1 = either FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    BlendState MakeState(Blend colorSrc, Blend colorDst, Blend alphaSrc, Blend alphaDst)
    {
        BlendState bs;
        bs.setColorSourceBlendProperty(colorSrc);
        bs.setColorDestinationBlendProperty(colorDst);
        bs.setAlphaSourceBlendProperty(alphaSrc);
        bs.setAlphaDestinationBlendProperty(alphaDst);
        return bs;
    }
}

class BlendStateSeparateFactorsTest : public Game
{
    bool done_   = false;
    int  result_ = 1;

    Color DrawAndSample(GraphicsDevice& dev, const BlendState& state)
    {
        const auto& vp = dev.getViewportProperty();

        dev.Clear(Color(50, 200, 0, 255));
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(state);

        const Color kSource(200, 50, 0, 255);
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
        return got;
    }

protected:
    void Initialize() override { Game::Initialize(); }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();

        const Color a = DrawAndSample(dev, MakeState(Blend::One, Blend::Zero, Blend::Zero, Blend::One));
        const Color b = DrawAndSample(dev, MakeState(Blend::Zero, Blend::One, Blend::One, Blend::Zero));

        // Check A: colour must be pure source (200,50,0) regardless of the (swapped) alpha factors.
        const bool aOk = a.getRProperty() >= 185 && a.getRProperty() <= 215
                      && a.getGProperty() >= 35  && a.getGProperty() <= 65;

        // Check B: colour must be pure destination (50,200,0) regardless of the (swapped) alpha factors.
        const bool bOk = b.getRProperty() >= 35  && b.getRProperty() <= 65
                      && b.getGProperty() >= 185 && b.getGProperty() <= 215;

        std::printf("[%s] ColorSrc=One,ColorDst=Zero (alpha factors swapped): centre=(%d,%d,%d), expected ~(200,50,0)\n",
                    aOk ? "PASS" : "FAIL", a.getRProperty(), a.getGProperty(), a.getBProperty());
        std::printf("[%s] ColorSrc=Zero,ColorDst=One (alpha factors swapped): centre=(%d,%d,%d), expected ~(50,200,0)\n",
                    bOk ? "PASS" : "FAIL", b.getRProperty(), b.getGProperty(), b.getBProperty());

        if (!aOk || !bOk)
        {
            std::printf("[INFO] A mismatch here means ColorSourceBlend/ColorDestinationBlend are not\n"
                        "       independent from AlphaSourceBlend/AlphaDestinationBlend.\n");
        }

        result_ = (aOk && bOk) ? 0 : 1;
        Exit();
    }

public:
    int getResult() const { return result_; }
};

int main()
{
    BlendStateSeparateFactorsTest game;
    game.Run();
    return game.getResult();
}
