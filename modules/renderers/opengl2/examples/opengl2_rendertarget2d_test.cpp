// SPDX-License-Identifier: MS-PL
// plans/plan_opengl2.md: pixel-exact RenderTarget2D/FBO proof for the native OpenGL 2.1 graphics
// renderer -- render into an off-screen target, read it back directly, sample it as an ordinary
// Texture2D via SpriteBatch, and prove a depth-test occlusion inside the FBO itself.
//
// Check A -- RenderTarget2D construction succeeds and reports the requested size.
// Check B -- a solid-color BasicEffect quad drawn into the RT reads back via
//   RenderTarget2D::GetData() exactly (proves the FBO color attachment + RenderTarget::GetData()).
// Check C -- that same RT, unbound and drawn via SpriteBatch onto the backbuffer, reads back the
//   same color at the expected screen position (proves a RenderTarget2D can be sampled as a plain
//   Texture2D through SpriteBatch -- this renderer's Sprite::Draw used to dynamic_cast to the
//   concrete Tex type and silently no-op for anything else, including RenderTarget2D).
// Check D -- real depth-test occlusion INSIDE the RT: a nearer quad wins regardless of draw
//   order, read back via GetData() (proves the RT's own depth/stencil renderbuffer is real).
// Check E -- a SpriteBatch draw made WHILE a differently-sized RT is bound fills that RT's own
//   dimensions, not the window's (the Task-1078-equivalent fix: GetCurrentRenderTarget2DSize()).
// Check F -- mipmap generation: a mipMap=true RT's level-1 (half-size) mip, read back via
//   RenderTarget2D::GetData(1, ...), matches the solid color drawn into level 0 -- only possible
//   if glGenerateMipmap() actually ran on unbind (an un-generated level 1 would still hold
//   whatever CreateResources()'s empty glTexImage2D(level=1, ..., nullptr) left it as, not the
//   drawn color).
// Check G -- MSAA: the same hard-edged diagonal-split scene rendered into a multisampled RT vs a
//   single-sample RT. At the exact center pixel (where the diagonal edge crosses), the
//   single-sample result must equal one of the two flat colors exactly (no blending possible
//   without multisampling); the MSAA result must NOT equal either flat color exactly (a real
//   resolve blend) -- proves glRenderbufferStorageMultisample + the resolve blit are both real.
// Check H -- 60 frames of the whole scene render with no exception.
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

