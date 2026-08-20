// SPDX-License-Identifier: MS-PL
// Task 878/879: verify MSAA render target creation and resolve behavior on Bgfx.
//
// Direct port of examples/easygl_rendertarget2d_msaa_test.cpp (Task 337) — same methodology, same
// kRTSize, same assertions. See that file's header comment for the full rationale; summarized
// here: a solid-fill test can't distinguish "MSAA resolve doesn't corrupt solid colors" from
// "anti-aliasing genuinely happened" (a non-MSAA target passes a solid-fill test just as
// trivially). This test proves REAL anti-aliasing: render a diagonal-edged triangle into a
// RenderTarget2D, resolve it, sample it back, and check for partially-covered (blended) pixels
// along the diagonal edge — a signature that can only appear if the multisampled resolve actually
// averaged sub-pixel coverage.
//
// Unlike Vulkan, Bgfx's per-RT MSAA (BGFX_TEXTURE_RT_MSAA_Xn) is fully independent of any
// backbuffer MSAA state — bgfx resolves an MSAA-flagged render target's color attachment
// internally into the same sampleable texture handle, with no renderer-wide "did the game ask for
// backbuffer MSAA" precondition the way Vulkan's piggyback-on-sampleCount_ scope decision
// requires (see plans/plan_graphics.md Task 878/879). No GraphicsDeviceManager.PreferMultiSampling is
// needed here.
//
// This test's RT-then-backbuffer-sample path (via SpriteBatch::Draw sampling the unbound
// RenderTarget2D) only works correctly now because Task 878/879 also fixed two real, separate,
// previously-undiscovered, pre-existing bugs -- both invisible until now because no earlier
// Bgfx test both rendered into an RT smaller than the window AND could pixel-verify the result:
//   1. Task 873: BgfxSpriteBatchRenderer::Draw's unsafe cast silently sampled the RT's framebuffer
//      handle as if it were a texture handle whenever the drawn ITextureRenderer was actually a
//      BgfxRenderTargetRenderer. Fixed via a new IBgfxSamplable accessor both BgfxTextureRenderer
//      and BgfxRenderTargetRenderer implement correctly.
//   2. BgfxRenderer::EnsureViewState() (called from Clear()/SubmitSprite()) unconditionally
//      reset the current view's rect and 2D ortho projection to the full WINDOW size on every
//      call, silently clobbering BindAsRenderTarget()'s correctly RT-sized bgfx::setViewRect()
//      the moment Clear() was next called on that RT -- meaning every draw into a RenderTarget2D
//      smaller than the window actually rasterized into a viewport far larger than the RT's own
//      texture, corrupting the result. Fixed by tracking the currently-bound RT's own size and
//      only falling back to the full window size when targeting the backbuffer (view 0).
//
// A third, separate, environment-specific finding: in this project's Linux dev sandbox, bgfx's
// default OpenGL renderer negotiates only a legacy OpenGL 2.1 context (confirmed via glxinfo that
// the underlying Mesa/RadeonSI driver actually supports OpenGL 4.6 with full hardware
// acceleration -- bgfx simply requests an old context by default here), and MSAA-flagged
// framebuffer-attached textures do not actually resolve with real sub-pixel blending under that
// legacy GL 2.1 context path on this system, despite bgfx::getCaps() reporting
// BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER_MSAA/BGFX_CAPS_FORMAT_TEXTURE_MSAA support for RGBA8.
// Confirmed this is NOT a defect in this task's C++ wiring (which is 100% renderer-agnostic bgfx::
// API calls) by forcing bgfx onto its Vulkan renderer instead (CNA_BGFX_RENDERER=VULKAN, routing
// through the same real AMD GPU Vulkan uses) -- both checks pass cleanly, including a genuine
// intermediate (blended) pixel value, with zero code changes. This test is therefore registered
// (see CMakeLists.txt) with CNA_BGFX_RENDERER=VULKAN in its environment, exercising bgfx's own
// Vulkan renderer rather than its default OpenGL one specifically for this reason -- a bgfx/legacy-
// GL-context limitation, not a Bgfx-graphics-renderer-API code gap, and out of scope to fix here.
//
// Bgfx-only conventions mirrored from this test family (Task 364/896 finding):
//   - RasterizerState::CullNone is required — Bgfx's default cull state is the only one of the 3
//     renderers that actually culls this test family's standard triangle winding.
//   - GetBackBufferData() only reliably reflects the *first* read call per rendered frame,
//     so the backbuffer-sample-and-read step retries (<=20 attempts) until a genuinely fresh
//     (non-all-black) row is captured. Only the backbuffer-sampling step is retried — the RT's
//     own content is rendered once and is already stable/resolved before the retry loop begins.
//     Exactly ONE rectangle read happens per attempt, matching every other Bgfx pixel test in
//     this project (see bgfx_viewport_subregion_test.cpp's renderAndReadFresh for the same
//     pattern).
//
// Exit code 0 = both checks PASS, 1 = either FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kRTSize = 32;

