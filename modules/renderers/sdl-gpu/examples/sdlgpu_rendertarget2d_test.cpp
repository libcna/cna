// SPDX-License-Identifier: MS-PL
// plans/plan_sdlgpu.md SDLGPU-35: RenderTarget2D proof for the SDL_GPU graphics renderer -- a
// COLOR_TARGET|SAMPLER texture rendered into during its own render pass and sampled during a
// later pass within the same frame (Phase SDLGPU-8's per-target multi-pass EnsureFrameRendered).
//
// This renderer does not implement ReadBackbuffer() (SDLGPU-39's swapchain leg is a documented,
// unresolved segfault -- see plans/plan_sdlgpu.md), but RenderTarget2D::GetData() itself IS real
// (SDLGPU-39's RenderTarget2D leg, closed via a real ITextureRenderer::GetData virtual +
// SdlGpuRenderTargetRenderer::GetData(), which reads its own self-owned colorTexture_, not the
// swapchain) -- so Checks E/F/G below assert exact pixel values, not just "didn't throw".
//
// Check A -- Clear()-only fill of a RenderTarget2D (no draw call between bind/unbind), then
//   sampled back via SpriteBatch onto the backbuffer, draws every frame with no exception. This
//   specifically exercises whether a render target with only a Clear() (no draw) still gets its
//   own render pass at all under this renderer's per-target grouping.
// Check B -- A real colored3d triangle (BasicEffect, VertexPositionColor) drawn into a
//   RenderTarget2D, then sampled back via SpriteBatch, draws every frame with no exception.
//   Proves the format-parameterized pipeline cache (Phase SDLGPU-8's refactor) produces a
//   pipeline compatible with the render target's own R8G8B8A8_UNORM format.
// Check C -- A depth-tested RenderTarget2D (DepthFormat::Depth24Stencil8): draws a farther red
//   quad then a nearer green quad over the same screen area, then samples it, draws every frame
//   with no exception. Exercises this target's own dedicated depth texture.
// Check D -- MultiSampleCount property fidelity: a RenderTarget2D constructed with
//   preferredMultiSampleCount=0 must report 0 (a real MSAA round-trip lives in its own dedicated
//   test, sdlgpu_rendertarget2d_msaa_test.cpp, SDLGPU-38).
// Check E/F/G -- real GetData() pixel assertions on rtClearOnly_/rtTriangle_/rtDepth_'s centre
//   pixels, proving the same 3 render targets Checks A-C exercise actually contain the expected
//   content, not just that drawing into them didn't throw.
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
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include "CNA/Internal/Renderers/SdlGpu/SdlGpuRenderer.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::SdlGpu;

namespace
{
    constexpr int kRTSize = 32;
    constexpr int kTotalFrames = 120;

    bool CloseTo(int a, int b, int tol) { return std::abs(a - b) <= tol; }
    bool Matches(const Color& c, const Color& expected)
    {
        return CloseTo(c.getRProperty(), expected.getRProperty(), 8)
            && CloseTo(c.getGProperty(), expected.getGProperty(), 8)
            && CloseTo(c.getBProperty(), expected.getBProperty(), 8);
    }

    Color ReadCentrePixel(RenderTarget2D& rt)
    {
        Color px(0, 0, 0, 0);
        const Rectangle rect(kRTSize / 2, kRTSize / 2, 1, 1);
        rt.GetData(0, &rect, &px, 0, 1);
        return px;
    }
}

