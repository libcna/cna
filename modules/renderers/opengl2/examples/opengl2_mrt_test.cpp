// SPDX-License-Identifier: MS-PL
// plans/plan_opengl2.md: real Multiple Render Targets (MRT) proof via SetRenderTargets.
//
// Until this task, IGraphicsRenderer::SetRenderTargets() was not overridden on OpenGL2 (the shared
// base-class default binds only rts[0] via SetRenderTarget2D and silently drops every other
// target). Implemented via a shared FBO re-attached on every call (glFramebufferTexture2D per
// target + glDrawBuffers), mirroring EasyGLRenderer::SetRenderTargets's own mrtFbo_
// precedent.
//
// This test deliberately does NOT port easygl_mrt_test.cpp's exact scene (draw only writes
// attachment 0 via a stock effect, attachment 1 is expected to keep its PRIOR content
// untouched). That relies on GLSL semantics EasyGL's GLES3/GL3+ target defines cleanly (no
// user-defined `out` variable for an attachment => that attachment is genuinely not written) but
// GLSL 1.10's gl_FragData[] model does NOT define the same guarantee for an unassigned index --
// the GLSL 1.10 spec calls an unassigned gl_FragData[n] "undefined", and this sandbox's real GL
// driver was empirically observed (via a diagnostic readback added while developing this test) to
// write black/zero into the unassigned attachment rather than preserve its prior content --
// contradicting the "stays untouched" assumption. That is a genuine GLSL-1.10-vs-GLES3/GL3+
// platform difference, not a bug in this renderer's SetRenderTargets/glDrawBuffers wiring.
//
// Instead, this test uses the pattern real MRT-using shaders actually rely on (G-buffer style:
// EVERY bound attachment is explicitly written in the same draw): a custom ShaderEffect whose
// fragment shader assigns gl_FragData[0] and gl_FragData[1] to two DIFFERENT colours in one draw
// call, proving genuine simultaneous multi-attachment output -- a stronger, more portable proof
// of real MRT than "one target's prior content survives an unrelated draw".
//
// Scene:
//   1. SetRenderTargets({rt0, rt1})
//   2. Draw a full-screen quad with a custom shader writing RED to gl_FragData[0] and BLUE to
//      gl_FragData[1] in the SAME fragment shader invocation
//   3. SetRenderTargets({}) -- back to default framebuffer
//   4. Blit rt0 to the left half, rt1 to the right half
//   5. Pixel readback: left quarter should be red, right quarter should be blue
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    const char* kMrtVert =
        "attribute vec3 aPosition;"
        "uniform mat4 World;uniform mat4 View;uniform mat4 Projection;"
        "void main(){gl_Position=Projection*View*World*vec4(aPosition,1.0);}";
    const char* kMrtFrag =
        "void main(){"
        "gl_FragData[0]=vec4(1.0,0.0,0.0,1.0);"  // attachment 0 (rt0) -> red
        "gl_FragData[1]=vec4(0.0,0.0,1.0,1.0);"  // attachment 1 (rt1) -> blue
        "}";
}

