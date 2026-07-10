// SPDX-License-Identifier: MS-PL
// Task 923: verify ColorBlendFunction and AlphaBlendFunction operate independently on Bgfx — a
// Bgfx-specific adaptation of examples/easygl_blendstate_separate_functions_test.cpp (Task 307,
// already reused verbatim on Vulkan for Task 868). This is genuinely discriminating for Bgfx's
// own gap (split from Task 868's original diagnosis): BgfxGraphicsBackend::ApplyBlendState always
// implicitly used BGFX_STATE_BLEND_EQUATION_ADD regardless of the requested colorBlendFunc/
// alphaBlendFunc, which this test's own Check A (ColorBlendFunction=Subtract) directly exposes.
//
// Not a verbatim reuse of the EasyGL source because that file calls the low-level
// GraphicsDevice.SetDepthTestEnabled(false), which is an unconditional throw on Bgfx
// (BgfxGraphicsBackend::SetDepthTestEnabled is a deliberate ThrowNo3DState() stub — a pre-existing,
// documented gap, not part of this task's scope). Safely omitted here instead: each DrawAndSample
// call clears the backbuffer and draws exactly one full-screen quad, so whether depth testing is
// enabled has no effect on the result regardless.
//
// None of the 4 static BlendState presets vary BlendFunction from the default Add, so this test
// builds two CUSTOM BlendStates (colorSrc=alphaSrc=colorDst=alphaDst=One, so blend *factors*
// don't complicate the *function* check):
//
//   Check A: ColorBlendFunction=Subtract, AlphaBlendFunction=Add    (opposite of each other)
//   Check B: ColorBlendFunction=Add,      AlphaBlendFunction=Subtract (swapped)
//
// Background: Color(50, 200, 0, 255). Source (opaque): Color(200, 50, 0, 255).
//   Check A expects Subtract's colour maths (src*1 - dst*1, clamped): R=200-50=150, G=50-200 -> 0.
//   Check B expects Add's colour maths (src*1 + dst*1):               R=200+50=250, G=50+200=250.
//
// Exit code 0 = both checks PASS, 1 = either FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kSize = 64;

namespace
{
    BlendState MakeState(BlendFunction colorFunc, BlendFunction alphaFunc)
    {
        BlendState bs;
        bs.setColorSourceBlendProperty(Blend::One);
        bs.setColorDestinationBlendProperty(Blend::One);
        bs.setAlphaSourceBlendProperty(Blend::One);
        bs.setAlphaDestinationBlendProperty(Blend::One);
        bs.setColorBlendFunctionProperty(colorFunc);
        bs.setAlphaBlendFunctionProperty(alphaFunc);
        return bs;
    }
}

class BgfxBlendStateSeparateFunctionsTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int result_ = 1;

    Color DrawAndSample(GraphicsDevice& dev, const BlendState& state)
    {
        Color got(0, 0, 0, 0);
        for (int i = 0; i < 20; ++i)
        {
            dev.Clear(Color(50, 200, 0, 255));
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

            // Task 896 finding: this quad's winding is CCW/back-facing under CNA's real default
            // RasterizerState on Bgfx specifically — needs CullNone.
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);

            const Rectangle reg(kSize / 2, kSize / 2, 1, 1);
            dev.GetBackBufferData(&reg, &got, 0, 1);
            if (got.getRProperty() != 0 || got.getGProperty() != 0 || got.getBProperty() != 0)
                break;  // Bgfx's GetBackBufferData only reliably reflects the first read per
                        // rendered frame (Task 406 finding).
        }
        return got;
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();

        const Color subA = DrawAndSample(dev, MakeState(BlendFunction::Subtract, BlendFunction::Add));
        const Color addB = DrawAndSample(dev, MakeState(BlendFunction::Add, BlendFunction::Subtract));

        // Check A: colour must follow Subtract (200-50=150, 50-200 clamps to 0) regardless of
        // AlphaBlendFunction=Add.
        const bool aOk = subA.getRProperty() >= 135 && subA.getRProperty() <= 165
                      && subA.getGProperty() <= 20;

        // Check B: colour must follow Add (200+50=250, 50+200=250) regardless of
        // AlphaBlendFunction=Subtract.
        const bool bOk = addB.getRProperty() >= 235
                      && addB.getGProperty() >= 235;

        std::printf("[%s] ColorBlendFunction=Subtract, AlphaBlendFunction=Add: centre=(%d,%d,%d), expected ~(150,0,0)\n",
                    aOk ? "PASS" : "FAIL", subA.getRProperty(), subA.getGProperty(), subA.getBProperty());
        std::printf("[%s] ColorBlendFunction=Add, AlphaBlendFunction=Subtract: centre=(%d,%d,%d), expected ~(250,250,0)\n",
                    bOk ? "PASS" : "FAIL", addB.getRProperty(), addB.getGProperty(), addB.getBProperty());

        if (!aOk || !bOk)
        {
            std::printf("[INFO] A mismatch here means ColorBlendFunction and AlphaBlendFunction are not\n"
                        "       independent - one leaked into the other's computation.\n");
        }

        result_ = (aOk && bOk) ? 0 : 1;
        Exit();
    }

public:
    BgfxBlendStateSeparateFunctionsTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    BgfxBlendStateSeparateFunctionsTest game;
    game.Run();
    return game.getResult();
}