class SdlGpuRenderTarget2DTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> sb_;

    std::unique_ptr<RenderTarget2D> rtClearOnly_;
    std::unique_ptr<RenderTarget2D> rtTriangle_;
    std::unique_ptr<RenderTarget2D> rtDepth_;

    std::unique_ptr<VertexBuffer> triangleVb_;
    std::unique_ptr<VertexBuffer> farQuadVb_;
    std::unique_ptr<VertexBuffer> nearQuadVb_;

    int frame_ = 0;
    int passCount_ = 0;
    int result_ = 1;

    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount_;
    }

    // Renders into all 3 render targets, mirroring plans/plan_sdlgpu.md's own "bound in one pass,
    // sampled in a later pass" contract for SDLGPU-35: Clear-only, a real colored3d draw, and a
    // depth-tested pair of overlapping quads.
    void RenderIntoTargets(GraphicsDevice& dev)
    {
        dev.SetRenderTarget(rtClearOnly_.get());
        dev.Clear(Color::Green);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        dev.SetRenderTarget(rtTriangle_.get());
        dev.Clear(Color::Black);
        BasicEffect triFx(dev);
        triFx.VertexColorEnabled = true;
        triFx.setWorldProperty(Matrix::getIdentityProperty());
        triFx.setViewProperty(Matrix::getIdentityProperty());
        triFx.setProjectionProperty(Matrix::getIdentityProperty());
        triFx.Apply();
        dev.SetVertexBuffer(triangleVb_.get());
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
        dev.SetVertexBuffer(nullptr);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        dev.SetRenderTarget(rtDepth_.get());
        dev.Clear(Color(0, 0, 0, 255), 1.0f);
        dev.SetDepthTestEnabled(true);
        dev.SetDepthWriteEnabled(true);
        BasicEffect depthFx(dev);
        depthFx.VertexColorEnabled = true;
        depthFx.setWorldProperty(Matrix::getIdentityProperty());
        depthFx.setViewProperty(Matrix::getIdentityProperty());
        depthFx.setProjectionProperty(Matrix::getIdentityProperty());
        depthFx.Apply();
        dev.SetVertexBuffer(farQuadVb_.get());
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nearQuadVb_.get());
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
        dev.SetDepthTestEnabled(false);
        dev.SetDepthWriteEnabled(false);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
    }

    void SampleTargetsToBackbuffer(GraphicsDevice& dev)
    {
        const auto& vp = dev.getViewportProperty();
        const int w = vp.getWidthProperty();
        const int h = vp.getHeightProperty();
        const int cellW = w / 3;

        dev.Clear(Color::CornflowerBlue);
        sb_->Begin();
        sb_->Draw(*rtClearOnly_, Rectangle(0, 0, cellW, h), Rectangle(0, 0, kRTSize, kRTSize), Color::White);
        sb_->Draw(*rtTriangle_, Rectangle(cellW, 0, cellW, h), Rectangle(0, 0, kRTSize, kRTSize), Color::White);
        sb_->Draw(*rtDepth_, Rectangle(cellW * 2, 0, cellW, h), Rectangle(0, 0, kRTSize, kRTSize), Color::White);
        sb_->End();
    }

