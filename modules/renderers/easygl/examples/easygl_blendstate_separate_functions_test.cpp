// SPDX-License-Identifier: MS-PL
// Task 307: verify ColorBlendFunction and AlphaBlendFunction operate independently — setting one
// must not affect the other. None of the 4 static BlendState presets vary BlendFunction from the
// default Add, so this test builds two CUSTOM BlendStates (colorSrc=alphaSrc=colorDst=alphaDst=One,
// so blend *factors* don't complicate the *function* check):
//
//   Check A: ColorBlendFunction=Subtract, AlphaBlendFunction=Add    (opposite of each other)
//   Check B: ColorBlendFunction=Add,      AlphaBlendFunction=Subtract (swapped)
//
// Background: Color(50, 200, 0, 255). Source (opaque): Color(200, 50, 0, 255).
//   Check A expects Subtract's colour maths (src*1 - dst*1, clamped): R=200-50=150, G=50-200 -> 0.
//   Check B expects Add's colour maths (src*1 + dst*1):               R=200+50=250, G=50+200=250.
// Both checks only ever read back RGB (this project has no established, verified alpha-channel
// backbuffer-readback pattern to rely on) — but together they still prove independence: if
// AlphaBlendFunction ever leaked into the colour computation (or vice versa), Check A would
// incorrectly show Add's result and/or Check B would incorrectly show Subtract's result.
//
// NOTE: Vulkan's ApplyBlendState (see plans/plan_graphics.md Task 868) takes colorBlendFunc/
// alphaBlendFunc as unused parameters (commented out in the signature) and always hardcodes
// VK_BLEND_OP_ADD — so Check A (expecting Subtract) is expected to fail there, while Check B
// (expecting Add) is expected to coincidentally pass, mirroring the Task 305/306 pattern.
//
// Exit code 0 = both checks PASS, 1 = either FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
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

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

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

class BlendStateSeparateFunctionsTest : public Game
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
    int getResult() const { return result_; }
};

int main()
{
    BlendStateSeparateFunctionsTest game;
    game.Run();
    return game.getResult();
}
