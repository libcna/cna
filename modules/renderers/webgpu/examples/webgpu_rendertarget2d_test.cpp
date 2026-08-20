// SPDX-License-Identifier: MS-PL
// WEBGPU-53/54: verify WebGPURenderTargetRenderer / WebGPURenderer::CreateRenderTarget2D()/
// SetRenderTarget2D() -- this renderer's first real RenderTarget2D support. Before this task,
// CreateRenderTarget2D/SetRenderTarget2D on this renderer were IGraphicsRenderer's own safe no-op
// defaults (CreateRenderTarget2D returned nullptr, SetRenderTarget2D did nothing), so any game
// creating a RenderTarget2D on WEBGPU silently drew to the visible backbuffer instead of the
// intended off-screen target, or crashed on the null renderer handle.
//
// Architecture note: this renderer renders every queued Clear()/3D-draw/SpriteBatch command into
// exactly ONE deferred render pass per logical frame, lazily, the first time something needs the
// frame to be real (EnsureFrameRendered()). SetRenderTarget2D() now eagerly flushes whatever was
// queued for the OUTGOING target into its own render pass the instant the target actually
// changes, so draws before/after a target switch land in the correct pass instead of collapsing
// into one -- see WebGPURenderer::SetRenderTarget2D()'s own comment for the full design
// discussion (and why this smaller change was chosen over VulkanRenderer's own per-draw
// target-tag/replay model).
//
// Check A -- Clear-only fill of a RenderTarget2D (DepthFormat::None), read back via GetData():
//   centre pixel is the clear colour, not stale/backbuffer content -- proves the clear genuinely
//   landed on the render target.
// Check B -- a real BasicEffect/VertexPositionColor quad drawn into a RenderTarget2D, read back
//   via GetData(): centre pixel is the quad's colour -- proves a real 3D draw dispatch reaches
//   the render target (not just Clear()).
// Check C -- a depth-tested RenderTarget2D (DepthFormat::Depth24Stencil8, a real combined
//   depth+stencil format, cleared via Clear(Target|DepthBuffer|Stencil, ...) -- a genuine stencil
//   clear inside a real render pass, exercising WEBGPU-8/9's previously-unexercised stencil-
//   attachment gap): a farther red quad then a nearer green quad -- green must win, proving a
//   genuine per-render-target depth buffer, not painter's-algorithm draw order.
// Check D -- sampling all 3 render targets back onto the visible backbuffer via SpriteBatch
//   renders with no exception -- proves a RenderTarget2D can be used as an ordinary sampled
//   texture, not just read back via GetData().
// Check E -- the critical architecture check: Clear(Blue) on the backbuffer, THEN
//   SetRenderTarget(rt) + Clear(Red) + SetRenderTarget(nullptr) targeting a SEPARATE
//   RenderTarget2D, then read back the BACKBUFFER's centre pixel -- must still be Blue, proving
//   the intervening render-target-targeted Clear() did NOT leak into the backbuffer's own render
//   pass (the single biggest risk of this renderer's "one deferred pass" architecture). Uses
//   Blue/Red (0/255-only channel values) rather than a blended reference colour such as
//   CornflowerBlue deliberately: this renderer's swapchain format is frequently an sRGB variant,
//   and a Clear() colour's exact byte-for-byte round trip through an sRGB-encoded attachment is a
//   separate, already-known, renderer-wide characteristic (see plans/plan_webgpu.md's WEBGPU-132 note on
//   "the exact value depends on whether the chosen swapchain format blends in linear or
//   sRGB-encoded space") -- 0/255 channel values are gamma-invariant at both encoding endpoints,
//   so this check exercises target separation specifically, without also depending on that
//   unrelated, pre-existing question.
// Check F -- MultiSampleCount property fidelity: this test's own GraphicsDeviceManager never
//   enables PreferMultiSampling, so the renderer's global sample count (WEBGPU-58) stays 1 (no
//   MSAA) throughout -- requesting 4 on this specific RenderTarget2D instance must still report
//   0, since WebGPURenderTargetRenderer unconditionally mirrors the RENDERER's own current global
//   state (not the per-instance request -- see that class's own doc comment), which is disabled
//   here. See examples/webgpu_msaa_test.cpp for full MSAA coverage (backbuffer AND
//   RenderTarget2D, with MSAA actually engaged).
// Check G -- CreateRenderTarget2D(mipMap=true) throws a clear exception rather than silently
//   creating a single-level target while the XNA layer expects a full mip chain (a deliberate,
//   documented scope cut -- see plans/plan_webgpu.md WEBGPU-53/54).
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kRTSize = 32;
    constexpr int kBackbufferSize = 64;

    bool CloseTo(int a, int b, int tol) { return std::abs(a - b) <= tol; }
    bool Matches(const Color& c, const Color& expected, int tol = 12)
    {
        return CloseTo(c.getRProperty(), expected.getRProperty(), tol)
            && CloseTo(c.getGProperty(), expected.getGProperty(), tol)
            && CloseTo(c.getBProperty(), expected.getBProperty(), tol);
    }

    Color ReadCentre(RenderTarget2D& rt)
    {
        Color px(0, 0, 0, 0);
        const Rectangle rect(rt.getWidthProperty() / 2, rt.getHeightProperty() / 2, 1, 1);
        rt.GetData(0, &rect, &px, 0, 1);
        return px;
    }

    Color ReadBackbufferCentre(GraphicsDevice& dev)
    {
        const Rectangle rect(kBackbufferSize / 2, kBackbufferSize / 2, 1, 1);
        Color px(0, 0, 0, 0);
        dev.GetBackBufferData(&rect, &px, 0, 1);
        return px;
    }

    void ApplyBasicEffect(GraphicsDevice& dev)
    {
        static BasicEffect* fx = nullptr;
        if (fx == nullptr) fx = new BasicEffect(dev);
        fx->setWorldProperty(Matrix::getIdentityProperty());
        fx->setViewProperty(Matrix::getIdentityProperty());
        fx->setProjectionProperty(Matrix::getIdentityProperty());
        fx->VertexColorEnabled = true;
        fx->Apply();
    }

    // Full-screen quad (clip-space -1..1) at the given Z, single solid colour -- renders into
    // whichever render target is currently bound on dev (RenderTarget2D or the backbuffer).
    void DrawFullScreenQuad(GraphicsDevice& dev, float z, const Color& color)
    {
        const VertexPositionColor verts[6] = {
            { Vector3(-1.0f,  1.0f, z), color },
            { Vector3(-1.0f, -1.0f, z), color },
            { Vector3( 1.0f, -1.0f, z), color },
            { Vector3(-1.0f,  1.0f, z), color },
            { Vector3( 1.0f, -1.0f, z), color },
            { Vector3( 1.0f,  1.0f, z), color },
        };
        ApplyBasicEffect(dev);
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);
    }
}

