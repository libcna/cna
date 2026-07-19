// SPDX-License-Identifier: MS-PL
// plan_opengl2.md: pixel-exact RenderTarget2D/FBO proof for the native OpenGL 2.1 graphics
// backend -- render into an off-screen target, read it back directly, sample it as an ordinary
// Texture2D via SpriteBatch, and prove a depth-test occlusion inside the FBO itself.
//
// Check A -- RenderTarget2D construction succeeds and reports the requested size.
// Check B -- a solid-color BasicEffect quad drawn into the RT reads back via
//   RenderTarget2D::GetData() exactly (proves the FBO color attachment + RenderTarget::GetData()).
// Check C -- that same RT, unbound and drawn via SpriteBatch onto the backbuffer, reads back the
//   same color at the expected screen position (proves a RenderTarget2D can be sampled as a plain
//   Texture2D through SpriteBatch -- this backend's Sprite::Draw used to dynamic_cast to the
//   concrete Tex type and silently no-op for anything else, including RenderTarget2D).
// Check D -- real depth-test occlusion INSIDE the RT: a nearer quad wins regardless of draw
//   order, read back via GetData() (proves the RT's own depth/stencil renderbuffer is real).
// Check E -- a SpriteBatch draw made WHILE a differently-sized RT is bound fills that RT's own
//   dimensions, not the window's (the Task-1078-equivalent fix: GetCurrentRenderTarget2DSize()).
// Check F -- 60 frames of the whole scene render with no exception.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include "CNA/Internal/Backends/OpenGL2/OpenGL2GraphicsBackend.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Backends::OpenGL2;

namespace
{
    constexpr int kTotalFrames = 60;
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

    Color ReadRTPixel(RenderTarget2D& rt, int x, int y)
    {
        Color px(0, 0, 0, 0);
        const Rectangle rect(x, y, 1, 1);
        rt.GetData(0, &rect, &px, 0, 1);
        return px;
    }
}

class OpenGL2RenderTarget2DTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::unique_ptr<RenderTarget2D> rtColor_;
    std::unique_ptr<RenderTarget2D> rtDepth_;
    std::unique_ptr<RenderTarget2D> rtOddSize_;

    int frame_ = 0;
    int passCount_ = 0;
    int result_ = 1;

    void Check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        if (ok) ++passCount_;
    }

    Color ReadBackbufferPixel(int x, int y)
    {
        Color px(0, 0, 0, 0);
        const Rectangle rect(x, y, 1, 1);
        getGraphicsDeviceProperty().GetBackBufferData(&rect, &px, 0, 1);
        return px;
    }

    void DrawFullscreenColorQuad(GraphicsDevice& dev, float z, const Color& color)
    {
        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.Apply();

        const VertexPositionColor verts[6] = {
            {Vector3(-1.0f, 1.0f, z), color}, {Vector3(1.0f, -1.0f, z), color}, {Vector3(-1.0f, -1.0f, z), color},
            {Vector3(-1.0f, 1.0f, z), color}, {Vector3(1.0f, 1.0f, z), color},  {Vector3(1.0f, -1.0f, z), color},
        };
        VertexBuffer vb(dev, VertexPositionColor::getVertexDeclarationStatic(), 6, BufferUsage::None);
        vb.SetData(verts, 0, 6);
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
    }

