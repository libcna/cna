// SPDX-License-Identifier: MS-PL
// plans/plan_opengl2.md session 13: real GraphicsDevice.ReferenceStencil support
// (IGraphicsRenderer::SetReferenceStencil) -- previously a silent no-op on this renderer (the
// shared IGraphicsRenderer base class's own default), so changing ONLY ReferenceStencil between
// stencil passes (a common real XNA stencil-shadow-volume pattern) had no effect until the next
// full DepthStencilState re-application.
//
// Scene, all inside one RenderTarget2D with a real Depth24Stencil8 buffer:
//   1. "Stamp" pass (StencilFunction=Always, StencilPass=Replace, Reference=7): write stencil
//      value 7 into the LEFT half only. Right half stays 0 (RenderTarget2D::Clear zeroes it).
//   2. "Test" pass A (StencilFunction=Equal, StencilPass/Fail=Keep, Reference=7, set via a full
//      DepthStencilState): draw RED full-screen -- only the left half (stencil==7) passes.
//   3. Reference is then changed to 0 via the STANDALONE GraphicsDevice.ReferenceStencil
//      property -- deliberately NOT via a new DepthStencilState -- and a GREEN full-screen quad
//      is drawn with the SAME test DepthStencilState still active.
//
// If SetReferenceStencil() genuinely reached the GPU, only the RIGHT half (stencil==0) newly
// matches step 3's Reference=0 test, so it should turn GREEN while the LEFT half stays RED
// (Reference=0 no longer matches its stencil==7). If SetReferenceStencil() were a no-op, the GPU's
// real stencil reference value would still be the stale 7 from step 2's full DepthStencilState
// apply, so step 3's GREEN draw would incorrectly still match (and overwrite) the LEFT half
// instead, leaving the RIGHT half untouched.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/StencilOperation.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kRTSize = 32;

    bool CloseTo(int a, int b, int tol) { return std::abs(a - b) <= tol; }
    bool Matches(const Color& c, const Color& expected, int tol)
    {
        return CloseTo(c.getRProperty(), expected.getRProperty(), tol)
            && CloseTo(c.getGProperty(), expected.getGProperty(), tol)
            && CloseTo(c.getBProperty(), expected.getBProperty(), tol);
    }
    std::string ColorStr(const Color& c)
    {
        return "(" + std::to_string(c.getRProperty()) + "," + std::to_string(c.getGProperty())
             + "," + std::to_string(c.getBProperty()) + ")";
    }

    void DrawColorQuad(GraphicsDevice& dev, float xMin, float xMax, const Color& color)
    {
        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.Apply();

        const VertexPositionColor verts[6] = {
            {Vector3(xMin, 1.0f, 0.0f), color}, {Vector3(xMax, -1.0f, 0.0f), color}, {Vector3(xMin, -1.0f, 0.0f), color},
            {Vector3(xMin, 1.0f, 0.0f), color}, {Vector3(xMax, 1.0f, 0.0f), color},  {Vector3(xMax, -1.0f, 0.0f), color},
        };
        VertexBuffer vb(dev, VertexPositionColor::getVertexDeclarationStatic(), 6, BufferUsage::None);
        vb.SetData(verts, 0, 6);
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
    }
}

class OpenGL2ReferenceStencilTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<RenderTarget2D> rt_;
    int passCount_ = 0;
    int result_ = 1;

    void Check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        if (ok) ++passCount_;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();
        rt_ = std::make_unique<RenderTarget2D>(dev, kRTSize, kRTSize, false, SurfaceFormat::Color,
                                               DepthFormat::Depth24Stencil8, 0, RenderTargetUsage::DiscardContents);
    }

    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.setBlendStateProperty(BlendState::Opaque);

        dev.SetRenderTarget(rt_.get());
        dev.Clear(Color::Black); // also zeroes the stencil buffer (Task 928, see GraphicsDevice::Clear(Color)).

        // Step 1: stamp stencil=7 into the left half only.
        DepthStencilState stamp;
        stamp.setDepthBufferEnableProperty(false);
        stamp.setStencilEnableProperty(true);
        stamp.setStencilFunctionProperty(CompareFunction::Always);
        stamp.setStencilPassProperty(StencilOperation::Replace);
        stamp.setStencilFailProperty(StencilOperation::Replace);
        stamp.setReferenceStencilProperty(7);
        dev.setDepthStencilStateProperty(stamp);
        DrawColorQuad(dev, -1.0f, 0.0f, Color::Black);

        // Step 2: RED full-screen quad, stencil-tested Equal against Reference=7 (set via the
        // full DepthStencilState) -- only the left half (stencil==7) should pass.
        DepthStencilState test;
        test.setDepthBufferEnableProperty(false);
        test.setStencilEnableProperty(true);
        test.setStencilFunctionProperty(CompareFunction::Equal);
        test.setStencilPassProperty(StencilOperation::Keep);
        test.setStencilFailProperty(StencilOperation::Keep);
        test.setReferenceStencilProperty(7);
        dev.setDepthStencilStateProperty(test);
        DrawColorQuad(dev, -1.0f, 1.0f, Color::Red);

        // Step 3: change ONLY GraphicsDevice.ReferenceStencil (standalone, no new
        // DepthStencilState) to 0, then draw GREEN full-screen with the SAME `test` state still
        // active. This is exactly what SetReferenceStencil() must make take effect.
        dev.setReferenceStencilProperty(0);
        DrawColorQuad(dev, -1.0f, 1.0f, Color::Green);

        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        Color leftPx(0, 0, 0, 0), rightPx(0, 0, 0, 0);
        const Rectangle leftRect(kRTSize / 4, kRTSize / 2, 1, 1);
        const Rectangle rightRect(3 * kRTSize / 4, kRTSize / 2, 1, 1);
        rt_->GetData(0, &leftRect, &leftPx, 0, 1);
        rt_->GetData(0, &rightRect, &rightPx, 0, 1);

        Check(Matches(leftPx, Color::Red, 30),
              "left half (stencil==7) stays RED -- Reference=0 no longer matches it: got=" + ColorStr(leftPx));
        Check(Matches(rightPx, Color::Green, 30),
              "right half (stencil==0) turns GREEN -- standalone ReferenceStencil=0 took effect: got=" + ColorStr(rightPx));

        result_ = (passCount_ == 2) ? 0 : 1;
        Exit();
    }

public:
    OpenGL2ReferenceStencilTest()
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

    OpenGL2ReferenceStencilTest game;
    game.Run();
    return game.getResult();
}
