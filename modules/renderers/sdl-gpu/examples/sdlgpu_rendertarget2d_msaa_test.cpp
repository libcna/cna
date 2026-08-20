// SPDX-License-Identifier: MS-PL
// plans/plan_sdlgpu.md SDLGPU-38: RenderTarget2D MSAA proof for the SDL_GPU graphics renderer --
// SDL_GPUSampleCount texture creation + SDL_GPUColorTargetInfo.resolve_texture automatic
// resolve-on-render-pass-end (the exact mechanism SdlGpuRenderTargetCubeRenderer's own MSAA
// support, SDLGPU-36, already proved out; this closes the matching RenderTarget2D leg).
//
// This renderer has no ReadBackbuffer() yet (SDLGPU-39's swapchain leg is a documented, unresolved
// segfault), so verification here follows the established convention: real draws with no
// exception, MultiSampleCount property fidelity, plus sampling the MSAA target via SpriteBatch for
// a real screenshot (not just "didn't throw"). All render targets are members (created once in
// LoadContent(), not Draw()-local) per the real use-after-free finding documented in
// plans/plan_sdlgpu.md's SDLGPU-37 row -- this renderer defers rendering to Present() time, so a target
// destroyed before that pass runs would release its GPU texture while queued commands still
// reference it.
//
// Check A -- MultiSampleCount property fidelity: preferredMultiSampleCount=4 must report a real,
//   device-clamped value >1 (proving the request reached the renderer, not silently dropped).
// Check B -- a real colored3d quad drawn into the MSAA target, resolved automatically at
//   render-pass end, then sampled back via SpriteBatch -- renders every frame with no exception.
// Check C -- a depth-tested MSAA target (DepthFormat::Depth24Stencil8): draws a farther red quad
//   then a nearer green quad, renders every frame with no exception. Exercises the MSAA depth
//   texture (sample count matches the MSAA color texture).
// Check D/E -- real RenderTarget2D::GetData() pixel readback of the resolved MSAA color/
//   depth-tested targets (Green in both cases) -- this is the actual discriminator for the
//   adversarial-review finding that every pipeline hardcoded SDL_GPU_SAMPLECOUNT_1 regardless of
//   the render pass's real MSAA sample count: SDL_gpu does not hard-crash on that mismatch (it
//   silently tolerates it), so Checks B/C alone ("didn't throw") could not have caught the bug --
//   only a real pixel readback proving the resolved content is correct can.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
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

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::SdlGpu;

namespace
{
    constexpr int kRTSize = 32;
    constexpr int kTotalFrames = 60;

    bool CloseTo(int a, int b, int tol) { return std::abs(a - b) <= tol; }
    bool Matches(const Color& c, const Color& expected, int tol = 10)
    {
        return CloseTo(c.getRProperty(), expected.getRProperty(), tol)
            && CloseTo(c.getGProperty(), expected.getGProperty(), tol)
            && CloseTo(c.getBProperty(), expected.getBProperty(), tol);
    }

    Color ReadPixel(RenderTarget2D& rt, int x, int y)
    {
        Color px(0, 0, 0, 0);
        const Rectangle rect(x, y, 1, 1);
        rt.GetData(0, &rect, &px, 0, 1);
        return px;
    }

    /// REMED-GFX-186: the same read, at a mip level this target's LevelCount says exists.
    Color ReadPixelAtLevel(RenderTarget2D& rt, int level, int x, int y)
    {
        Color px(0, 0, 0, 0);
        const Rectangle rect(x, y, 1, 1);
        rt.GetData(level, &rect, &px, 0, 1);
        return px;
    }

    /// Mirrors RenderTarget2D's own CalculateMipLevels -- what LevelCount must report.
    int ExpectedLevels(int w, int h)
    {
        int levels = 1;
        while (w > 1 || h > 1) { w = std::max(1, w / 2); h = std::max(1, h / 2); ++levels; }
        return levels;
    }
}

