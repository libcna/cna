// SPDX-License-Identifier: MS-PL
// OPENGL1 renderer: constant blend color (plans/plan_opengl1.md item 17, EasyGL parity).
//
// Before this, SetBlendFactor() was a no-op and Blend.BlendFactor/InverseBlendFactor mapped to
// GL_ONE/GL_ZERO in BlendF() -- not a degraded case, a silently WRONG color: a game using
// GraphicsDevice.BlendFactor got GL_ONE (i.e. as if it had asked for Blend.One) instead.
//
// Uses the 3D draw path (BasicEffect + DrawUserPrimitives), not SpriteBatch --
// OpenGL1SpriteBatchRenderer::Begin() hardcodes its own glBlendFunc(GL_SRC_ALPHA,
// GL_ONE_MINUS_SRC_ALPHA) independent of whatever BlendState a game passes to SpriteBatch.Begin(),
// a separate pre-existing limitation unrelated to this item that would otherwise mask the very
// thing being tested here (confirmed empirically: an earlier version of this test using
// SpriteBatch always read back the SpriteBatch-hardcoded blend result regardless of BlendFactor).
//
// Method: BlendState.ColorSourceBlend=BlendFactor, ColorDestinationBlend=Zero,
// BlendFactor=(128,0,0,255). Drawing solid white (255,255,255) over a black backbuffer:
//   GL_ONE  fallback (the pre-existing bug): result stays white (255,255,255) -- BlendFactor is
//           silently ignored as if the source were used unmodified.
//   GL_ZERO fallback:                        result is black (0,0,0) -- also wrong, but a
//           different wrong answer, ruling out "coincidentally looks right for the wrong reason".
//   Correct GL_CONSTANT_COLOR:                result is white*BlendFactor = (~128,0,0), a distinct
//           third value only reachable if SetBlendFactor() and BlendF()'s new mapping both work.
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kViewport = 16;
}

class OpenGL1BlendFactorTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_ = false;
    int  pass_ = 0;
    int  fail_ = 0;

    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++pass_; else ++fail_;
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.SetDepthTestEnabled(false);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.Clear(Color::Black);

        BlendState bs;
        bs.setColorSourceBlendProperty(Blend::BlendFactor);
        bs.setColorDestinationBlendProperty(Blend::Zero);
        bs.setAlphaSourceBlendProperty(Blend::One);
        bs.setAlphaDestinationBlendProperty(Blend::Zero);
        bs.setColorBlendFunctionProperty(BlendFunction::Add);
        bs.setAlphaBlendFunctionProperty(BlendFunction::Add);
        bs.setBlendFactorProperty(Color(128, 0, 0, 255));
        dev.setBlendStateProperty(bs);

        Matrix world = Matrix::getIdentityProperty();
        Matrix view  = Matrix::getIdentityProperty();
        Matrix proj  = Matrix::CreateOrthographicOffCenter(0.0f, (float)kViewport, (float)kViewport, 0.0f, -1.0f, 1.0f);

        const VertexPositionColor q[6] = {
            { Vector3(0.0f, 0.0f, 0.0f), Color::White },
            { Vector3(0.0f, (float)kViewport, 0.0f), Color::White },
            { Vector3((float)kViewport, (float)kViewport, 0.0f), Color::White },
            { Vector3(0.0f, 0.0f, 0.0f), Color::White },
            { Vector3((float)kViewport, (float)kViewport, 0.0f), Color::White },
            { Vector3((float)kViewport, 0.0f, 0.0f), Color::White },
        };

        BasicEffect fx(dev);
        fx.setWorldProperty(world);
        fx.setViewProperty(view);
        fx.setProjectionProperty(proj);
        fx.VertexColorEnabled = true;
        fx.Apply();
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);

        const Rectangle centReg(kViewport / 2, kViewport / 2, 1, 1);
        Color centPx(0, 0, 0, 0);
        dev.GetBackBufferData(&centReg, &centPx, 0, 1);
        std::printf("centre=(%d,%d,%d)\n", centPx.getRProperty(), centPx.getGProperty(), centPx.getBProperty());

        Check(centPx.getRProperty() >= 110 && centPx.getRProperty() <= 145,
              "R channel scaled by BlendFactor.R (~128), not left unmodified (GL_ONE fallback "
              "would read 255) and not zeroed (GL_ZERO fallback would read 0)");
        Check(centPx.getGProperty() <= 10, "G channel scaled by BlendFactor.G=0 -- fully suppressed");
        Check(centPx.getBProperty() <= 10, "B channel scaled by BlendFactor.B=0 -- fully suppressed");

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    OpenGL1BlendFactorTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kViewport);
        gdm_->setPreferredBackBufferHeightProperty(kViewport);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    OpenGL1BlendFactorTest game;
    game.Run();
    return game.getResult();
}