protected:
    void LoadContent() override
    {
        auto& dev = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(dev);

        rtClearOnly_ = std::make_unique<RenderTarget2D>(dev, kRTSize, kRTSize, false, SurfaceFormat::Color,
                                                         DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
        rtTriangle_ = std::make_unique<RenderTarget2D>(dev, kRTSize, kRTSize, false, SurfaceFormat::Color,
                                                        DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
        rtDepth_ = std::make_unique<RenderTarget2D>(dev, kRTSize, kRTSize, false, SurfaceFormat::Color,
                                                     DepthFormat::Depth24Stencil8, 0, RenderTargetUsage::DiscardContents);

        const VertexPositionColor triVerts[3] = {
            { Vector3(-1.0f, -1.0f, 0.0f), Color::Red },
            { Vector3( 0.0f,  1.0f, 0.0f), Color::Red },
            { Vector3( 1.0f, -1.0f, 0.0f), Color::Red },
        };
        triangleVb_ = std::make_unique<VertexBuffer>(dev, VertexPositionColor::getVertexDeclarationStatic(), 3, BufferUsage::None);
        triangleVb_->SetData(triVerts, 0, 3);

        const VertexPositionColor farVerts[6] = {
            { Vector3(-1.0f, -1.0f, 0.5f), Color::Red }, { Vector3(-1.0f, 1.0f, 0.5f), Color::Red }, { Vector3(1.0f, -1.0f, 0.5f), Color::Red },
            { Vector3(-1.0f, 1.0f, 0.5f), Color::Red }, { Vector3(1.0f, 1.0f, 0.5f), Color::Red }, { Vector3(1.0f, -1.0f, 0.5f), Color::Red },
        };
        farQuadVb_ = std::make_unique<VertexBuffer>(dev, VertexPositionColor::getVertexDeclarationStatic(), 6, BufferUsage::None);
        farQuadVb_->SetData(farVerts, 0, 6);

        const VertexPositionColor nearVerts[6] = {
            { Vector3(-1.0f, -1.0f, -0.5f), Color::Green }, { Vector3(-1.0f, 1.0f, -0.5f), Color::Green }, { Vector3(1.0f, -1.0f, -0.5f), Color::Green },
            { Vector3(-1.0f, 1.0f, -0.5f), Color::Green }, { Vector3(1.0f, 1.0f, -0.5f), Color::Green }, { Vector3(1.0f, -1.0f, -0.5f), Color::Green },
        };
        nearQuadVb_ = std::make_unique<VertexBuffer>(dev, VertexPositionColor::getVertexDeclarationStatic(), 6, BufferUsage::None);
        nearQuadVb_->SetData(nearVerts, 0, 6);
    }

    void Draw(const GameTime&) override
    {
        ++frame_;
        auto& dev = getGraphicsDeviceProperty();

        if (frame_ == 1)
        {
            bool threw = false;
            const char* stage = "Check A (Clear-only)";
            try
            {
                RenderIntoTargets(dev);
                Check(true, "RenderTarget2D Clear-only/colored3d/depth-tested draws all render with no exception");
                stage = "SampleTargetsToBackbuffer";
                SampleTargetsToBackbuffer(dev);
                Check(true, "sampling all 3 RenderTarget2D instances via SpriteBatch renders with no exception");
            }
            catch (const std::exception& e)
            {
                threw = true;
                Check(false, (std::string("threw during ") + stage + ": " + e.what()).c_str());
            }
            (void)threw;

            // Check D: MultiSampleCount property fidelity for the non-MSAA case (real MSAA
            // round-trip verification lives in sdlgpu_rendertarget2d_msaa_test.cpp, SDLGPU-38).
            RenderTarget2D rtNoMsaa(dev, kRTSize, kRTSize, false, SurfaceFormat::Color,
                                    DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
            Check(rtNoMsaa.getMultiSampleCountProperty() == 0,
                  "RenderTarget2D MultiSampleCount request 0 -> applied 0");

            // Check E/F/G: real GetData() pixel assertions (SDLGPU-39's RenderTarget2D leg) --
            // proves the same 3 targets actually contain the expected content, not just that
            // drawing into them didn't throw.
            try
            {
                const Color gotClear = ReadCentrePixel(*rtClearOnly_);
                Check(Matches(gotClear, Color::Green),
                      ("GetData(): Clear-only RenderTarget2D centre pixel is green: got=("
                       + std::to_string(gotClear.getRProperty()) + "," + std::to_string(gotClear.getGProperty())
                       + "," + std::to_string(gotClear.getBProperty()) + ")").c_str());

                const Color gotTriangle = ReadCentrePixel(*rtTriangle_);
                Check(Matches(gotTriangle, Color::Red),
                      ("GetData(): colored3d triangle RenderTarget2D centre pixel is red: got=("
                       + std::to_string(gotTriangle.getRProperty()) + "," + std::to_string(gotTriangle.getGProperty())
                       + "," + std::to_string(gotTriangle.getBProperty()) + ")").c_str());

                const Color gotDepth = ReadCentrePixel(*rtDepth_);
                Check(Matches(gotDepth, Color::Green),
                      ("GetData(): depth-tested RenderTarget2D centre pixel is green (nearer quad won): got=("
                       + std::to_string(gotDepth.getRProperty()) + "," + std::to_string(gotDepth.getGProperty())
                       + "," + std::to_string(gotDepth.getBProperty()) + ")").c_str());
            }
            catch (const std::exception& e)
            {
                Check(false, (std::string("GetData() threw: ") + e.what()).c_str());
            }
        }
        else
        {
            RenderIntoTargets(dev);
            SampleTargetsToBackbuffer(dev);
        }

        if (frame_ == kTotalFrames)
        {
            Check(true, "120 frames of RenderTarget2D render/sample render with no exception");
            std::printf("=== %d/7 PASS ===\n", passCount_);
            result_ = (passCount_ == 7) ? 0 : 1;
            Exit();
        }
    }

public:
    SdlGpuRenderTarget2DTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(300);
        gdm_->setPreferredBackBufferHeightProperty(100);
        // GraphicsDeviceManager.SynchronizeWithVerticalRetrace defaults to true (the XNA
        // default); this test's virtual/headless display has no real vblank signal, so leaving
        // VSync on makes every frame wait roughly a second, blowing past this test's frame
        // budget. Disabling it here -- before Game::DoInitialize()'s CreateDevice() call reads
        // it -- is the correct, property-level way to request Immediate presentation.
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    int getResult() const { return result_; }
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    SdlGpuRenderTarget2DTest game;
    game.Run();
    return game.getResult();
}