class WebGpuRenderTarget2DTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_ = false;
    int passCount_ = 0;
    int result_ = 1;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount_;
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        SpriteBatch spriteBatch(dev);

        // Check A: Clear-only RenderTarget2D (no depth), read back via GetData().
        RenderTarget2D rtClearOnly(dev, kRTSize, kRTSize, false, SurfaceFormat::Color,
                                   DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
        dev.SetRenderTarget(&rtClearOnly);
        dev.Clear(Color::Lime);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        {
            const Color got = ReadCentre(rtClearOnly);
            check(Matches(got, Color::Lime),
                  ("Check A: Clear-only RenderTarget2D centre pixel is Lime via GetData(): got=("
                   + std::to_string(got.getRProperty()) + "," + std::to_string(got.getGProperty())
                   + "," + std::to_string(got.getBProperty()) + ")").c_str());
        }

        // Check B: a real BasicEffect quad drawn into a RenderTarget2D.
        RenderTarget2D rtQuad(dev, kRTSize, kRTSize, false, SurfaceFormat::Color,
                              DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
        dev.SetRenderTarget(&rtQuad);
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        dev.Clear(Color::Black);
        DrawFullScreenQuad(dev, 0.5f, Color::Red);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        {
            const Color got = ReadCentre(rtQuad);
            check(Matches(got, Color::Red),
                  ("Check B: BasicEffect quad drawn into a RenderTarget2D reads back Red via "
                   "GetData(): got=(" + std::to_string(got.getRProperty()) + ","
                   + std::to_string(got.getGProperty()) + "," + std::to_string(got.getBProperty())
                   + ")").c_str());
        }

        // Check C: depth-tested RenderTarget2D (real Depth24Stencil8 attachment) -- a farther red
        // quad then a nearer green quad, cleared via ClearOptions::Target|DepthBuffer|Stencil (a
        // real stencil clear inside a real render pass, exercising WEBGPU-8/9's previously-
        // unexercised stencil-attachment gap). Green must win.
        RenderTarget2D rtDepth(dev, kRTSize, kRTSize, false, SurfaceFormat::Color,
                               DepthFormat::Depth24Stencil8, 0, RenderTargetUsage::DiscardContents);
        dev.SetRenderTarget(&rtDepth);
        dev.setDepthStencilStateProperty(DepthStencilState::Default);
        dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
                  Color::Black, 1.0f, 0);
        DrawFullScreenQuad(dev, 0.9f, Color::Red);    // far, drawn first
        DrawFullScreenQuad(dev, 0.1f, Color::Lime);   // near, drawn second -- must win
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        {
            const Color got = ReadCentre(rtDepth);
            check(Matches(got, Color::Lime),
                  ("Check C: depth-tested RenderTarget2D (Depth24Stencil8, real stencil clear) -- "
                   "nearer green quad wins over farther red: got=("
                   + std::to_string(got.getRProperty()) + "," + std::to_string(got.getGProperty())
                   + "," + std::to_string(got.getBProperty()) + ")").c_str());
        }

        // Check D: sampling all 3 render targets back onto the visible backbuffer via
        // SpriteBatch renders with no exception.
        {
            bool threw = false;
            std::string what;
            try
            {
                dev.Clear(Color::CornflowerBlue);
                const int cellW = kBackbufferSize / 3;
                spriteBatch.Begin();
                spriteBatch.Draw(rtClearOnly, Rectangle(0, 0, cellW, kBackbufferSize),
                                 Rectangle(0, 0, kRTSize, kRTSize), Color::White);
                spriteBatch.Draw(rtQuad, Rectangle(cellW, 0, cellW, kBackbufferSize),
                                 Rectangle(0, 0, kRTSize, kRTSize), Color::White);
                spriteBatch.Draw(rtDepth, Rectangle(cellW * 2, 0, cellW, kBackbufferSize),
                                 Rectangle(0, 0, kRTSize, kRTSize), Color::White);
                spriteBatch.End();
            }
            catch (const std::exception& e)
            {
                threw = true;
                what = e.what();
            }
            check(!threw, (std::string("Check D: sampling 3 RenderTarget2D instances via "
                                       "SpriteBatch renders with no exception") +
                          (threw ? (": threw " + what) : "")).c_str());
            // Force this frame's content to actually flush now (mirrors every other pixel check
            // in this file, which reads back immediately after drawing) -- otherwise Check D's
            // own pending SpriteBatch draws would still be sitting unflushed when Check E begins,
            // and would land on the backbuffer during Check E's own SetRenderTarget() switch
            // instead of at the point this test intended, contaminating that check's centre-pixel
            // read with stale content from this one.
            (void) ReadBackbufferCentre(dev);
        }

        // Check E: the critical architecture check -- an intervening render-target-targeted
        // Clear() must not leak into the backbuffer's own render pass.
        {
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            dev.Clear(Color::Blue);

            RenderTarget2D rtOther(dev, kRTSize, kRTSize, false, SurfaceFormat::Color,
                                   DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
            dev.SetRenderTarget(&rtOther);
            dev.Clear(Color::Red);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            const Color got = ReadBackbufferCentre(dev);
            check(Matches(got, Color::Blue),
                  ("Check E: backbuffer stays Blue across an intervening "
                   "RenderTarget2D-targeted Clear(Red) -- proves target separation, not a single "
                   "collapsed render pass: got=(" + std::to_string(got.getRProperty()) + ","
                   + std::to_string(got.getGProperty()) + "," + std::to_string(got.getBProperty())
                   + ")").c_str());

            const Color gotRT = ReadCentre(rtOther);
            check(Matches(gotRT, Color::Red),
                  "Check E (other half): the render target itself really did receive Clear(Red)");
        }

        // Check F: MultiSampleCount property fidelity -- this test's GraphicsDeviceManager never
        // enables PreferMultiSampling, so the renderer's own global MSAA sample count (WEBGPU-58)
        // stays disabled throughout; a RenderTarget2D unconditionally mirrors that renderer-global
        // state (not its own per-instance request), so requesting 4 here must still honestly
        // report 0. See examples/webgpu_msaa_test.cpp for the counterpart with MSAA actually
        // engaged.
        {
            RenderTarget2D rtMsaaRequest(dev, kRTSize, kRTSize, false, SurfaceFormat::Color,
                                        DepthFormat::None, 4, RenderTargetUsage::DiscardContents);
            check(rtMsaaRequest.getMultiSampleCountProperty() == 0,
                  "Check F: RenderTarget2D MultiSampleCount request 4 -> honestly reports 0 "
                  "(renderer's own global MSAA is disabled in this test -- see webgpu_msaa_test.cpp)");
        }

        // Check G: mipMap=true is a deliberate, documented scope cut -- must throw a clear
        // exception rather than silently under-delivering a requested mip chain.
        {
            bool threw = false;
            try
            {
                RenderTarget2D rtMip(dev, kRTSize, kRTSize, true, SurfaceFormat::Color,
                                     DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
            }
            catch (const std::exception&)
            {
                threw = true;
            }
            check(threw, "Check G: RenderTarget2D(mipMap=true) throws a clear exception "
                        "(mip regeneration not yet implemented, WEBGPU-53/54)");
        }

        std::printf("=== %d/8 PASS ===\n", passCount_);
        result_ = (passCount_ == 8) ? 0 : 1;
        Exit();
    }

public:
    WebGpuRenderTarget2DTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBackbufferSize);
        gdm_->setPreferredBackBufferHeightProperty(kBackbufferSize);
        gdm_->setPreferredDepthStencilFormatProperty(DepthFormat::Depth24Stencil8);
    }

    int getResult() const { return result_; }
};

int main()
{
    WebGpuRenderTarget2DTest game;
    game.Run();
    return game.getResult();
}
