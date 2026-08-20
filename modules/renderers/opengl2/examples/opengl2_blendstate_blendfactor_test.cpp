// SPDX-License-Identifier: MS-PL
// plans/plan_opengl2.md: proof that BlendState.BlendFactor (Blend::BlendFactor /
// Blend::InverseBlendFactor) actually reaches the GPU on OpenGL2 -- reuses
// examples/easygl_blendstate_blendfactor_test.cpp's own scene and expected values verbatim.
//
// Until this task, OpenGL2Renderer mapped Blend::BlendFactor/InverseBlendFactor to
// GL_CONSTANT_COLOR/GL_ONE_MINUS_CONSTANT_COLOR (ToGLBlendFactor) but never called
// glBlendColor(), so the GL constant-color register stayed at its GL default of (0,0,0,0)
// regardless of what BlendState.BlendFactor was set to -- IGraphicsRenderer::SetBlendFactor()
// was never overridden and silently no-op'd via the shared base-class default. Fixed by
// overriding SetBlendFactor() to call glBlendColor(r,g,b,a).
//
// Background: Color(0, 0, 0, 255). Source: Color(255, 255, 255, 255) (all-ones, so
// ColorSourceBlend=Blend::BlendFactor * source == the BlendFactor constant exactly).
// Expected result: centre pixel ~= (200, 100, 0), i.e. the state's own BlendFactor, NOT
// GL's default constant-color of (0,0,0,0) (which would read back black).
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
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

#include "common/PixelTestGame.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class OpenGL2BlendStateBlendFactorTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_   = false;
    int  result_ = 1;

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        const auto& vp = dev.getViewportProperty();

        dev.Clear(Color(0, 0, 0, 255));
        dev.SetDepthTestEnabled(false);

        BlendState state;
        state.setColorSourceBlendProperty(Blend::BlendFactor);
        state.setColorDestinationBlendProperty(Blend::Zero);
        state.setAlphaSourceBlendProperty(Blend::One);
        state.setAlphaDestinationBlendProperty(Blend::Zero);
        state.setBlendFactorProperty(Color(200, 100, 0, 255));

        // Deliberately no separate dev.setBlendFactorProperty(...) call: the whole point of this
        // test is that setBlendStateProperty alone must propagate the state's own BlendFactor.
        dev.setBlendStateProperty(state);

        const Color kSource(255, 255, 255, 255);
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

        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);

        const Rectangle reg(vp.getWidthProperty() / 2, vp.getHeightProperty() / 2, 1, 1);
        Color got(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &got, 0, 1);

        const bool ok = got.getRProperty() >= 185 && got.getRProperty() <= 215
                     && got.getGProperty() >= 85  && got.getGProperty() <= 115
                     && got.getBProperty() <= 15;

        std::printf("[%s] centre=(%d,%d,%d), expected ~(200,100,0)\n",
                    ok ? "PASS" : "FAIL", got.getRProperty(), got.getGProperty(), got.getBProperty());

        result_ = ok ? 0 : 1;
        Exit();
    }

public:
    OpenGL2BlendStateBlendFactorTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    int getResult() const { return result_; }
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    OpenGL2BlendStateBlendFactorTest game;
    game.Run();
    return game.getResult();
}