#include "CNA/Internal/Renderers/OpenGL2/OpenGL2Renderer.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::OpenGL2;

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

    Color ReadRTPixel(RenderTarget2D& rt, int x, int y, int level = 0)
    {
        Color px(0, 0, 0, 0);
        const Rectangle rect(x, y, 1, 1);
        rt.GetData(level, &rect, &px, 0, 1);
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
    std::unique_ptr<RenderTarget2D> rtMipmap_;
    std::unique_ptr<RenderTarget2D> rtMsaa_;
    std::unique_ptr<RenderTarget2D> rtNoMsaa_;

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

    // A single triangle covering the main-diagonal half of the target (vertices at the
    // bottom-left, top-left, and bottom-right clip-space corners), leaving the rest showing
    // whatever the target was Clear()'d to. The hypotenuse passes exactly through the target's
    // center pixel. Authored for the corrected top-down render-target orientation (clip +Y =
    // caller row 0): the pre-fix vertex set only straddled the (c,c) probe diagonal because the
    // renderer's old bottom-up storage flipped it there.
    void DrawDiagonalTriangle(GraphicsDevice& dev, const Color& color)
    {
        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.Apply();

        // CW winding (matches DrawFullscreenColorQuad's own established convention -- visible
        // under this project's default RasterizerState.CullCounterClockwise with Identity
        // view/projection). The original CCW ordering here was silently backface-culled in both
        // the MSAA and single-sample RTs, which the single-sample check didn't catch (an
        // all-background result is trivially "flat, unblended" too) -- a test bug, not a
        // renderer one.
        const VertexPositionColor verts[3] = {
            {Vector3(-1.0f, -1.0f, 0.0f), color}, {Vector3(-1.0f, 1.0f, 0.0f), color}, {Vector3(1.0f, -1.0f, 0.0f), color},
        };
        VertexBuffer vb(dev, VertexPositionColor::getVertexDeclarationStatic(), 3, BufferUsage::None);
        vb.SetData(verts, 0, 3);
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
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

        rtMipmap_ = std::make_unique<RenderTarget2D>(dev, kRTSize, kRTSize, true, SurfaceFormat::Color,
                                                      DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
        rtMsaa_ = std::make_unique<RenderTarget2D>(dev, kRTSize, kRTSize, false, SurfaceFormat::Color,
                                                    DepthFormat::None, 4, RenderTargetUsage::DiscardContents);
        rtNoMsaa_ = std::make_unique<RenderTarget2D>(dev, kRTSize, kRTSize, false, SurfaceFormat::Color,
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

        // Check F: mipmap generation -- level 1 must hold the same solid color drawn into level 0.
        dev.SetRenderTarget(rtMipmap_.get());
        dev.Clear(Color::Black);
        DrawFullscreenColorQuad(dev, 0.0f, Color(0, 200, 255, 255));
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        if (runChecks)
        {
            const Color mip1 = ReadRTPixel(*rtMipmap_, kRTSize / 4, kRTSize / 4, /*level=*/1);
            Check(Matches(mip1, Color(0, 200, 255, 255), 15),
                  "mipMap=true RT's level-1 mip matches the level-0 color (glGenerateMipmap ran): got=" + ColorStr(mip1));
        }

        // Check G: MSAA -- same hard diagonal-split scene into a multisampled vs. single-sample RT.
        // Sample several pixels straddling the diagonal rather than just one: exactly which pixel
        // has genuinely mixed sample coverage depends on the driver's MSAA sample-point pattern,
        // not just geometry, so a single fixed pixel risks a false negative.
        const int kProbeCoords[3] = {kRTSize / 2 - 1, kRTSize / 2, kRTSize / 2 + 1};

        dev.SetRenderTarget(rtNoMsaa_.get());
        dev.Clear(Color::Blue);
        DrawDiagonalTriangle(dev, Color::Red);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        bool noMsaaAllFlat = true;
        for (int c : kProbeCoords)
        {
            const Color px = ReadRTPixel(*rtNoMsaa_, c, c);
            if (!Matches(px, Color::Red, 5) && !Matches(px, Color::Blue, 5)) noMsaaAllFlat = false;
        }

        dev.SetRenderTarget(rtMsaa_.get());
        dev.Clear(Color::Blue);
        DrawDiagonalTriangle(dev, Color::Red);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        bool msaaAnyBlended = false;
        Color msaaSample(0, 0, 0, 0);
        for (int c : kProbeCoords)
        {
            const Color px = ReadRTPixel(*rtMsaa_, c, c);
            if (!Matches(px, Color::Red, 5) && !Matches(px, Color::Blue, 5)) { msaaAnyBlended = true; msaaSample = px; }
        }
        if (runChecks)
        {
            Check(noMsaaAllFlat, "single-sample RT: diagonal-edge pixels are flat, unblended colors (no MSAA resolve to blend them)");
            Check(msaaAnyBlended,
                  "MSAA RT (4x): at least one diagonal-edge pixel is a real resolve blend, not a flat color: got=" + ColorStr(msaaSample) +
                  " (GetMultiSampleCount()=" + std::to_string(rtMsaa_->getMultiSampleCountProperty()) + ")");
        }
    }

    void Draw(const GameTime&) override
    {
        ++frame_;
        RunScene(/*runChecks=*/frame_ == 1);

        if (frame_ == kTotalFrames)
        {
            Check(true, "60 frames of the RenderTarget2D scene render with no exception");
            std::printf("=== %d/%d PASS ===\n", passCount_, 9);
            result_ = (passCount_ == 9) ? 0 : 1;
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
