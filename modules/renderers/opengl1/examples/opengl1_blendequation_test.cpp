// SPDX-License-Identifier: MS-PL
// OPENGL1 renderer: blend equation beyond additive (plans/plan_opengl1.md item 18, EasyGL parity).
//
// Before this, ApplyBlendState() dropped colorBlendFunc/alphaBlendFunc entirely -- glBlendFunc
// always implied GL_FUNC_ADD regardless of what BlendState.ColorBlendFunction/AlphaBlendFunction
// actually requested.
//
// Uses the 3D draw path (BasicEffect + DrawUserPrimitives), not SpriteBatch -- see
// opengl1_blendfactor_test.cpp's own header for why (OpenGL1SpriteBatchRenderer::Begin() hardcodes
// its own additive-only glBlendFunc, independent of any BlendState a game passes in).
//
// Method: draw an opaque destination quad (200,50,50), then a second quad (50,20,20) with
// ColorSourceBlend=One, ColorDestinationBlend=One, ColorBlendFunction=ReverseSubtract
// ("dest - source" per BlendFunction's own doc comment). Correct result: (200-50, 50-20, 50-20)
// = (150,30,30). The pre-existing bug (always GL_FUNC_ADD) would instead give (200+50, 50+20,
// 50+20) = (250,70,70) -- clearly distinguishable from the correct subtractive result.
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

    Matrix World() { return Matrix::getIdentityProperty(); }
    Matrix View()  { return Matrix::getIdentityProperty(); }
    Matrix Proj()  { return Matrix::CreateOrthographicOffCenter(0.0f, (float)kViewport, (float)kViewport, 0.0f, -1.0f, 1.0f); }
}

class OpenGL1BlendEquationTest : public Game
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

    void DrawFullQuad(GraphicsDevice& dev, const Color& c)
    {
        const VertexPositionColor q[6] = {
            { Vector3(0.0f, 0.0f, 0.0f), c }, { Vector3(0.0f, (float)kViewport, 0.0f), c },
            { Vector3((float)kViewport, (float)kViewport, 0.0f), c }, { Vector3(0.0f, 0.0f, 0.0f), c },
            { Vector3((float)kViewport, (float)kViewport, 0.0f), c }, { Vector3((float)kViewport, 0.0f, 0.0f), c },
        };
        BasicEffect fx(dev);
        fx.setWorldProperty(World()); fx.setViewProperty(View()); fx.setProjectionProperty(Proj());
        fx.VertexColorEnabled = true;
        fx.Apply();
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);
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

        dev.setBlendStateProperty(BlendState::Opaque);
        DrawFullQuad(dev, Color(200, 50, 50, 255));

        BlendState bs;
        bs.setColorSourceBlendProperty(Blend::One);
        bs.setColorDestinationBlendProperty(Blend::One);
        bs.setColorBlendFunctionProperty(BlendFunction::ReverseSubtract);
        bs.setAlphaSourceBlendProperty(Blend::One);
        bs.setAlphaDestinationBlendProperty(Blend::Zero);
        bs.setAlphaBlendFunctionProperty(BlendFunction::Add);
        dev.setBlendStateProperty(bs);
        DrawFullQuad(dev, Color(50, 20, 20, 255));

        const Rectangle centReg(kViewport / 2, kViewport / 2, 1, 1);
        Color centPx(0, 0, 0, 0);
        dev.GetBackBufferData(&centReg, &centPx, 0, 1);
        std::printf("centre=(%d,%d,%d)\n", centPx.getRProperty(), centPx.getGProperty(), centPx.getBProperty());

        Check(centPx.getRProperty() >= 140 && centPx.getRProperty() <= 160,
              "R = dest(200) - src(50) = ~150, not dest+src=250 (the pre-existing always-Add bug)");
        Check(centPx.getGProperty() >= 20 && centPx.getGProperty() <= 40,
              "G = dest(50) - src(20) = ~30, not dest+src=70");
        Check(centPx.getBProperty() >= 20 && centPx.getBProperty() <= 40,
              "B = dest(50) - src(20) = ~30, not dest+src=70");

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    OpenGL1BlendEquationTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kViewport);
        gdm_->setPreferredBackBufferHeightProperty(kViewport);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    OpenGL1BlendEquationTest game;
    game.Run();
    return game.getResult();
}