class SdlGpuRenderTarget2DMsaaTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> sb_;
    std::unique_ptr<RenderTarget2D> rtMsaa_;
    std::unique_ptr<RenderTarget2D> rtMsaaDepth_;
    std::unique_ptr<RenderTarget2D> rtMsaaMip_;   ///< REMED-GFX-186: multisampled AND mipmapped.
    std::unique_ptr<VertexBuffer> quadVb_;
    std::unique_ptr<VertexBuffer> farQuadVb_;
    std::unique_ptr<VertexBuffer> nearQuadVb_;
    int frame_ = 0;
    int passCount_ = 0;
    int result_ = 1;

    void Check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        if (ok) ++passCount_;
    }

    void RenderAndSample(GraphicsDevice& dev)
    {
        dev.SetRenderTarget(rtMsaa_.get());
        dev.Clear(Color::Black);
        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.Apply();
        dev.SetVertexBuffer(quadVb_.get());
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        dev.SetRenderTarget(rtMsaaDepth_.get());
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

        // REMED-GFX-186: the multisampled AND mipmapped target. Green fills the whole surface, so
        // every generated level is Green too -- a level that was never generated reads (0,0,0,0)
        // and a level read from the wrong resource cannot be Green either.
        dev.SetRenderTarget(rtMsaaMip_.get());
        dev.Clear(Color::Black);
        BasicEffect mipFx(dev);
        mipFx.VertexColorEnabled = true;
        mipFx.setWorldProperty(Matrix::getIdentityProperty());
        mipFx.setViewProperty(Matrix::getIdentityProperty());
        mipFx.setProjectionProperty(Matrix::getIdentityProperty());
        mipFx.Apply();
        dev.SetVertexBuffer(quadVb_.get());
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        const auto& vp = dev.getViewportProperty();
        const int w = vp.getWidthProperty();
        const int h = vp.getHeightProperty();
        const int cellW = w / 2;
        dev.Clear(Color::CornflowerBlue);
        dev.setBlendStateProperty(BlendState::Opaque);
        sb_->Begin();
        sb_->Draw(*rtMsaa_, Rectangle(0, 0, cellW, h), Rectangle(0, 0, kRTSize, kRTSize), Color::White);
        sb_->Draw(*rtMsaaDepth_, Rectangle(cellW, 0, cellW, h), Rectangle(0, 0, kRTSize, kRTSize), Color::White);
        sb_->End();
    }