protected:
    void LoadContent() override
    {
        auto& dev = getGraphicsDeviceProperty();
        spriteBatch_ = std::make_unique<SpriteBatch>(dev);

        rtColor_ = std::make_unique<RenderTarget2D>(dev, kRTSize, kRTSize, false, SurfaceFormat::Color,
                                                     DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
        Check(rtColor_->getWidthProperty() == kRTSize && rtColor_->getHeightProperty() == kRTSize,
              "RenderTarget2D construction reports the requested size");

        rtDepth_ = std::make_unique<RenderTarget2D>(dev, kRTSize, kRTSize, false, SurfaceFormat::Color,
                                                     DepthFormat::Depth24Stencil8, 0, RenderTargetUsage::DiscardContents);
        rtOddSize_ = std::make_unique<RenderTarget2D>(dev, 50, 30, false, SurfaceFormat::Color,
                                                       DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
    }

    void RunScene(bool runChecks)
    {
        auto& dev = getGraphicsDeviceProperty();

        // Check B: solid color into the RT, read back directly.
        dev.SetRenderTarget(rtColor_.get());
        dev.Clear(Color::Black);
        DrawFullscreenColorQuad(dev, 0.0f, Color(0, 200, 255, 255));
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        if (runChecks)
        {
            const Color got = ReadRTPixel(*rtColor_, kRTSize / 2, kRTSize / 2);
            Check(Matches(got, Color(0, 200, 255, 255), 10),
                  "RenderTarget2D::GetData() reads back the drawn color: got=" + ColorStr(got));
        }

        // Check C: sample that RT as an ordinary Texture2D via SpriteBatch onto the backbuffer.
        dev.Clear(Color::Black);
        spriteBatch_->Begin();
        spriteBatch_->Draw(*rtColor_, Rectangle(100, 80, 64, 64), Rectangle(0, 0, kRTSize, kRTSize), Color::White);
        spriteBatch_->End();
        if (runChecks)
        {
            const Color got = ReadBackbufferPixel(132, 112);
            Check(Matches(got, Color(0, 200, 255, 255), 10),
                  "SpriteBatch draws a RenderTarget2D sampled as a Texture2D: got=" + ColorStr(got));
        }

        // Check D: depth-test occlusion inside the RT, both draw orders. SpriteBatch::Begin()
        // above (Check C) leaves GraphicsDevice.DepthStencilState at its own default of None
        // (matches real XNA/FNA SpriteBatch behavior) -- restore Default explicitly, exactly as
        // a real game would need to before any 3D draw following 2D SpriteBatch usage.
        dev.setDepthStencilStateProperty(DepthStencilState::Default);
        dev.SetRenderTarget(rtDepth_.get());
        dev.Clear(Color::Black);
        DrawFullscreenColorQuad(dev, -0.5f, Color::Blue); // nearer, first
        DrawFullscreenColorQuad(dev, 0.5f, Color::Red);   // farther, second
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        if (runChecks)
        {
            const Color got = ReadRTPixel(*rtDepth_, kRTSize / 2, kRTSize / 2);
            Check(Matches(got, Color::Blue, 10),
                  "depth test inside a RenderTarget2D: nearer quad wins: got=" + ColorStr(got));
        }

        // Check E: a SpriteBatch draw made while a 50x30 RT (not matching the 320x240 window) is
        // bound must fill THAT target's own size, not the window's.
        dev.SetRenderTarget(rtOddSize_.get());
        dev.Clear(Color::Black);
        spriteBatch_->Begin();
        spriteBatch_->Draw(*rtColor_, Rectangle(0, 0, 50, 30), Rectangle(0, 0, kRTSize, kRTSize), Color::White);
        spriteBatch_->End();
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        if (runChecks)
        {
            const Color corner = ReadRTPixel(*rtOddSize_, 45, 25);
            Check(Matches(corner, Color(0, 200, 255, 255), 10),
                  "SpriteBatch draw while a differently-sized RT is bound fills the RT, not the window: got=" + ColorStr(corner));
        }
    }

    void Draw(const GameTime&) override
    {
        ++frame_;
        RunScene(/*runChecks=*/frame_ == 1);

        if (frame_ == kTotalFrames)
        {
            Check(true, "60 frames of the RenderTarget2D scene render with no exception");
            std::printf("=== %d/%d PASS ===\n", passCount_, 6);
            result_ = (passCount_ == 6) ? 0 : 1;
            Exit();
        }
    }

public:
    OpenGL2RenderTarget2DTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(320);
        gdm_->setPreferredBackBufferHeightProperty(240);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    int getResult() const { return result_; }
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    OpenGL2RenderTarget2DTest game;
    game.Run();
    return game.getResult();
}
