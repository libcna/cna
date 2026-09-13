// SPDX-License-Identifier: MS-PL
// WEBGPU-58: verify real MSAA support for both the swapchain backbuffer and RenderTarget2D on the
// WebGPU renderer. Before this task every WGPURenderPipeline hardcoded WGPUMultisampleState.count=1
// and IGraphicsRenderer::ApplyMultiSampleCount()/GetMultiSampleCount() were unoverridden (silently
// returning the base no-op 0/"unsupported" default), so GraphicsDeviceManager.PreferMultiSampling
// had no effect at all on this renderer.
//
// Architecture note (see WebGPURenderer::sampleCount_'s own comment): unlike Vulkan's
// per-draw MSAA/non-MSAA pipeline selection, this renderer treats MSAA as a single RENDERER-GLOBAL
// sample count -- every one of the ~10 GetOrCreatePipeline*3D() families plus the 2 SpriteBatch
// pipelines bake the SAME sampleCount_ into their own WGPUMultisampleState.count, invalidated and
// rebuilt lazily whenever ApplyMultiSampleCount() actually changes it (a rare, not-a-hot-path
// event -- mirrors VulkanRenderer::sampleCount_'s own precedent). RenderTarget2D instances
// (WebGPURenderTargetRenderer) unconditionally mirror this global value at CONSTRUCTION time -- see
// that class's own doc comment for why a Vulkan-style per-instance opt-out is not safe given this
// renderer's single shared pipeline sample count.
//
// Methodology (matches every other MSAA test in this project family --
// easygl/vulkan/bgfx_*_msaa_test.cpp): render a diagonal-edged triangle and read back a centre row.
// A hard, unblended binary red/black transition proves no AA; genuinely blended (grayscale-ish)
// intermediate pixel values at the SAME geometry prove a real multisample resolve happened -- a
// signature a solid-fill test cannot distinguish from "MSAA was silently ignored".
//
// Check A -- backbuffer, MultiSampleCount=0 (the default): diagonal edge is a hard binary
//   transition.
// Check B -- backbuffer, PreferMultiSampling toggled + GraphicsDeviceManager.ApplyChanges() called
//   AFTER the renderer already exists (the real runtime-reconfigure path, exactly like
//   vulkan_msaa_test.cpp's own Task 902 verification -- NOT the CNAEXT
//   GraphicsDevice::RecreateRendererForMultiSampleCount() test-only escape hatch, which does not
//   work for this renderer since WebGPURenderer's constructor never reads
//   GraphicsRendererCreateArgs::multiSampleCount at all, unlike VulkanRenderer's): diagonal
//   edge now has genuinely blended pixels.
// Check C -- ApplyMultiSampleCount()'s clamped-return-value contract: requesting 8 (an unsupported
//   count -- wgpu-native/WebGPU only has two legal WGPUMultisampleState.count values, 1 and 4,
//   unlike Vulkan's wider 1/2/4/8/16/32/64 range) must report back exactly 0 or 4 (XNA's own "0 =
//   disabled" PresentationParameters.MultiSampleCount convention), NEVER the raw unsupported
//   request (8).
// Check D -- a RenderTarget2D created AFTER backbuffer MSAA is engaged mirrors the renderer's
//   global state: getMultiSampleCountProperty() reports the SAME real applied value as Check C's
//   backbuffer value (not the RT constructor's own requested value, which is deliberately passed
//   as 0 here to prove the mirroring is unconditional), and a diagonal-edged triangle drawn into it
//   (sampled back via SpriteBatch, same methodology as
//   vulkan_rendertarget2d_msaa_test.cpp/easygl_rendertarget2d_msaa_test.cpp) shows the same
//   genuinely blended signature as Check B.
// Check E -- consistency cross-check: Check C's clamped-return value and Check B's direct visual
//   blending evidence must agree (applied==4 iff blending was actually observed) -- catches a
//   renderer that claims a sample count it never really applies, or vice versa.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.
//
// RESOLVED (2026-07-18, WEBGPU-58): Checks B, D (2/2) and E previously FAILed on this
// implementation (3/6). Extensive investigation of the MSAA *infrastructure* itself found nothing
// wrong there -- ApplyMultiSampleCount()'s clamped-return-value contract (Check C),
// PickSampleCount()/Supports4xMsaa()'s genuine device-capability probe (empirically found this
// device supports exactly 4x, never assumed), RenderTarget2D's unconditional mirroring of the
// renderer's global sample count (Check D 1/2), and the multisampled colour texture + resolveTarget
// wiring on both the backbuffer and RenderTarget2D render passes were all independently confirmed
// correct. The real root cause was in THIS TEST, not the renderer: BasicEffect.Apply() leaves
// whatever RasterizerState the device already has (the XNA default, CullCounterClockwiseFace), and
// this test's diagonal triangle -- unlike every other WebGPU 3D test in this suite
// (webgpu_colored3d_test.cpp, webgpu_graphicsstate_test.cpp, etc., all of which explicitly set
// RasterizerState::CullNone before drawing) -- never overrode that default. The triangle's winding
// is a genuine XNA BACK face under WebGPURenderer::ToWGPUCullMode()'s mapping (independently
// verified correct by WebGPU_GraphicsState's own non-vacuous, real differential cull-mode checks),
// so it was being legitimately backface-culled on EVERY sample count, not just under MSAA -- this
// has nothing to do with MSAA at all. Check A's own assertion (IsBinary(), which only rejects
// intermediate/antialiased pixel values) could not detect this: an entirely-background row (nothing
// drawn) is trivially "binary", so Check A was passing vacuously the whole time, masking the real
// defect until Check B/D2's stronger HasIntermediate() assertion (which requires real content)
// exposed it. The fix: both RenderBackbufferRow() and RenderRTRow() now explicitly set
// RasterizerState::CullNone before drawing, matching the established pattern of every other WebGPU
// 3D test -- this test's actual subject is MSAA resolve quality, not backface culling (already
// covered by WebGPU_GraphicsState). No production renderer code changed; WebGPURenderer.cpp
// is untouched by this fix. See plans/plan_webgpu.md's WEBGPU-58 row for the full investigation writeup.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kBackbufferSize = 64;
    constexpr int kRTSize = 32;

    bool IsBinary(const std::vector<Color>& row)
    {
        for (const auto& c : row)
        {
            const int v = c.getRProperty();
            if (v > 40 && v < 215) return false;
        }
        return true;
    }

    bool HasIntermediate(const std::vector<Color>& row)
    {
        for (const auto& c : row)
        {
            const int v = c.getRProperty();
            if (v > 40 && v < 215) return true;
        }
        return false;
    }
}

class WebGpuMsaaTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> sb_;
    bool done_ = false;
    int passCount_ = 0;
    int result_ = 1;

    void check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        if (ok) ++passCount_;
    }

    // Renders a diagonal-edged triangle directly to the backbuffer (at whichever MultiSampleCount
    // is currently engaged) and returns the full centre-row pixel colours.
    std::vector<Color> RenderBackbufferRow(GraphicsDevice& device)
    {
        const auto& vp = device.getViewportProperty();
        const auto& pp = device.getPresentationParametersProperty();
        // Read within the AUTHORITATIVE backbuffer, not the viewport: under FixedHeightDynamicWidth the
        // logical viewport width is aspect-scaled and can exceed the backbuffer, and REMED-GFX-165's
        // GetBackBufferData now (correctly) rejects a rectangle that runs past the backbuffer bounds.
        const int W = std::min(vp.getWidthProperty(), pp.getBackBufferWidthProperty());
        const int H = std::min(vp.getHeightProperty(), pp.getBackBufferHeightProperty());

        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        device.setBlendStateProperty(BlendState::Opaque);
        // WEBGPU-58 root-cause fix: this test's diagonal triangle is wound as a genuine XNA
        // BACK face (verified against WebGPURenderer::ToWGPUCullMode()'s own
        // empirically-correct, independently-tested mapping in webgpu_graphicsstate_test.cpp --
        // see that test's DrawWindingQuad() derivation). BasicEffect.Apply() otherwise leaves
        // whatever RasterizerState the device already has (default CullCounterClockwiseFace),
        // which would legitimately cull this triangle regardless of MSAA -- exactly like every
        // other WebGPU 3D test in this suite (webgpu_colored3d_test.cpp,
        // webgpu_graphicsstate_test.cpp, etc.), explicitly disable culling since this test's own
        // subject is MSAA resolve quality, not backface culling (already covered by
        // WebGPU_GraphicsState).
        device.setRasterizerStateProperty(RasterizerState::CullNone);
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

        std::vector<Color> row(W, Color(0, 0, 0, 0));
        const Rectangle reg(0, H / 2, W, 1);
        device.GetBackBufferData(&reg, row.data(), 0, W);
        return row;
    }

    // Renders the same diagonal triangle into a RenderTarget2D, samples it back onto the
    // backbuffer 1:1 via SpriteBatch (PointClamp, to avoid bilinear-filter blur masquerading as
    // the MSAA blending signature), and returns the full centre-row pixel colours -- same "sample
    // back, don't GetData() the RT directly" methodology as
    // vulkan_rendertarget2d_msaa_test.cpp/easygl_rendertarget2d_msaa_test.cpp.
    std::vector<Color> RenderRTRow(GraphicsDevice& device, RenderTarget2D& rt)
    {
        device.SetRenderTarget(&rt);
        device.setBlendStateProperty(BlendState::Opaque);
        // See RenderBackbufferRow()'s identical comment: this triangle's winding is a genuine
        // XNA back face, unrelated to MSAA -- disable culling so this test isolates MSAA resolve
        // behaviour only.
        device.setRasterizerStateProperty(RasterizerState::CullNone);
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

        device.Clear(Color(0, 0, 0, 255));
        SamplerState point = SamplerState::PointClamp;
        sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
        sb_->Draw(rt, Rectangle(0, 0, kRTSize, kRTSize), Rectangle(0, 0, kRTSize, kRTSize), Color::White);
        sb_->End();

        std::vector<Color> row(kRTSize, Color(0, 0, 0, 0));
        const Rectangle reg(0, kRTSize / 2, kRTSize, 1);
        device.GetBackBufferData(&reg, row.data(), 0, kRTSize);
        return row;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        sb_ = std::make_unique<SpriteBatch>(getGraphicsDeviceProperty());
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();

        // Check A: baseline, no MSAA engaged yet.
        const std::vector<Color> noMsaaRow = RenderBackbufferRow(device);
        const bool checkA = IsBinary(noMsaaRow);
        check(checkA, "Check A: MultiSampleCount=0 backbuffer diagonal edge is a hard binary "
                      "transition (no AA)");

        // Check B: the REAL runtime path -- toggle PreferMultiSampling and re-apply on the
        // ALREADY-CONSTRUCTED renderer (Task 902's own GraphicsDeviceManager plumbing), exactly
        // like vulkan_msaa_test.cpp's own verification. GraphicsDeviceManager's own
        // "PreferMultiSampling==true -> request 8" default (GraphicsDeviceManager.cpp) exercises
        // this renderer's clamp-down-to-4 behaviour, not merely a pass-through of an
        // already-legal value.
        gdm_->setPreferMultiSamplingProperty(true);
        gdm_->ApplyChanges();
        const std::vector<Color> msaaRow = RenderBackbufferRow(device);
        const bool checkB = HasIntermediate(msaaRow);
        check(checkB, "Check B: PreferMultiSampling+ApplyChanges() AFTER construction genuinely "
                      "re-engages backbuffer MSAA (blended diagonal edge)");

        // Check C: ApplyMultiSampleCount()'s clamped-return-value contract -- must never echo
        // back the raw unsupported request (8); only 0 or 4 are legal reported WebGPU sample
        // counts (see PickSampleCount()'s own comment: wgpu-native/WebGPU has no adjustable
        // per-device "max sample count" limit at all, unlike Vulkan -- only 1 and 4 are ever
        // legal WGPUMultisampleState.count values).
        const int appliedMultiSampleCount = device.getPresentationParametersProperty().getMultiSampleCountProperty();
        std::printf("[INFO] PresentationParameters.MultiSampleCount after requesting 8 -> %d\n",
                    appliedMultiSampleCount);
        const bool checkC = (appliedMultiSampleCount == 0 || appliedMultiSampleCount == 4);
        check(checkC, "Check C: ApplyMultiSampleCount(8) clamps to a legal WebGPU value (0 or 4), "
                      "never echoes back the raw unsupported request");

        // Check D (WEBGPU-165): a RenderTarget2D created AFTER backbuffer MSAA is engaged honours
        // its OWN request, not the renderer's global count. This check used to assert the opposite
        // -- WebGPURenderTargetRenderer mirrored the global unconditionally, because every pipeline
        // baked one renderer-global sample count and a target that opted out would have been
        // pipeline-incompatible. WEBGPU-197 made the count a per-pass property, so a target that
        // asks for none gets none while the backbuffer stays multisampled, which is what XNA means
        // by a per-target MultiSampleCount and what the reference renderer has always done.
        RenderTarget2D rt(device, kRTSize, kRTSize, false, SurfaceFormat::Color,
                           DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
        const int rtMultiSampleCount = rt.getMultiSampleCountProperty();
        std::printf("[INFO] RenderTarget2D created after enabling backbuffer MSAA (requested "
                    "multiSampleCount=0 itself) -> MultiSampleCount=%d (backbuffer %d)\n",
                    rtMultiSampleCount, appliedMultiSampleCount);
        const bool checkD1 = (rtMultiSampleCount == 0);
        check(checkD1, "Check D (1/2): a RenderTarget2D that requested multiSampleCount=0 reports 0 "
                       "even while the backbuffer is multisampled -- the per-instance request is "
                       "honoured, not overridden by the renderer's global count");

        // ... and it renders like a single-sample target: a binary diagonal, no coverage blending.
        // This is the half a property check alone cannot give, and it is now the interesting one,
        // because the backbuffer beside it IS multisampled in this very frame.
        const std::vector<Color> rtRow = RenderRTRow(device, rt);
        const bool checkD2 = IsBinary(rtRow);
        check(checkD2, "Check D (2/2): that single-sample target's diagonal edge really is binary, "
                       "with the multisampled backbuffer alive alongside it");

        // Check D (3/3), WEBGPU-165: the other direction, in the same frame. A target that DOES ask
        // for 4x gets it and blends, so the two targets differ by their own requests rather than by
        // anything global.
        RenderTarget2D rtMsaa(device, kRTSize, kRTSize, false, SurfaceFormat::Color,
                              DepthFormat::None, 4, RenderTargetUsage::DiscardContents);
        const int rtMsaaCount = rtMsaa.getMultiSampleCountProperty();
        std::printf("[INFO] sibling RenderTarget2D requesting 4 -> MultiSampleCount=%d\n",
                    rtMsaaCount);
        const std::vector<Color> rtMsaaRow = RenderRTRow(device, rtMsaa);
        const bool checkD3 = (appliedMultiSampleCount == 4)
                                 ? (rtMsaaCount == 4 && HasIntermediate(rtMsaaRow))
                                 : (rtMsaaCount == 0 && IsBinary(rtMsaaRow));
        check(checkD3, "Check D (3/3): a sibling target requesting 4x applies it and blends its "
                       "diagonal, while the single-sample target above does not -- two live targets, "
                       "two different sample counts, one frame");

        // Check E: consistency cross-check -- the clamped-return-value contract (Check C) and the
        // actual visual evidence (Check B) must agree; a renderer claiming a sample count it never
        // really applies (or vice versa) would fail this even if both individual checks above
        // happened to pass in isolation.
        const bool checkE = (appliedMultiSampleCount == 4) == checkB;
        check(checkE, "Check E: ApplyMultiSampleCount()'s reported value and the actual visual "
                      "MSAA evidence agree");

        if (!checkB || appliedMultiSampleCount != 4)
        {
            std::printf("[INFO] 4x backbuffer MSAA was not actually engaged on this device/"
                        "adapter -- either WebGPURenderer::Supports4xMsaa()'s probe "
                        "reported unsupported, or the resolve did not produce genuine blending.\n");
        }

        std::printf("=== %d/7 PASS ===\n", passCount_);
        result_ = (passCount_ == 7) ? 0 : 1;
        Exit();
    }

public:
    WebGpuMsaaTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBackbufferSize);
        gdm_->setPreferredBackBufferHeightProperty(kBackbufferSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    WebGpuMsaaTest game;
    game.Run();
    return game.getResult();
}
