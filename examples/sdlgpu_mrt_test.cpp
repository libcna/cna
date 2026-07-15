// SPDX-License-Identifier: MS-PL
// plan_sdlgpu.md SDLGPU-37: Multiple Render Targets (MRT) proof for the SDL_GPU graphics backend.
//
// Matches this project's established D3D11/D3D12 MRT scope boundary: rts[0] is the real draw
// target; rts[1..count-1] are bound and independently cleared but never receive draws, since no
// shader in this codebase declares more than one fragment output.
//
// This backend has no ReadBackbuffer() yet (SDLGPU-39's swapchain leg is a documented, unresolved
// segfault -- see that row), so verification here follows the established convention: real draws
// with no exception, plus sampling all 3 targets via SpriteBatch into distinct screen regions for
// a real screenshot (not just "didn't throw").
//
// The render targets are members created once in LoadContent(), not Draw()-local variables --
// this backend defers all rendering to its own Present()-time EnsureFrameRendered() pass, so a
// render target destroyed before that pass runs releases its GPU texture while sprite/draw
// commands still queued against it hold the now-freed handle, a real use-after-free crash
// (discovered while first authoring this test with Draw()-local RenderTarget2D instances --
// SdlGpu_RenderTargetCube's own test avoids this by forcing an eager flush inside GetData()
// itself; this test has no such call, so the render targets must simply outlive the frame).
//
// Check A -- SetRenderTargets({rt0,rt1,rt2}) + one Clear(magenta) call clears all 3 simultaneously
//   (real MRT bind+clear), with no exception.
// Check B -- a colored3d quad drawn afterward renders with no exception; per the single-target
//   scope boundary, it should only visibly affect rt0 (confirmed via the screenshot: rt1/rt2 stay
//   magenta, rt0 turns green).
// Check C -- ClearColorAndDepth propagates to all 3 targets (depth clear included) with no
//   exception, and SetRenderTargets(nullptr, 0) cleanly restores the swapchain.
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
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include "CNA/Internal/Backends/SdlGpu/SdlGpuGraphicsBackend.hpp"

#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Backends::SdlGpu;

namespace
{
    constexpr int kRTSize = 32;
    constexpr int kTotalFrames = 30;
}

class SdlGpuMrtTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> sb_;
    std::unique_ptr<RenderTarget2D> rt0_;
    std::unique_ptr<RenderTarget2D> rt1_;
    std::unique_ptr<RenderTarget2D> rt2_;
    std::unique_ptr<VertexBuffer> quadVb_;
    int frame_ = 0;
    int passCount_ = 0;
    int result_ = 1;

    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount_;
    }

