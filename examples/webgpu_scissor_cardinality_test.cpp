// SPDX-License-Identifier: MS-PL
// REMED-GFX-146, structural half: capturing the scissor state per draw must stay DYNAMIC pass
// state.
//
// The pixel fixture (examples/deferred_scissor_capture_test.cpp) proves the captured value is the
// right one. This file proves the mechanism did not pay for it with resources, because several
// wrong-but-passing implementations exist:
//
//   * put the rectangle in the pipeline cache key         -> one pipeline variant per rectangle;
//   * put ScissorTestEnable in the pipeline cache key     -> one variant per enable transition;
//   * end and re-begin a render pass on every change      -> one pass (and one submit) per rect;
//   * call wgpuRenderPassEncoderSetScissorRect per draw   -> unbounded redundant native calls;
//   * keep a heap-allocated snapshot per queued draw      -> per-frame allocation growth.
//
// Every count below is read from the live renderer, before and after a known public sequence, so
// each expectation is an exact number rather than a trend. The viewport counter is read alongside
// it: REMED-GFX-116's mechanism must be untouched, so a sequence that changes only the rectangle
// must issue exactly ONE native setViewport (the one that opens the pass). The renderer's
// uncaptured-error callback count is also reported: WebGPU validation must stay silent through
// every rectangle used here, including the boundary ones (final row/column, 1x1, degenerate,
// hanging off the target).
//
// Exit code 0 = all checks PASS, 1 = any FAIL.

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
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "CNA/Internal/Renderers/WebGPU/WebGPURenderer.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::WebGPU::WebGPURenderer;

namespace
{
    constexpr int kBBW = 64;
    constexpr int kBBH = 64;
    constexpr int kRT  = 32;

    const Color kBlack(0, 0, 0, 255);
    const Color kRed  (255, 0, 0, 255);
}

class WebGpuScissorCardinalityTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> sb_;
    std::unique_ptr<BasicEffect> fx_;
    std::unique_ptr<Texture2D> white_;
    int phase_ = 0;
    int passCount_ = 0;
    int totalCount_ = 0;
    int result_ = 1;

    void check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        ++totalCount_;
        if (ok) ++passCount_;
    }

    void checkCount(std::size_t got, std::size_t want, const std::string& label)
    {
        check(got == want, got == want
                  ? label + " = " + std::to_string(got)
                  : label + ": expected " + std::to_string(want) + ", got " + std::to_string(got));
    }

    static std::array<VertexPositionColor, 6> Quad(const Color& c)
    {
        return {{
            {Vector3(-1.0f,  1.0f, 0.5f), c},
            {Vector3(-1.0f, -1.0f, 0.5f), c},
            {Vector3( 1.0f, -1.0f, 0.5f), c},
            {Vector3(-1.0f,  1.0f, 0.5f), c},
            {Vector3( 1.0f, -1.0f, 0.5f), c},
            {Vector3( 1.0f,  1.0f, 0.5f), c},
        }};
    }

    static RasterizerState Raster(bool scissorTestEnable)
    {
        RasterizerState rs = RasterizerState::CullNone;
        rs.setScissorTestEnableProperty(scissorTestEnable);
        return rs;
    }

    void Draw3D(GraphicsDevice& dev, const Color& c, bool scissorTestEnable = true)
    {
        dev.setRasterizerStateProperty(Raster(scissorTestEnable));
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        fx_->VertexColorEnabled = true;
        fx_->Apply();
        const auto quad = Quad(c);
        VertexBuffer vb(dev, VertexPositionColor::getVertexDeclarationStatic(), 6, BufferUsage::None);
        vb.SetData(quad.data(), 0, 6);
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);
    }

    static void SetScissor(GraphicsDevice& dev, int x, int y, int w, int h)
    {
        dev.setScissorRectangleProperty(Rectangle(x, y, w, h));
    }

    std::unique_ptr<RenderTarget2D> MakeTarget(GraphicsDevice& dev)
    {
        return std::make_unique<RenderTarget2D>(dev, kRT, kRT, false, SurfaceFormat::Color,
                                                DepthFormat::Depth24Stencil8, 0,
                                                RenderTargetUsage::DiscardContents);
    }

    void Draw(const GameTime&) override
    {
        if (phase_ == 0) { phase_ = 1; return; }
        if (phase_ != 1) { Exit(); return; }
        phase_ = 2;

        auto& dev = getGraphicsDeviceProperty();
        auto& renderer = static_cast<WebGPURenderer&>(dev.GetRenderer());
        std::printf("REMED-GFX-146 scissor state cardinality -- WEBGPU\n");

        // Warm-up cycle: creates the one Colored3D and the one sprite pipeline this file's draws
        // need, and gets the frame past its first swapchain acquisition, so every delta below
        // measures the sequence under test and not one-off setup. Both enable states are warmed,
        // so a pipeline variant later would have to be caused by the RECTANGLE.
        {
            auto warm = MakeTarget(dev);
            dev.SetRenderTarget(warm.get());
            dev.Clear(kBlack);
            Draw3D(dev, kRed, true);
            Draw3D(dev, kRed, false);
            SamplerState point = SamplerState::PointClamp;
            DepthStencilState noDepth = DepthStencilState::None;
            RasterizerState rs = Raster(true);
            sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, &noDepth, &rs);
            sb_->Draw(*white_, Rectangle(0, 0, kRT, kRT), Rectangle(0, 0, 1, 1), kRed);
            sb_->End();
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        }

        // ---- S1: four different rectangles in one bind cycle -> one pass, one submit ----
        {
            auto rt = MakeTarget(dev);
            dev.SetRenderTarget(rt.get());
            // Baseline INSIDE the cycle: binding a target flushes whatever the backbuffer had
            // pending, which is not what this check is measuring.
            const std::size_t pass0 = renderer.GetRenderPassCountEXT();
            const std::size_t submit0 = renderer.GetQueueSubmitCountEXT();
            const std::size_t ss0 = renderer.GetSetScissorCallCountEXT();
            const std::size_t sv0 = renderer.GetSetViewportCallCountEXT();
            const std::size_t pipe0 = renderer.GetColoredPipelineCacheSizeEXT();

            dev.Clear(kBlack);
            for (int i = 0; i < 4; ++i)
            {
                SetScissor(dev, i * (kRT / 4), 0, kRT / 4, kRT);
                Draw3D(dev, kRed);
            }
            SetScissor(dev, 0, 0, kRT, kRT);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            checkCount(renderer.GetRenderPassCountEXT() - pass0, 1,
                       "S1 four rectangles in one cycle: render passes");
            checkCount(renderer.GetQueueSubmitCountEXT() - submit0, 1,
                       "S1 four rectangles in one cycle: queue submits");
            // One pass-opening call plus one per genuine rectangle transition.
            checkCount(renderer.GetSetScissorCallCountEXT() - ss0, 5,
                       "S1 four rectangles in one cycle: native setScissorRect calls");
            // REMED-GFX-116 is untouched: the viewport never changed, so only the pass-opening
            // call is issued.
            checkCount(renderer.GetSetViewportCallCountEXT() - sv0, 1,
                       "S1 four rectangles in one cycle: native setViewport calls");
            checkCount(renderer.GetColoredPipelineCacheSizeEXT() - pipe0, 0,
                       "S1 four rectangles in one cycle: new Colored3D pipeline variants");
        }

        // ---- S2: four IDENTICAL rectangles -> the redundant native calls are skipped ----
        {
            auto rt = MakeTarget(dev);
            dev.SetRenderTarget(rt.get());
            const std::size_t ss0 = renderer.GetSetScissorCallCountEXT();
            const std::size_t pipe0 = renderer.GetColoredPipelineCacheSizeEXT();

            dev.Clear(kBlack);
            SetScissor(dev, 4, 4, kRT / 2, kRT / 2);
            for (int i = 0; i < 4; ++i)
                Draw3D(dev, kRed);
            SetScissor(dev, 0, 0, kRT, kRT);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            checkCount(renderer.GetSetScissorCallCountEXT() - ss0, 2,
                       "S2 four draws sharing one rectangle: native setScissorRect calls");
            checkCount(renderer.GetColoredPipelineCacheSizeEXT() - pipe0, 0,
                       "S2 four draws sharing one rectangle: new pipeline variants");
        }

        // ---- S3: 32 distinct rectangles must not grow any permanent cache ----
        {
            auto rt = MakeTarget(dev);
            dev.SetRenderTarget(rt.get());
            const std::size_t pipe0 = renderer.GetColoredPipelineCacheSizeEXT();
            const std::size_t sprite0 = renderer.GetSpritePipelineCacheSizeEXT();
            const std::size_t pass0 = renderer.GetRenderPassCountEXT();

            dev.Clear(kBlack);
            for (int i = 0; i < 32; ++i)
            {
                SetScissor(dev, i % kRT, (i * 3) % kRT, 1 + (i % (kRT - 1)),
                           1 + ((i * 5) % (kRT - 1)));
                Draw3D(dev, kRed);
            }
            SetScissor(dev, 0, 0, kRT, kRT);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            checkCount(renderer.GetColoredPipelineCacheSizeEXT() - pipe0, 0,
                       "S3 32 distinct rectangles: new Colored3D pipeline variants");
            checkCount(renderer.GetSpritePipelineCacheSizeEXT() - sprite0, 0,
                       "S3 32 distinct rectangles: new sprite pipeline variants");
            checkCount(renderer.GetRenderPassCountEXT() - pass0, 1,
                       "S3 32 distinct rectangles: render passes");
        }

        // ---- S4: boundary and degenerate rectangles are legal and change no cardinality ----
        {
            auto rt = MakeTarget(dev);
            dev.SetRenderTarget(rt.get());
            const std::size_t pipe0 = renderer.GetColoredPipelineCacheSizeEXT();
            const std::size_t pass0 = renderer.GetRenderPassCountEXT();
            const std::size_t submit0 = renderer.GetQueueSubmitCountEXT();

            dev.Clear(kBlack);
            SetScissor(dev, kRT - 1, kRT - 1, 1, 1);          // final row and column
            Draw3D(dev, kRed);
            SetScissor(dev, 0, 0, 0, 0);                       // wholly degenerate
            Draw3D(dev, kRed);
            SetScissor(dev, kRT - 4, kRT - 4, 32, 32);         // hangs off the target
            Draw3D(dev, kRed);
            SetScissor(dev, kRT + 16, kRT + 16, 8, 8);         // entirely outside
            Draw3D(dev, kRed);
            SetScissor(dev, 0, 0, kRT, kRT);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            checkCount(renderer.GetColoredPipelineCacheSizeEXT() - pipe0, 0,
                       "S4 boundary rectangles: new pipeline variants");
            checkCount(renderer.GetRenderPassCountEXT() - pass0, 1,
                       "S4 boundary rectangles: render passes");
            checkCount(renderer.GetQueueSubmitCountEXT() - submit0, 1,
                       "S4 boundary rectangles: queue submits");
        }

        // ---- S5: ScissorTestEnable transitions are not a pipeline dimension ----
        {
            auto rt = MakeTarget(dev);
            dev.SetRenderTarget(rt.get());
            const std::size_t ss0 = renderer.GetSetScissorCallCountEXT();
            const std::size_t pipe0 = renderer.GetColoredPipelineCacheSizeEXT();
            const std::size_t pass0 = renderer.GetRenderPassCountEXT();

            dev.Clear(kBlack);
            SetScissor(dev, 8, 8, 8, 8);
            Draw3D(dev, kRed, true);
            Draw3D(dev, kRed, false);   // resolves to the whole target
            Draw3D(dev, kRed, true);    // back to the rectangle
            SetScissor(dev, 0, 0, kRT, kRT);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            // Pass opens at the whole target, then rect -> full -> rect: three transitions.
            checkCount(renderer.GetSetScissorCallCountEXT() - ss0, 4,
                       "S5 enable transitions: native setScissorRect calls");
            checkCount(renderer.GetColoredPipelineCacheSizeEXT() - pipe0, 0,
                       "S5 enable transitions: new pipeline variants");
            checkCount(renderer.GetRenderPassCountEXT() - pass0, 1,
                       "S5 enable transitions: render passes");
        }

        // ---- S6: the per-frame command/state storage is released with the frame ----
        {
            auto rt = MakeTarget(dev);
            std::size_t previousPasses = 0;
            std::size_t previousScissors = 0;
            bool stable = true;
            for (int frame = 0; frame < 3; ++frame)
            {
                dev.SetRenderTarget(rt.get());
                const std::size_t pass0 = renderer.GetRenderPassCountEXT();
                const std::size_t ss0 = renderer.GetSetScissorCallCountEXT();
                dev.Clear(kBlack);
                for (int i = 0; i < 8; ++i)
                {
                    SetScissor(dev, i * (kRT / 8), 0, kRT / 8, kRT);
                    Draw3D(dev, kRed);
                }
                SetScissor(dev, 0, 0, kRT, kRT);
                dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
                const std::size_t usedPasses = renderer.GetRenderPassCountEXT() - pass0;
                const std::size_t usedScissors = renderer.GetSetScissorCallCountEXT() - ss0;
                if (frame > 0 && (usedPasses != previousPasses || usedScissors != previousScissors))
                    stable = false;
                previousPasses = usedPasses;
                previousScissors = usedScissors;
            }
            check(stable && previousPasses == 1 && previousScissors == 9,
                  stable && previousPasses == 1 && previousScissors == 9
                      ? "S6 repeated mixed-rectangle cycles: one pass and nine calls each, no growth"
                      : "S6 repeated mixed-rectangle cycles: per-cycle cardinality is not stable "
                        "(passes " + std::to_string(previousPasses) + ", calls " +
                        std::to_string(previousScissors) + ")");
        }

        // ---- S7: SpriteBatch shares the mechanism, not a second one ----
        {
            auto rt = MakeTarget(dev);
            dev.SetRenderTarget(rt.get());
            const std::size_t ss0 = renderer.GetSetScissorCallCountEXT();
            const std::size_t sprite0 = renderer.GetSpritePipelineCacheSizeEXT();

            dev.Clear(kBlack);
            SamplerState point = SamplerState::PointClamp;
            DepthStencilState noDepth = DepthStencilState::None;
            RasterizerState rs = Raster(true);
            for (int i = 0; i < 3; ++i)
            {
                SetScissor(dev, i * (kRT / 4), 0, kRT / 4, kRT);
                sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, &noDepth, &rs);
                sb_->Draw(*white_, Rectangle(0, 0, kRT, kRT), Rectangle(0, 0, 1, 1), kRed);
                sb_->End();
            }
            SetScissor(dev, 0, 0, kRT, kRT);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            checkCount(renderer.GetSetScissorCallCountEXT() - ss0, 4,
                       "S7 three sprite rectangles: native setScissorRect calls");
            check(renderer.GetSpritePipelineCacheSizeEXT() - sprite0 <= 1,
                  "S7 three sprite rectangles: at most one new sprite pipeline");
        }

        // ---- S8: the BACKBUFFER pass follows the same rules ----
        {
            const std::size_t ss0 = renderer.GetSetScissorCallCountEXT();
            const std::size_t pass0 = renderer.GetRenderPassCountEXT();
            const std::size_t submit0 = renderer.GetQueueSubmitCountEXT();

            dev.Clear(kBlack);
            for (int i = 0; i < 4; ++i)
            {
                SetScissor(dev, i * (kBBW / 4), 0, kBBW / 4, kBBH);
                Draw3D(dev, kRed);
            }
            SetScissor(dev, 0, 0, kBBW, kBBH);
            std::vector<Color> pixels(static_cast<std::size_t>(kBBW) * kBBH, kBlack);
            const Rectangle whole(0, 0, kBBW, kBBH);
            dev.GetBackBufferData(&whole, pixels.data(), 0, static_cast<int>(pixels.size()));

            checkCount(renderer.GetSetScissorCallCountEXT() - ss0, 5,
                       "S8 four backbuffer rectangles: native setScissorRect calls");
            checkCount(renderer.GetRenderPassCountEXT() - pass0, 1,
                       "S8 four backbuffer rectangles: render passes");
            checkCount(renderer.GetQueueSubmitCountEXT() - submit0, 1,
                       "S8 four backbuffer rectangles: queue submits");
        }

        const std::size_t errors = renderer.GetUncapturedErrorCountEXT();
        check(errors == 0, errors == 0
                  ? "S9 WebGPU validation stayed silent through every rectangle"
                  : "S9 WebGPU reported " + std::to_string(errors) + " uncaptured errors");

        std::printf("%d/%d checks passed on WEBGPU\n", passCount_, totalCount_);
        result_ = (passCount_ == totalCount_) ? 0 : 1;
        Exit();
    }

    void Initialize() override
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBBW);
        gdm_->setPreferredBackBufferHeightProperty(kBBH);
        gdm_->ApplyChanges();
        Game::Initialize();

        auto& dev = getGraphicsDeviceProperty();
        dev.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
        sb_ = std::make_unique<SpriteBatch>(dev);
        white_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(
            dev, 1, 1, std::vector<std::uint8_t>{255, 255, 255, 255}));
        fx_ = std::make_unique<BasicEffect>(dev);
        fx_->setWorldProperty(Matrix::getIdentityProperty());
        fx_->setViewProperty(Matrix::getIdentityProperty());
        fx_->setProjectionProperty(Matrix::getIdentityProperty());
        fx_->setLightingEnabledProperty(false);
    }

public:
    [[nodiscard]] int Result() const { return result_; }
};

int main()
{
    WebGpuScissorCardinalityTest test;
    test.Run();
    return test.Result();
}