class OpenGL2MRTTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<RenderTarget2D> rt0_; // left  -- written red  (gl_FragData[0])
    std::unique_ptr<RenderTarget2D> rt1_; // right -- written blue (gl_FragData[1])
    bool done_   = false;
    int  result_ = 1;

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        rt0_ = std::make_unique<RenderTarget2D>(device, W, H);
        rt1_ = std::make_unique<RenderTarget2D>(device, W, H);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);

        device.SetRenderTargets({ RenderTargetBinding(rt0_.get()),
                                  RenderTargetBinding(rt1_.get()) });

        ShaderEffect fxMrt(device, kMrtVert, kMrtFrag);
        fxMrt.setWorldProperty(Matrix::getIdentityProperty());
        fxMrt.setViewProperty(Matrix::getIdentityProperty());
        fxMrt.setProjectionProperty(Matrix::getIdentityProperty());
        fxMrt.Apply();

        const VertexPositionColor quad[6] = {
            { Vector3(-1.0f,  1.0f, 0.0f), Color::White },
            { Vector3(-1.0f, -1.0f, 0.0f), Color::White },
            { Vector3( 1.0f, -1.0f, 0.0f), Color::White },
            { Vector3(-1.0f,  1.0f, 0.0f), Color::White },
            { Vector3( 1.0f, -1.0f, 0.0f), Color::White },
            { Vector3( 1.0f,  1.0f, 0.0f), Color::White },
        };
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);

        device.SetRenderTargets({});
        device.Clear(Color(0, 0, 0, 255));

        {
            BasicEffect blit(device);
            blit.VertexColorEnabled = false;
            blit.setTextureEnabledProperty(true);
            blit.setTextureProperty(rt0_.get());
            blit.setWorldProperty(Matrix::getIdentityProperty());
            blit.setViewProperty(Matrix::getIdentityProperty());
            blit.setProjectionProperty(Matrix::getIdentityProperty());
            blit.Apply();

            const VertexPositionTexture leftQuad[6] = {
                { Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.0f, 1.0f) },
                { Vector3(-1.0f, -1.0f, 0.0f), Vector2(0.0f, 0.0f) },
                { Vector3( 0.0f, -1.0f, 0.0f), Vector2(1.0f, 0.0f) },
                { Vector3(-1.0f,  1.0f, 0.0f), Vector2(0.0f, 1.0f) },
                { Vector3( 0.0f, -1.0f, 0.0f), Vector2(1.0f, 0.0f) },
                { Vector3( 0.0f,  1.0f, 0.0f), Vector2(1.0f, 1.0f) },
            };
            device.DrawUserPrimitives(PrimitiveType::TriangleList, leftQuad, 0, 2);
        }

        {
            BasicEffect blit(device);
            blit.VertexColorEnabled = false;
            blit.setTextureEnabledProperty(true);
            blit.setTextureProperty(rt1_.get());
            blit.setWorldProperty(Matrix::getIdentityProperty());
            blit.setViewProperty(Matrix::getIdentityProperty());
            blit.setProjectionProperty(Matrix::getIdentityProperty());
            blit.Apply();

            const VertexPositionTexture rightQuad[6] = {
                { Vector3( 0.0f,  1.0f, 0.0f), Vector2(0.0f, 1.0f) },
                { Vector3( 0.0f, -1.0f, 0.0f), Vector2(0.0f, 0.0f) },
                { Vector3( 1.0f, -1.0f, 0.0f), Vector2(1.0f, 0.0f) },
                { Vector3( 0.0f,  1.0f, 0.0f), Vector2(0.0f, 1.0f) },
                { Vector3( 1.0f, -1.0f, 0.0f), Vector2(1.0f, 0.0f) },
                { Vector3( 1.0f,  1.0f, 0.0f), Vector2(1.0f, 1.0f) },
            };
            device.DrawUserPrimitives(PrimitiveType::TriangleList, rightQuad, 0, 2);
        }

        const Rectangle leftReg ( W / 4,     H / 2, 1, 1);
        const Rectangle rightReg(3 * W / 4, H / 2, 1, 1);
        Color leftPx(0,0,0,0), rightPx(0,0,0,0);
        device.GetBackBufferData(&leftReg,  &leftPx,  0, 1);
        device.GetBackBufferData(&rightReg, &rightPx, 0, 1);

        const bool leftRed  = (leftPx.getRProperty()  >= 200 &&
                               leftPx.getGProperty()  <= 50  &&
                               leftPx.getBProperty()  <= 50);
        const bool rightBlue = (rightPx.getRProperty() <= 50  &&
                                rightPx.getGProperty() <= 50  &&
                                rightPx.getBProperty() >= 200);

        std::printf("[%s] MRT: left=(%d,%d,%d) [expect red], right=(%d,%d,%d) [expect blue]\n",
                    (leftRed && rightBlue) ? "PASS" : "FAIL",
                    leftPx.getRProperty(),  leftPx.getGProperty(),  leftPx.getBProperty(),
                    rightPx.getRProperty(), rightPx.getGProperty(), rightPx.getBProperty());

        result_ = (leftRed && rightBlue) ? 0 : 1;
        Exit();
    }

public:
    OpenGL2MRTTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    int getResult() const { return result_; }
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    OpenGL2MRTTest game;
    game.Run();
    return game.getResult();
}