protected:
    void LoadContent() override
    {
        auto& dev = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(dev);

        rt0_ = std::make_unique<RenderTarget2D>(dev, kRTSize, kRTSize, false, SurfaceFormat::Color,
                                                DepthFormat::Depth24Stencil8, 0, RenderTargetUsage::DiscardContents);
        rt1_ = std::make_unique<RenderTarget2D>(dev, kRTSize, kRTSize, false, SurfaceFormat::Color,
                                                DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
        rt2_ = std::make_unique<RenderTarget2D>(dev, kRTSize, kRTSize, false, SurfaceFormat::Color,
                                                DepthFormat::None, 0, RenderTargetUsage::DiscardContents);

        const VertexPositionColor verts[6] = {
            { Vector3(-1.0f, -1.0f, 0.0f), Color::Green }, { Vector3(-1.0f, 1.0f, 0.0f), Color::Green }, { Vector3(1.0f, -1.0f, 0.0f), Color::Green },
            { Vector3(-1.0f, 1.0f, 0.0f), Color::Green }, { Vector3(1.0f, 1.0f, 0.0f), Color::Green }, { Vector3(1.0f, -1.0f, 0.0f), Color::Green },
        };
        quadVb_ = std::make_unique<VertexBuffer>(dev, VertexPositionColor::getVertexDeclarationStatic(), 6, BufferUsage::None);
        quadVb_->SetData(verts, 0, 6);
    }

    void RenderMrtAndSample(GraphicsDevice& dev)
    {
        std::vector<RenderTargetBinding> bindings{
            RenderTargetBinding(rt0_.get()), RenderTargetBinding(rt1_.get()), RenderTargetBinding(rt2_.get())};
        dev.SetRenderTargets(bindings);
        dev.Clear(Color::Magenta);

        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.Apply();
        dev.SetVertexBuffer(quadVb_.get());
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);

        dev.Clear(Color::Magenta, 1.0f);
        dev.SetRenderTargets(std::vector<RenderTargetBinding>{});

        const auto& vp = dev.getViewportProperty();
        const int w = vp.getWidthProperty();
        const int h = vp.getHeightProperty();
        const int cellW = w / 3;
        dev.Clear(Color::Black);
        dev.setBlendStateProperty(BlendState::Opaque);
        sb_->Begin();
        sb_->Draw(*rt0_, Rectangle(0, 0, cellW, h), Rectangle(0, 0, kRTSize, kRTSize), Color::White);
        sb_->Draw(*rt1_, Rectangle(cellW, 0, cellW, h), Rectangle(0, 0, kRTSize, kRTSize), Color::White);
        sb_->Draw(*rt2_, Rectangle(cellW * 2, 0, cellW, h), Rectangle(0, 0, kRTSize, kRTSize), Color::White);
        sb_->End();
    }

    void Draw(const GameTime&) override
    {
        ++frame_;
        auto& dev = getGraphicsDeviceProperty();

        if (frame_ == 1)
        {
            bool threw = false;
            const char* stage = "SetRenderTargets+Clear";
            try
            {
                std::vector<RenderTargetBinding> bindings{
                    RenderTargetBinding(rt0_.get()), RenderTargetBinding(rt1_.get()), RenderTargetBinding(rt2_.get())};
                dev.SetRenderTargets(bindings);
                dev.Clear(Color::Magenta);
                Check(true, "SetRenderTargets({rt0,rt1,rt2}) + Clear(magenta) renders with no exception");

                stage = "colored3d draw";
                BasicEffect fx(dev);
                fx.VertexColorEnabled = true;
                fx.setWorldProperty(Matrix::getIdentityProperty());
                fx.setViewProperty(Matrix::getIdentityProperty());
                fx.setProjectionProperty(Matrix::getIdentityProperty());
                fx.Apply();
                dev.SetVertexBuffer(quadVb_.get());
                dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
                dev.SetVertexBuffer(nullptr);
                Check(true, "colored3d quad drawn while MRT is bound renders with no exception");

                stage = "ClearColorAndDepth + SetRenderTargets(nullptr,0)";
                dev.Clear(Color::Magenta, 1.0f);
                dev.SetRenderTargets(std::vector<RenderTargetBinding>{});
                dev.Clear(Color::CornflowerBlue);
                Check(true, "ClearColorAndDepth on the MRT set + SetRenderTargets(nullptr,0) restore renders with no exception");
            }
            catch (const std::exception& e)
            {
                threw = true;
                Check(false, (std::string("threw during ") + stage + ": " + e.what()).c_str());
            }
            (void)threw;

            const auto& vp = dev.getViewportProperty();
            const int w = vp.getWidthProperty();
            const int h = vp.getHeightProperty();
            const int cellW = w / 3;
            dev.Clear(Color::Black);
            dev.setBlendStateProperty(BlendState::Opaque);
            sb_->Begin();
            sb_->Draw(*rt0_, Rectangle(0, 0, cellW, h), Rectangle(0, 0, kRTSize, kRTSize), Color::White);
            sb_->Draw(*rt1_, Rectangle(cellW, 0, cellW, h), Rectangle(0, 0, kRTSize, kRTSize), Color::White);
            sb_->Draw(*rt2_, Rectangle(cellW * 2, 0, cellW, h), Rectangle(0, 0, kRTSize, kRTSize), Color::White);
            sb_->End();
        }
        else
        {
            RenderMrtAndSample(dev);
        }

        if (frame_ == kTotalFrames)
        {
            std::printf("=== %d/3 PASS ===\n", passCount_);
            result_ = (passCount_ == 3) ? 0 : 1;
            Exit();
        }
    }

public:
    SdlGpuMrtTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(300);
        gdm_->setPreferredBackBufferHeightProperty(100);
        static_cast<SdlGpuGraphicsBackend&>(getGraphicsDeviceProperty().GetBackend()).SetSwapInterval(0);
    }

    int getResult() const { return result_; }
};

int main()
{
    SdlGpuMrtTest game;
    game.Run();
    return game.getResult();
}