protected:
    void LoadContent() override
    {
        auto& dev = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(dev);

        rtMsaa_ = std::make_unique<RenderTarget2D>(dev, kRTSize, kRTSize, false, SurfaceFormat::Color,
                                                   DepthFormat::None, 4, RenderTargetUsage::DiscardContents);
        rtMsaaDepth_ = std::make_unique<RenderTarget2D>(dev, kRTSize, kRTSize, false, SurfaceFormat::Color,
                                                        DepthFormat::Depth24Stencil8, 4, RenderTargetUsage::DiscardContents);
        // REMED-GFX-186: this file is NAMED for multisampled RenderTarget2D and its own header
        // advertises "real RenderTarget2D::GetData() pixel readback of the resolved MSAA color"
        // -- yet every target above is mipMap=false and every read above is level 0, so it could
        // not fail when a multisampled target silently lost its mip chain and reading level 1
        // segfaulted. A THIRD target, mipmapped as well as multisampled, closes that blind spot.
        rtMsaaMip_ = std::make_unique<RenderTarget2D>(dev, kRTSize, kRTSize, true, SurfaceFormat::Color,
                                                      DepthFormat::None, 4, RenderTargetUsage::DiscardContents);

        const VertexPositionColor quadVerts[6] = {
            { Vector3(-1.0f, -1.0f, 0.0f), Color::Green }, { Vector3(-1.0f, 1.0f, 0.0f), Color::Green }, { Vector3(1.0f, -1.0f, 0.0f), Color::Green },
            { Vector3(-1.0f, 1.0f, 0.0f), Color::Green }, { Vector3(1.0f, 1.0f, 0.0f), Color::Green }, { Vector3(1.0f, -1.0f, 0.0f), Color::Green },
        };
        quadVb_ = std::make_unique<VertexBuffer>(dev, VertexPositionColor::getVertexDeclarationStatic(), 6, BufferUsage::None);
        quadVb_->SetData(quadVerts, 0, 6);

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
            const int applied = rtMsaa_->getMultiSampleCountProperty();
            Check(applied > 1, "MultiSampleCount request 4 -> applied " + std::to_string(applied) + " (expected >1)");

            bool threw = false;
            const char* stage = "colored3d draw into MSAA target";
            try
            {
                RenderAndSample(dev);
                Check(true, "colored3d quad drawn into MSAA RenderTarget2D + resolve renders with no exception");
                stage = "depth-tested MSAA target";
                Check(true, "depth-tested MSAA RenderTarget2D renders with no exception");

                // Checks D/E: the actual discriminator (see this file's own top comment) -- a
                // pipeline created with the wrong sample_count for this MSAA render pass is not
                // guaranteed to hard-crash on this driver, so only a real pixel readback of the
                // resolved content can prove the fix matters.
                stage = "RenderTarget2D::GetData() readback of resolved MSAA color target";
                const Color gotColor = ReadPixel(*rtMsaa_, kRTSize / 2, kRTSize / 2);
                Check(Matches(gotColor, Color::Green),
                      "Check D: resolved MSAA colored3d quad reads back Green, got " +
                      std::to_string(gotColor.getRProperty()) + "," + std::to_string(gotColor.getGProperty()) +
                      "," + std::to_string(gotColor.getBProperty()));

                stage = "RenderTarget2D::GetData() readback of resolved MSAA depth-tested target";
                const Color gotDepth = ReadPixel(*rtMsaaDepth_, kRTSize / 2, kRTSize / 2);
                Check(Matches(gotDepth, Color::Green),
                      "Check E: resolved MSAA depth-tested target shows nearer Green quad, got " +
                      std::to_string(gotDepth.getRProperty()) + "," + std::to_string(gotDepth.getGProperty()) +
                      "," + std::to_string(gotDepth.getBProperty()));

                // Checks F/G/H (REMED-GFX-186): multisampling must not shorten the public chain,
                // and every level it promises must be readable. Pre-fix, F reported 1 instead of 6
                // and G killed the process with SIGSEGV inside VULKAN_DownloadFromTexture.
                stage = "RenderTarget2D LevelCount of a mipmapped MULTISAMPLED target";
                const int levels = rtMsaaMip_->getLevelCountProperty();
                Check(levels == ExpectedLevels(kRTSize, kRTSize),
                      "Check F: a mipMap=true, MSAA=4 target reports LevelCount " +
                      std::to_string(ExpectedLevels(kRTSize, kRTSize)) + ", got " +
                      std::to_string(levels));

                stage = "RenderTarget2D::GetData() readback of a GENERATED mip level";
                const Color gotMip = ReadPixelAtLevel(*rtMsaaMip_, 1, kRTSize / 4, kRTSize / 4);
                Check(Matches(gotMip, Color::Green),
                      "Check G: level 1 of the resolved, mipmapped MSAA target reads back Green, got " +
                      std::to_string(gotMip.getRProperty()) + "," + std::to_string(gotMip.getGProperty()) +
                      "," + std::to_string(gotMip.getBProperty()));

                stage = "RenderTarget2D::GetData() at a level that does not exist";
                bool rejected = false;
                try
                {
                    Color scratch(0, 0, 0, 0);
                    const Rectangle one(0, 0, 1, 1);
                    rtMsaaMip_->GetData(levels, &one, &scratch, 0, 1);
                }
                catch (const std::exception&) { rejected = true; }
                Check(rejected, "Check H: level == LevelCount is refused through a catchable public "
                                "exception, never a signal");
            }
            catch (const std::exception& e)
            {
                threw = true;
                Check(false, std::string("threw during ") + stage + ": " + e.what());
            }
            (void)threw;
        }
        else
        {
            RenderAndSample(dev);
        }

        if (frame_ == kTotalFrames)
        {
            Check(true, std::to_string(kTotalFrames) + " frames of MSAA RenderTarget2D render/sample render with no exception");
            std::printf("=== %d/9 PASS ===\n", passCount_);
            // REMED-GFX-186 added checks F, G and H.
            result_ = (passCount_ == 9) ? 0 : 1;
            Exit();
        }
    }

public:
    SdlGpuRenderTarget2DMsaaTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(200);
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

    SdlGpuRenderTarget2DMsaaTest game;
    game.Run();
    return game.getResult();
}