class BgfxRenderTarget2DMsaaTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> sb_;
    bool done_   = false;
    int  result_ = 1;

    // Renders the diagonal triangle once into a RenderTarget2D with the given MultiSampleCount,
    // then repeatedly samples it back onto the backbuffer 1:1 (retrying the backbuffer-sample step
    // only, per this file's header comment) and returns the full centre-row pixel colours.
    std::vector<Color> RenderAndReadRow(GraphicsDevice& device, int multiSampleCount,
                                        DepthFormat depthFormat)
    {
        RenderTarget2D rt(device, kRTSize, kRTSize, false, SurfaceFormat::Color,
                           depthFormat, multiSampleCount, RenderTargetUsage::DiscardContents);

        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        device.SetRenderTarget(&rt);
        device.Clear(Color(0, 0, 0, 255));

        BasicEffect fx(device);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.VertexColorEnabled = true;
        fx.Apply();

        const Color white(255, 255, 255, 255);
        const VertexPositionColor tri[3] = {
            { Vector3(-1.0f,  1.0f, 0.0f), white },
            { Vector3( 1.0f,  1.0f, 0.0f), white },
            { Vector3(-1.0f, -1.0f, 0.0f), white },
        };
        device.DrawUserPrimitives(PrimitiveType::TriangleList, tri, 0, 1);

        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        std::vector<Color> row(kRTSize, Color(0, 0, 0, 0));
        const Rectangle reg(0, kRTSize / 2, kRTSize, 1);
        SamplerState point = SamplerState::PointClamp;

        for (int i = 0; i < 20; ++i)
        {
            device.Clear(Color(0, 0, 0, 255));
            sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
            sb_->Draw(rt,
                      Rectangle(0, 0, kRTSize, kRTSize),
                      Rectangle(0, 0, kRTSize, kRTSize),
                      Color::White);
            sb_->End();

            device.GetBackBufferData(&reg, row.data(), 0, kRTSize);

            bool anyNonBlack = false;
            for (const auto& c : row)
            {
                if (c.getRProperty() > 10 || c.getGProperty() > 10 || c.getBProperty() > 10)
                {
                    anyNonBlack = true;
                    break;
                }
            }
            if (anyNonBlack) break;
        }
        return row;
    }

    static bool IsBinary(const std::vector<Color>& row)
    {
        for (const auto& c : row)
        {
            const int v = c.getRProperty();
            if (v > 40 && v < 215) return false;
        }
        return true;
    }

    static bool HasIntermediate(const std::vector<Color>& row)
    {
        for (const auto& c : row)
        {
            const int v = c.getRProperty();
            if (v > 40 && v < 215) return true;
        }
        return false;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(device);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();

        const std::vector<Color> noMsaaRow = RenderAndReadRow(device, 0, DepthFormat::None);
        const std::vector<Color> msaaRow   = RenderAndReadRow(device, 8, DepthFormat::None);

        const bool noMsaaOk = IsBinary(noMsaaRow);
        const bool msaaOk   = HasIntermediate(msaaRow);

        std::printf("[%s] MultiSampleCount=0: diagonal edge is a hard binary transition (no AA)\n",
                    noMsaaOk ? "PASS" : "FAIL");
        std::printf("[%s] MultiSampleCount=8: diagonal edge has genuinely blended pixels (real AA)\n",
                    msaaOk ? "PASS" : "FAIL");

        // REMED-GFX-163: every case above asks for DepthFormat::None, so this file measured MSAA
        // WITHOUT depth and never reached the combination that used to abort the process inside
        // bgfx's framebuffer validation. A depth attachment must not change the resolve at all, so
        // the same two judgements are repeated with one attached -- and reaching them is itself the
        // regression gate, because pre-fix the constructor below killed the process by SIGTRAP.
        const std::vector<Color> noMsaaDepthRow =
            RenderAndReadRow(device, 0, DepthFormat::Depth24Stencil8);
        const std::vector<Color> msaaDepthRow =
            RenderAndReadRow(device, 8, DepthFormat::Depth24Stencil8);

        const bool noMsaaDepthOk = IsBinary(noMsaaDepthRow);
        const bool msaaDepthOk   = HasIntermediate(msaaDepthRow);

        std::printf("[%s] MultiSampleCount=0 + Depth24Stencil8: still a hard binary transition\n",
                    noMsaaDepthOk ? "PASS" : "FAIL");
        std::printf("[%s] MultiSampleCount=8 + Depth24Stencil8: still genuinely blended pixels -- a "
                    "depth attachment does not disturb the colour resolve\n",
                    msaaDepthOk ? "PASS" : "FAIL");

        if (!noMsaaOk)
        {
            std::printf("[INFO] MultiSampleCount=0 row unexpectedly has intermediate pixels — "
                        "either a rasterizer quirk or a false positive in the differential test.\n");
        }
        if (!msaaOk)
        {
            std::printf("[INFO] MultiSampleCount=8 row is purely binary — MSAA resolve is not "
                        "actually averaging sub-pixel coverage.\n");
        }

        result_ = (noMsaaOk && msaaOk && noMsaaDepthOk && msaaDepthOk) ? 0 : 1;
        Exit();
    }

public:
    BgfxRenderTarget2DMsaaTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(320);
        gdm_->setPreferredBackBufferHeightProperty(240);
    }

    int getResult() const { return result_; }
};

int main()
{
    BgfxRenderTarget2DMsaaTest game;
    game.Run();
    return game.getResult();
}
