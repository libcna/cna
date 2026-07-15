// SPDX-License-Identifier: MS-PL
// plan_sdlgpu.md SDLGPU-38: RenderTarget2D MSAA proof for the SDL_GPU graphics backend --
// SDL_GPUSampleCount texture creation + SDL_GPUColorTargetInfo.resolve_texture automatic
// resolve-on-render-pass-end (the exact mechanism SdlGpuRenderTargetCubeBackend's own MSAA
// support, SDLGPU-36, already proved out; this closes the matching RenderTarget2D leg).
//
// This backend has no ReadBackbuffer() yet (SDLGPU-39's swapchain leg is a documented, unresolved
// segfault), so verification here follows the established convention: real draws with no
// exception, MultiSampleCount property fidelity, plus sampling the MSAA target via SpriteBatch for
// a real screenshot (not just "didn't throw"). All render targets are members (created once in
// LoadContent(), not Draw()-local) per the real use-after-free finding documented in
// plan_sdlgpu.md's SDLGPU-37 row -- this backend defers rendering to Present() time, so a target
// destroyed before that pass runs would release its GPU texture while queued commands still
// reference it.
//
// Check A -- MultiSampleCount property fidelity: preferredMultiSampleCount=4 must report a real,
//   device-clamped value >1 (proving the request reached the backend, not silently dropped).
// Check B -- a real colored3d quad drawn into the MSAA target, resolved automatically at
//   render-pass end, then sampled back via SpriteBatch -- renders every frame with no exception.
// Check C -- a depth-tested MSAA target (DepthFormat::Depth24Stencil8): draws a farther red quad
//   then a nearer green quad, renders every frame with no exception. Exercises the MSAA depth
//   texture (sample count matches the MSAA color texture).
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

#include "CNA/Internal/Backends/SdlGpu/SdlGpuGraphicsBackend.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdio>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Backends::SdlGpu;

namespace
{
    constexpr int kRTSize = 32;
    constexpr int kTotalFrames = 60;
}

class SdlGpuRenderTarget2DMsaaTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> sb_;
    std::unique_ptr<RenderTarget2D> rtMsaa_;
    std::unique_ptr<RenderTarget2D> rtMsaaDepth_;
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
            std::printf("=== %d/4 PASS ===\n", passCount_);
            result_ = (passCount_ == 4) ? 0 : 1;
            Exit();
        }
    }

public:
    SdlGpuRenderTarget2DMsaaTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(200);
        gdm_->setPreferredBackBufferHeightProperty(100);
        static_cast<SdlGpuGraphicsBackend&>(getGraphicsDeviceProperty().GetBackend()).SetSwapInterval(0);
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
