// SPDX-License-Identifier: MS-PL
// REMED-GFX-142: RenderTargetUsage's DEPTH and STENCIL half.
//
// REMED-GFX-136 established the usage contract as a COLOUR contract and recorded, as an open
// question, that the three renderers with a real load action disagree about depth. This file
// settles the question from the reference rather than from the renderers, and then enforces the
// answer.
//
// ---------------------------------------------------------------------------------------------
// The public contract, established rather than assumed
// ---------------------------------------------------------------------------------------------
//
// The reference is explicit, in words, in FNA3D's own public header. `FNA3D_SetRenderTargets`
// documents its last parameter as:
//
//     preserveTargetContents:
//         Set this to 1 to store the color/depth/stencil contents for future use.
//
// -- "color/depth/stencil", not "color". FNA's `GraphicsDevice.SetRenderTargets` passes exactly
// `usage != RenderTargetUsage.DiscardContents` for that byte, which is the same rule CNA already
// centralised in `RenderTargetUsagePreservesContentsEXT()`. So RenderTargetUsage governs all three
// aspects, and `PlatformContents` maps to preservation for depth and stencil for the same reason
// and through the same one function it already does for colour.
//
// All three FNA3D drivers agree with the header:
//   * OpenGL   -- `preserveTargetContents` is ignored outright; an FBO's depth/stencil attachment
//                 simply persists across binds.
//   * SDL_GPU  -- depth uses `LOADOP_LOAD` unless a clear is pending, stencil likewise, and both
//                 store ops are unconditionally `STOREOP_STORE` ("We always have to store just in
//                 case changing render state breaks the render pass").
//   * D3D11    -- `preserveTargetContents` is ignored; `OMSetRenderTargets` has no load action and
//                 a DSV's contents persist.
//
// And `DiscardContents` is NOT "undefined" in this project or in FNA. FNA's own
// `GraphicsDevice.SetRenderTargets` ends with
//
//     if (clearTarget == RenderTargetUsage.DiscardContents)
//         Clear(ClearOptions.Target | ClearOptions.DepthBuffer | ClearOptions.Stencil,
//               DiscardColor, Viewport.MaxDepth, 0);
//
// -- a deterministic replacement for all three aspects: colour to the discard colour, depth to
// `Viewport.MaxDepth` (1.0), stencil to 0. CNA's shared layer already mirrors the colour and depth
// halves of that; REMED-GFX-142 adds the stencil flag it was missing, so the enum value has one
// meaning instead of two.
//
// Cube targets: FNA's `RenderTargetCube` allocates exactly ONE `glDepthStencilBuffer` for the whole
// cube (`FNA3D_GenDepthStencilRenderbuffer(device, Size, Size, DepthStencilFormat,
// MultiSampleCount)`), not one per face. Depth/stencil is therefore a PER-TARGET resource that all
// six faces share -- switching faces neither switches depth buffers nor gives you a fresh one.
// That is the opposite of REMED-GFX-141's colour conclusion, and it is deliberate: colour lives in
// six independent cube-face subresources, depth in one. Check U2 is the discriminator and it
// REQUIRES the shared answer, so nothing here quietly grows six depth surfaces per cube.
//
// MSAA: depth is never resolved (FNA allocates one multisampled depth renderbuffer and reads it
// back through nothing), so preserving it means the MULTISAMPLE depth attachment itself must
// survive a bind cycle. No fake depth resolve is introduced anywhere to fake that.
//
// ---------------------------------------------------------------------------------------------
// The oracle
// ---------------------------------------------------------------------------------------------
//
// Depth contents are observable only indirectly, through what a later depth-tested draw does, so
// every depth check here is a rendering sequence read back as colour. The surface is split into
// three vertical bands and each band answers a different question, so one readback separates
// "depth was preserved" from the three things that could otherwise be mistaken for it:
//
//   band 0 -- cycle 1 draws colour A at z=0.25, cycle 2 draws colour B at z=0.75.
//             THE DEPTH ORACLE. Preserved depth rejects B (band reads A); cleared or discarded
//             depth accepts it (band reads B).
//   band 1 -- cycle 1 draws colour A, cycle 2 draws nothing.
//             COLOUR CONTROL. Proves colour preservation independently, so "depth was lost" can
//             never be reported when what actually happened is that colour was lost.
//   band 2 -- cycle 1 draws colour A at z=0.25, cycle 2 draws colour B at z=0.10 (NEARER).
//             DRAW CONTROL. B must win here on every renderer and in every mode. A second cycle
//             that silently did not draw at all would otherwise make band 0 read A and look like
//             a pass.
//
// So a preserving target reads A/A/B; a discarding target reads B/discard-colour/B; a renderer that
// lost colour reads something else in band 1; a renderer whose second pass never ran reads A in
// band 2. All four are distinct.
//
// Stencil gets its own sequence, with the same three-way separation:
//   cycle 1 stamps StencilFunction=Always, StencilPass=Replace, ReferenceStencil=1 over bands 0
//           and 1, in colour S.
//   cycle 2 draws the WHOLE surface in colour G, gated by StencilFunction=Equal,
//           ReferenceStencil=1, StencilPass=Keep.
// Preserved stencil  -> G, G, background   (the gate passed exactly where the stamp was)
// Cleared stencil    -> S, S, background   (the gate passed nowhere)
// Ignored stencil    -> G, G, G            (the gate passed everywhere)
// Lost colour        -> band 1 is neither S nor G
// Those four outcomes are mutually exclusive, so "stencil test unsupported" can never be scored as
// "stencil preserved" -- which is why checks C1/C2 run first and classify the renderer's ability to
// depth-test and stencil-test INSIDE a render target at all, in a single bind cycle, before any
// preservation claim is made about it.
//
// Every check is a public XNA sequence: RenderTarget2D/RenderTargetCube, SetRenderTarget,
// Clear, DepthStencilState, BasicEffect, DrawUserPrimitives, GetData. Nothing reads native depth
// or stencil memory, and no check treats the absence of a validation message as evidence.
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
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/StencilOperation.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "System/NotSupportedException.hpp"

#include <array>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kBBW  = 64;  ///< Backbuffer width.
    constexpr int kBBH  = 64;  ///< Backbuffer height.
    constexpr int kRT   = 12;  ///< Render-target edge (2D and cube face). 3 bands of 4 texels.
    constexpr int kBand = kRT / 3;
    constexpr int kMsaaRequest = 4;  ///< Multisample count requested by the MSAA checks.

    /// Depths used by the two cycles. Ordered so the band-0 pair is unambiguous under LessEqual.
    constexpr float kNear   = 0.25f;  ///< Cycle-1 occluder.
    constexpr float kFar    = 0.75f;  ///< Cycle-2 probe -- rejected iff depth was preserved.
    constexpr float kNearer = 0.10f;  ///< Cycle-2 draw control -- must always win.

    /**
     * @brief What a rendered render target's public readback must do on this renderer.
     *
     * `Exact` -- GetData returns the rendered surface byte for byte.
     * `Unsupported` -- GetData raises System::NotSupportedException with the caller's destination
     * untouched (REMED-GFX-127/130's contract).
     */
    enum class Support
    {
        Exact,
        Unsupported,
    };

    /// The complete, reviewed per-renderer claim this file enforces.
    struct Contract
    {
        const char* name;
        Support readback;      ///< RenderTarget2D::GetData at level 0.
        bool    cubeTargets;   ///< RenderTargetCube can be created AND bound here.
        Support cubeReadback;  ///< RenderTargetCube::GetData at level 0.
        /**
         * A depth-tested draw INSIDE a RenderTarget2D really is gated by that target's own depth
         * buffer. Check C1 enforces this before any depth-preservation claim is made, so a renderer
         * that cannot depth-test at all is never scored as "preserved".
         */
        bool    depthInRT;
        /**
         * A stencil-tested draw INSIDE a RenderTarget2D really is gated by that target's own
         * stencil buffer, i.e. the gate passes exactly where the stamp was and nowhere else.
         * Check C2 enforces it, in one bind cycle, before any stencil-preservation claim.
         */
        bool    stencilInRT;
        /**
         * REMED-GFX-142's subject. `PreserveContents`/`PlatformContents` keep DEPTH across a full
         * unbind/rebind cycle, per the FNA3D contract quoted in this file's header. False is a
         * declared defect: pre-fix it is false for VULKAN (LOAD_OP_DONT_CARE for depth in the
         * preserving RT render pass) and for WEBGPU's cube leg (an unconditional
         * WGPULoadOp_Clear).
         */
        bool    depthPreserves;
        /// The same claim for STENCIL. Only meaningful where `stencilInRT` is true.
        bool    stencilPreserves;
        /**
         * A `Clear()` issued AFTER a draw inside the SAME bind cycle wipes that draw, per XNA's
         * command order (REMED-GFX-129's subject on Vulkan). False on a renderer whose only clear
         * mechanism is still the pass load action, so a mid-cycle clear is hoisted to the front of
         * the cycle -- check K7 then asserts the COLLAPSED result, which stays falsifiable in both
         * directions and turns red the day that renderer gains ordered clears.
         */
        bool    orderedClearInCycle;
        /**
         * A multisampled RenderTarget2D with a real DepthFormat can be created and bound at all.
         * True everywhere now. It was false on bgfx, where `bgfx::isFrameBufferValid` refuses a
         * multisampled depth attachment whose texture lacks `BGFX_TEXTURE_RT_WRITE_ONLY` and ABORTS
         * the process. REMED-GFX-141 fixed exactly that on the cube leg and recorded the 2D leg as a
         * separate finding; REMED-GFX-163 closed it, so check D8 builds the target on bgfx again.
         */
        bool    msaaDepthRT2D;
        /// The same for a multisampled depth-backed RenderTargetCube (REMED-GFX-141 fixed bgfx's).
        bool    msaaDepthCube;
        /**
         * A multisampled RenderTarget2D/RenderTargetCube reads back its RESOLVED content, so an
         * MSAA depth check can say anything at all. False on bgfx, whose blit from a multisampled
         * attachment reports success over untouched memory (REMED-GFX-134/138).
         */
        bool    msaaReadback;
        /**
         * The same question for a multisampled RenderTargetCube FACE, declared separately because
         * the two routes diverged. REMED-GFX-154 fixed the RenderTarget2D resolve on bgfx, but a
         * multisampled cube face there is still a deterministic REFUSAL rather than a zero read
         * (`BgfxRenderTargetCubeRenderer::GetData` returns false for any multisampled target) --
         * REMED-GFX-134's separately recorded boundary, on a different helper path, which
         * REMED-GFX-154 deliberately did not expand into. One field could not describe both once
         * only one of them was fixed, and check U4 turned red saying so.
         */
        bool    msaaCubeReadback;
        /**
         * A multisampled RenderTarget2D honours `PreserveContents` at all. False on Vulkan, where
         * `VulkanRenderTargetRenderer::GetRenderPass()` hands its MSAA leg a hardcoded
         * `discardContents=true` -- COLOUR and DEPTH alike. That is the multisampled-RT2D
         * preservation finding REMED-GFX-141 recorded and deliberately did not fix (its cube
         * sibling passes `!preserveContents_` and is correct), so check D8 asserts the discarding
         * outcome there instead of claiming a depth result the renderer cannot give.
         */
        bool    msaaRt2dPreserves;
        /**
         * `DrawUserPrimitives` works at all. False on the 2D-only renderers, which throw
         * `std::runtime_error("... does not support 3D: CreateVertexBuffer")` from the first draw.
         * Every depth/stencil observation in this file is a depth-tested or stencil-gated DRAW, so
         * where this is false no content can be measured -- but the whole public sequence
         * (construct, bind, Clear, unbind, read, dispose) still runs and still has to be legal,
         * which is what those renderers are registered here to prove.
         */
        bool    drawsUserPrimitives;
        /**
         * Ask for a multisampled BACKBUFFER. Only true where that is the sole way a render target
         * can engage MSAA: Vulkan gates its RT MSAA resources on the renderer's own sampleCount_.
         */
        bool    preferMultiSampling;
        bool    wantHiDefProfile;  ///< Request GraphicsProfile::HiDef.
    };

    // Field order:
    //   name, readback, cubeTargets, cubeReadback,
    //   depthInRT, stencilInRT, depthPreserves, stencilPreserves, orderedClearInCycle,
    //   msaaDepthRT2D, msaaDepthCube, msaaReadback, msaaCubeReadback, msaaRt2dPreserves,
    //   drawsUserPrimitives,
    //   preferMultiSampling, wantHiDefProfile
#if defined(CNA_RENDERER_HEADLESS)
    // Headless rasterizes nothing, so it owns no colour at all and its readback is
    // REMED-GFX-127/130's deterministic refusal. Every sequence must still be legal.
    constexpr Contract kContract{"HEADLESS", Support::Unsupported, true, Support::Unsupported,
                                 false, false, false, false, true,
                                 true, true, false, false, true, true, false, false};
#elif defined(CNA_RENDERER_SOFTWARE)
    // No RenderTargetCube. A real CPU depth buffer per framebuffer, owned by that framebuffer and
    // never shared, so depth simply persists. NO stencil storage at all
    // (`SoftwareRenderer::ClearStencil` is an empty body and nothing rasterises a stencil
    // test), so C2 declares stencil unsupported and every stencil check reports that boundary
    // instead of a result it cannot have.
    constexpr Contract kContract{"SOFTWARE", Support::Exact, false, Support::Unsupported,
                                 true, false, true, false, true,
                                 true, true, false, false, true, true, false, false};
#elif defined(CNA_RENDERER_EASYGL)
    // A real depth renderbuffer per target, attached once at construction and never re-attached;
    // no glInvalidateFramebuffer and no clear-on-bind anywhere, so an FBO's depth/stencil simply
    // persists -- exactly FNA3D's OpenGL driver.
    constexpr Contract kContract{"EASYGL", Support::Exact, true, Support::Exact,
                                 true, true, true, true, true,
                                 true, true, true, true, true, true, false, false};
#elif defined(CNA_RENDERER_BGFX)
    // Render-target views are left at BGFX_CLEAR_NONE, so the depth attachment persists.
    // `msaaDepthRT2D` was false while a multisampled depth-backed RenderTarget2D aborted the process
    // here; REMED-GFX-163 fixed that, so check D8 builds the target again. `msaaReadback` was then
    // false because a multisampled target's readback reported success over untouched memory;
    // REMED-GFX-154 fixed THAT, so D8 now measures multisampled depth instead of skipping it.
    constexpr Contract kContract{"BGFX", Support::Exact, true, Support::Exact,
                                 true, true, true, true, true,
                                 true, true, true, false, true, true, false, false};
#elif defined(CNA_RENDERER_VULKAN)
    // `msaaRt2dPreserves` false: the multisampled RenderTarget2D leg hardcodes
    // `discardContents=true`, discarding colour AND depth -- REMED-GFX-141's recorded finding.
    constexpr Contract kContract{"VULKAN", Support::Exact, true, Support::Exact,
                                 true, true, true, true, true,
                                 true, true, true, true, false, true, true, false};
#elif defined(CNA_RENDERER_WEBGPU)
    // `stencilInRT` false: `WebGPURenderer::ApplyDepthStencilState` stores the stencil
    // state and deliberately never bakes it into any pipeline's `WGPUStencilFaceState` (WEBGPU-83),
    // so every fragment passes the stencil test -- check C2 measures exactly that and is the
    // reason no stencil-preservation result is claimed here.
    // `orderedClearInCycle` was false while REMED-GFX-156 was open: a mid-cycle Clear was still
    // delivered through the pass load action, so it could not wipe a draw recorded before it
    // (check K7). It cuts the bind cycle into one native pass per observable Clear now, so K7
    // asserts the ordered result here; it was measured red the moment the fix landed.
    constexpr Contract kContract{"WEBGPU", Support::Exact, true, Support::Exact,
                                 true, false, true, false, true,
                                 true, true, false, false, true, true, false, false};
#elif defined(CNA_RENDERER_SDL_GPU)
    // Already FNA3D-shaped: every depth/stencil target info uses `clearX ? CLEAR : LOAD` with an
    // unconditional STORE, which is what FNA3D's own SDL_GPU driver does.
    // `orderedClearInCycle` was false while REMED-GFX-156 was open, the same boundary as WebGPU
    // (check K7); both now segment the bind cycle at every observable Clear.
    constexpr Contract kContract{"SDL_GPU", Support::Exact, true, Support::Exact,
                                 true, true, true, true, true,
                                 true, true, true, true, true, true, false, false};
#elif defined(CNA_RENDERER_SDL_RENDERER)
    constexpr Contract kContract{"SDL_RENDERER", Support::Exact, false, Support::Unsupported,
                                 false, false, false, false, true,
                                 true, true, false, false, true, false, false, false};
#elif defined(CNA_RENDERER_CANVAS)
    constexpr Contract kContract{"CANVAS", Support::Exact, false, Support::Unsupported,
                                 false, false, false, false, true,
                                 true, true, false, false, true, false, false, false};
#elif defined(CNA_RENDERER_FREEDIRECT)
    constexpr Contract kContract{"FREEDIRECT", Support::Exact, false, Support::Unsupported,
                                 false, false, false, false, true,
                                 true, true, false, false, true, false, false, false};
#elif defined(CNA_RENDERER_DIRECTX9)
    constexpr Contract kContract{"DIRECTX9", Support::Exact, true, Support::Exact,
                                 true, true, true, true, true,
                                 true, true, false, false, true, true, false, true};
#elif defined(CNA_RENDERER_DIRECTX11)
    constexpr Contract kContract{"DIRECTX11", Support::Exact, true, Support::Exact,
                                 true, true, true, true, true,
                                 true, true, true, true, true, true, false, false};
#elif defined(CNA_RENDERER_DIRECTX12)
    constexpr Contract kContract{"DIRECTX12", Support::Exact, true, Support::Exact,
                                 true, true, true, true, true,
                                 true, true, true, true, true, true, false, false};
#elif defined(CNA_RENDERER_SOKOL)
    // plans/plan_sokol.md SOKOL-25/26/38: RenderTarget2D AND RenderTargetCube can both be created and
    // bound, and both now have a working single-sample GetData (SokolRenderTargetRenderer/
    // SokolRenderTargetCubeRenderer read back their real colour content via a throwaway GL FBO
    // around the raw GL texture sg_gl_query_image_info() exposes), so `readback`/`cubeReadback`
    // are Exact and every readback-driven check in this file (U1-U4 in particular) measures real
    // pixels. `msaaReadback` stays false, conservatively unclaimed: a multisampled RenderTarget2D's
    // own multisample colour image has a DONTCARE store action at every pass end (SOKOL-26's own
    // resolve-then-discard design, matching sokol_gfx.h's documented MSAA workflow), so whether its
    // content survives an unbind/rebind PreserveContents cycle the way D8 requires is genuinely
    // unverified here, not just untested -- D8 stays skipped rather than asserting an unmeasured
    // claim either way. `stencilInRT` and `stencilPreserves` true (plans/plan_sokol.md SOKOL-23): ApplyDepthStencilState now wires the
    // real stencil state into Pipeline3DKey/Get3DPipeline (front/back ops, compare, masks and
    // reference are baked into the sokol_gfx pipeline itself, since sg_stencil_state.ref is
    // pipeline state there, not dynamic). Depth and stencil share the one real depth-stencil image
    // CreateRenderTarget2D/CreateRenderTargetCube always allocate (docs/sokol-renderer.md: every
    // non-None DepthFormat gets the combined SG_PIXELFORMAT_DEPTH_STENCIL, never a depth-only
    // format -- and for a cube, ONE 2D depth-stencil image shared by all six faces, exactly what
    // U2 measures), and BeginPassIfNeeded's LOAD action applies to both attachments identically,
    // so stencil persists across a bind cycle exactly like depth already does
    // (Sokol_RenderTarget2D_Depth's own proof). `orderedClearInCycle` true: QueueClear ends the
    // active pass so a mid-cycle Clear cannot be reordered against an earlier draw -- the same
    // "GraphicsDevice itself clears a DiscardContents target" convention EasyGL documents.
    // `msaaDepthRT2D` true: a multisampled, depth-backed RenderTarget2D is real as of SOKOL-26 (a
    // genuine multisample colour + matching-sample-count depth image, not a silent clamp) and can
    // be created and bound. `msaaDepthCube` true for the opposite reason: CreateRenderTargetCube
    // still silently clamps `multiSampleCount` to 1 (RenderTargetCube MSAA is a permanent sokol_gfx
    // boundary -- SG_IMAGETYPE_CUBE rejects sample_count > 1 outright), so it too can always be
    // created and bound, just never genuinely multisampled; either way the D8/U-series checks that
    // would measure MSAA depth content stay skipped via `msaaReadback` above -- and so does
    // `msaaCubeReadback`, a field this file gained after this lane was branched, which is false
    // because a multisampled cube can never exist here at all.
    constexpr Contract kContract{"SOKOL", Support::Exact, true, Support::Exact,
                                 true, true, true, true, true,
                                 true, true, false, false, true, true, false, false};
#else
#error "REMED-GFX-142: this renderer has no declared render-target depth/stencil contract."
#endif

    // --- The palette. 0/255-only, so every comparison is byte-exact even on an sRGB target. ---
    Color BgColour()      { return Color(  0,   0, 255, 255); }  ///< Cycle-1 clear colour (blue).
    Color AColour()       { return Color(255,   0,   0, 255); }  ///< Cycle-1 occluder (red).
    Color BColour()       { return Color(  0, 255,   0, 255); }  ///< Cycle-2 probe (green).
    Color StampColour()   { return Color(255, 255,   0, 255); }  ///< Stencil stamp (yellow).
    Color GateColour()    { return Color(  0, 255, 255, 255); }  ///< Stencil gate (cyan).
    /// The colour GraphicsDevice::SetRenderTargets clears a DiscardContents target to on bind.
    Color DiscardColour() { return Color(  0,   0,   0, 255); }

    std::string ColorText(const Color& c)
    {
        return "(" + std::to_string(static_cast<int>(c.getRProperty())) + "," +
               std::to_string(static_cast<int>(c.getGProperty())) + "," +
               std::to_string(static_cast<int>(c.getBProperty())) + "," +
               std::to_string(static_cast<int>(c.getAProperty())) + ")";
    }

    bool Same(const Color& a, const Color& b)
    {
        return a.getRProperty() == b.getRProperty() && a.getGProperty() == b.getGProperty() &&
               a.getBProperty() == b.getBProperty() && a.getAProperty() == b.getAProperty();
    }

    /**
     * @brief A depth-testing state with an explicit compare function.
     *
     * DepthStencilState::Default is depth-enabled + write-enabled + LessEqual, which is what the
     * whole depth half of this file uses; naming it here keeps every draw's state visible at the
     * call site rather than relying on whatever the device was last left in.
     */
    DepthStencilState DepthTesting()
    {
        DepthStencilState ds;
        ds.setDepthBufferEnableProperty(true);
        ds.setDepthBufferWriteEnableProperty(true);
        ds.setDepthBufferFunctionProperty(CompareFunction::LessEqual);
        ds.setStencilEnableProperty(false);
        return ds;
    }

    /// Writes ReferenceStencil into the stencil buffer wherever it draws. Depth plays no part.
    DepthStencilState StencilStamp()
    {
        DepthStencilState ds;
        ds.setDepthBufferEnableProperty(false);
        ds.setDepthBufferWriteEnableProperty(false);
        ds.setStencilEnableProperty(true);
        ds.setStencilFunctionProperty(CompareFunction::Always);
        ds.setStencilPassProperty(StencilOperation::Replace);
        ds.setStencilFailProperty(StencilOperation::Keep);
        ds.setStencilDepthBufferFailProperty(StencilOperation::Replace);
        ds.setReferenceStencilProperty(1);
        return ds;
    }

    /// Draws only where the stencil buffer already equals 1, and never modifies it.
    DepthStencilState StencilGate()
    {
        DepthStencilState ds;
        ds.setDepthBufferEnableProperty(false);
        ds.setDepthBufferWriteEnableProperty(false);
        ds.setStencilEnableProperty(true);
        ds.setStencilFunctionProperty(CompareFunction::Equal);
        ds.setStencilPassProperty(StencilOperation::Keep);
        ds.setStencilFailProperty(StencilOperation::Keep);
        ds.setStencilDepthBufferFailProperty(StencilOperation::Keep);
        ds.setReferenceStencilProperty(1);
        return ds;
    }

    /// What one band of a readback says, as a single classified answer.
    struct Bands
    {
        bool        threwNotSupported  = false;
        bool        threwSomethingElse = false;
        std::string otherWhat;
        /// Per band: the one colour every sampled texel of that band carried, or `mixed`.
        std::array<Color, 3> colour{Color(0, 0, 0, 0), Color(0, 0, 0, 0), Color(0, 0, 0, 0)};
        std::array<bool, 3>  uniform{false, false, false};
        std::string          detail;
    };
}

/// REMED-GFX-142's public depth/stencil lifetime oracle.
class RenderTargetDepthStencilUsageTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<BasicEffect> fx_;
    bool done_ = false;
    bool warmedUp_ = false;
    int  passCount_ = 0;
    int  totalCount_ = 0;
    int  result_ = 1;
    int  appliedMsaa_ = 0;

    void check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        ++totalCount_;
        if (ok) ++passCount_;
    }

    /// A boundary this renderer genuinely does not have, recorded as a pass with its reason.
    void skip(const std::string& label) { check(true, label + " -- SKIPPED"); }

    // -----------------------------------------------------------------------------------------
    // Drawing
    // -----------------------------------------------------------------------------------------

    /// Identity World/View/Projection, so a vertex's z IS its clip-space depth.
    void ApplyEffect()
    {
        fx_->setWorldProperty(Matrix::getIdentityProperty());
        fx_->setViewProperty(Matrix::getIdentityProperty());
        fx_->setProjectionProperty(Matrix::getIdentityProperty());
        fx_->VertexColorEnabled = true;
        fx_->Apply();
    }

    /**
     * @brief Fills vertical band @p band (0..2) at clip depth @p z with @p colour.
     *
     * @p band == -1 covers the whole surface. Bands are X-only on purpose: no renderer flips X, so
     * this file is completely independent of the Y-orientation question REMED-GFX-134 owns.
     */
    void DrawBand(GraphicsDevice& dev, int band, float z, const Color& colour)
    {
        // A 2D-only renderer throws from the first DrawUserPrimitives (see drawsUserPrimitives), so
        // every sequence here degenerates to its bind/Clear/unbind/read skeleton there rather than
        // being skipped -- that skeleton still has to be legal and is exactly what those renderers
        // are registered to prove.
        if (!kContract.drawsUserPrimitives) return;
        const float x0 = (band < 0) ? -1.0f : (-1.0f + static_cast<float>(band) * (2.0f / 3.0f));
        const float x1 = (band < 0) ?  1.0f : (-1.0f + static_cast<float>(band + 1) * (2.0f / 3.0f));
        const VertexPositionColor verts[6] = {
            { Vector3(x0,  1.0f, z), colour },
            { Vector3(x0, -1.0f, z), colour },
            { Vector3(x1, -1.0f, z), colour },
            { Vector3(x0,  1.0f, z), colour },
            { Vector3(x1, -1.0f, z), colour },
            { Vector3(x1,  1.0f, z), colour },
        };
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, 2);
    }

    /// The state every draw in this file shares: opaque, unculled, effect applied.
    void PrepareDraw(GraphicsDevice& dev, const DepthStencilState& ds)
    {
        if (!kContract.drawsUserPrimitives) return;   ///< See DrawBand's identical guard.
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.setDepthStencilStateProperty(ds);
        ApplyEffect();
    }

    // -----------------------------------------------------------------------------------------
    // The two cycles
    // -----------------------------------------------------------------------------------------

    /// Cycle 1: clear, then lay colour A at kNear across all three bands.
    void DepthCycle1(GraphicsDevice& dev)
    {
        dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
                  BgColour(), 1.0f, 0);
        PrepareDraw(dev, DepthTesting());
        DrawBand(dev, 0, kNear, AColour());
        DrawBand(dev, 1, kNear, AColour());
        DrawBand(dev, 2, kNear, AColour());
    }

    /// Cycle 2: NO clear. Band 0 probes retained depth, band 2 proves the pass drew at all.
    void DepthCycle2(GraphicsDevice& dev)
    {
        PrepareDraw(dev, DepthTesting());
        DrawBand(dev, 0, kFar,    BColour());
        DrawBand(dev, 2, kNearer, BColour());
    }

    /// Cycle 1 of the stencil sequence: stamp ReferenceStencil=1 over bands 0 and 1.
    void StencilCycle1(GraphicsDevice& dev)
    {
        dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
                  BgColour(), 1.0f, 0);
        PrepareDraw(dev, StencilStamp());
        DrawBand(dev, 0, 0.5f, StampColour());
        DrawBand(dev, 1, 0.5f, StampColour());
    }

    /// Cycle 2 of the stencil sequence: one full-surface draw, gated by the retained stencil.
    void StencilCycle2(GraphicsDevice& dev)
    {
        PrepareDraw(dev, StencilGate());
        DrawBand(dev, -1, 0.5f, GateColour());
    }

    // -----------------------------------------------------------------------------------------
    // Readback
    // -----------------------------------------------------------------------------------------

    /**
     * @brief Classifies a readback into one answer per band.
     *
     * Only the interior of each band is sampled (its two middle columns, four middle rows), so a
     * rasteriser's edge rule can never decide a check. A band whose sampled texels disagree is
     * reported as non-uniform and fails whatever asserted it, rather than silently taking the
     * first texel's colour.
     */
    static void Classify(Bands& b, const std::vector<Color>& dest)
    {
        for (int band = 0; band < 3; ++band)
        {
            const int x0 = band * kBand + 1;
            bool first = true;
            b.uniform[static_cast<std::size_t>(band)] = true;
            for (int y = kRT / 2 - 2; y < kRT / 2 + 2; ++y)
                for (int x = x0; x < x0 + kBand - 2; ++x)
                {
                    const Color& c = dest[static_cast<std::size_t>(y) * kRT + x];
                    if (first) { b.colour[static_cast<std::size_t>(band)] = c; first = false; }
                    else if (!Same(c, b.colour[static_cast<std::size_t>(band)]))
                        b.uniform[static_cast<std::size_t>(band)] = false;
                }
        }
        b.detail = " [b0=" + ColorText(b.colour[0]) + (b.uniform[0] ? "" : "!mixed") +
                   " b1=" + ColorText(b.colour[1]) + (b.uniform[1] ? "" : "!mixed") +
                   " b2=" + ColorText(b.colour[2]) + (b.uniform[2] ? "" : "!mixed") + "]";
    }

    static Bands ReadTarget(const RenderTarget2D& rt)
    {
        Bands b;
        std::vector<Color> dest(static_cast<std::size_t>(kRT) * kRT, Color(0xCD, 0xCD, 0xCD, 0xCD));
        try { rt.GetData(0, nullptr, dest.data(), 0, static_cast<int>(dest.size())); }
        catch (const System::NotSupportedException&) { b.threwNotSupported = true; }
        catch (const std::exception& e) { b.threwSomethingElse = true; b.otherWhat = e.what(); }
        catch (...) { b.threwSomethingElse = true; b.otherWhat = "non-std exception"; }
        Classify(b, dest);
        return b;
    }

    static Bands ReadFace(const RenderTargetCube& cube, int face)
    {
        Bands b;
        std::vector<Color> dest(static_cast<std::size_t>(kRT) * kRT, Color(0xCD, 0xCD, 0xCD, 0xCD));
        try
        {
            cube.GetData(static_cast<CubeMapFace>(face), 0, nullptr, dest.data(), 0,
                         static_cast<int>(dest.size()));
        }
        catch (const System::NotSupportedException&) { b.threwNotSupported = true; }
        catch (const std::exception& e) { b.threwSomethingElse = true; b.otherWhat = e.what(); }
        catch (...) { b.threwSomethingElse = true; b.otherWhat = "non-std exception"; }
        Classify(b, dest);
        return b;
    }

    /// Asserts three exact band colours, after proving the readback itself behaved.
    void ExpectBands(const Bands& b, const Color& b0, const Color& b1, const Color& b2,
                     const std::string& label)
    {
        const std::string facts =
            " [threwNotSupported=" + std::string(b.threwNotSupported ? "1" : "0") +
            " threwOther=" + (b.threwSomethingElse ? ("1:" + b.otherWhat) : "0") + "]" + b.detail +
            " expected b0=" + ColorText(b0) + " b1=" + ColorText(b1) + " b2=" + ColorText(b2);
        check(!b.threwNotSupported && !b.threwSomethingElse &&
              b.uniform[0] && b.uniform[1] && b.uniform[2] &&
              Same(b.colour[0], b0) && Same(b.colour[1], b1) && Same(b.colour[2], b2),
              label + facts);
    }

    // -----------------------------------------------------------------------------------------
    // Target factories
    // -----------------------------------------------------------------------------------------

    std::unique_ptr<RenderTarget2D> MakeRT(GraphicsDevice& dev, RenderTargetUsage usage,
                                           DepthFormat depth, int samples = 0)
    {
        return std::make_unique<RenderTarget2D>(dev, kRT, kRT, false, SurfaceFormat::Color, depth,
                                                samples, usage);
    }

    std::unique_ptr<RenderTargetCube> MakeCube(GraphicsDevice& dev, RenderTargetUsage usage,
                                               DepthFormat depth, int samples = 0)
    {
        return std::make_unique<RenderTargetCube>(dev, kRT, false, SurfaceFormat::Color, depth,
                                                  samples, usage);
    }

    // -----------------------------------------------------------------------------------------
    // C -- capability controls. Nothing below means anything unless these hold.
    // -----------------------------------------------------------------------------------------

    void RunCapability(GraphicsDevice& dev)
    {
        // C1: a depth-tested draw inside a render target is really gated by that target's depth
        // buffer, within ONE bind cycle. Same shape as the preservation checks, so C1 passing and
        // D1 failing isolates the bind cycle as the only difference.
        {
            auto rt = MakeRT(dev, RenderTargetUsage::PreserveContents, DepthFormat::Depth24Stencil8);
            dev.SetRenderTarget(rt.get());
            DepthCycle1(dev);
            DepthCycle2(dev);   // same cycle, no unbind
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            const Bands b = ReadTarget(*rt);
            if (kContract.readback != Support::Exact)
                skip("C1 capability: depth testing inside a RenderTarget2D (no readback here)");
            else if (kContract.depthInRT)
                ExpectBands(b, AColour(), AColour(), BColour(),
                            "C1 capability: within ONE bind cycle a RenderTarget2D's own depth "
                            "buffer rejects the farther draw and accepts the nearer one");
            else
                check(!b.threwSomethingElse,
                      "C1 capability: this renderer declares no functional render-target depth "
                      "test; the sequence must still be legal" + b.detail);
        }

        // C2: the stencil test inside a render target passes exactly where the stamp was, in ONE
        // bind cycle. The band-2 half is the one that catches a fully-bypassed stencil test: a
        // renderer that always passes shows the gate colour there too.
        {
            auto rt = MakeRT(dev, RenderTargetUsage::PreserveContents, DepthFormat::Depth24Stencil8);
            dev.SetRenderTarget(rt.get());
            StencilCycle1(dev);
            StencilCycle2(dev);  // same cycle, no unbind
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            const Bands b = ReadTarget(*rt);
            if (kContract.readback != Support::Exact)
                skip("C2 capability: stencil testing inside a RenderTarget2D (no readback here)");
            else if (kContract.stencilInRT)
                ExpectBands(b, GateColour(), GateColour(), BgColour(),
                            "C2 capability: within ONE bind cycle the stencil gate passes exactly "
                            "where the stamp was and is rejected everywhere else");
            else
                check(!b.threwSomethingElse,
                      "C2 capability: this renderer declares no functional render-target stencil "
                      "test; the sequence must still be legal" + b.detail);
        }
    }

    // -----------------------------------------------------------------------------------------
    // D -- depth across a bind cycle, RenderTarget2D
    // -----------------------------------------------------------------------------------------

    /// The three band colours a full two-cycle depth sequence must produce for @p usage.
    void ExpectDepthCycleResult(const Bands& b, RenderTargetUsage usage, const std::string& label)
    {
        const bool discards = (usage == RenderTargetUsage::DiscardContents);
        if (discards)
            // The shared layer clears colour AND depth AND stencil on every DiscardContents bind,
            // so band 0's far draw passes the reset depth and band 1 keeps the discard colour.
            ExpectBands(b, BColour(), DiscardColour(), BColour(), label);
        else if (kContract.depthPreserves)
            ExpectBands(b, AColour(), AColour(), BColour(), label);
        else
            // Declared defect: colour survives (band 1) but the retained depth does not (band 0).
            ExpectBands(b, BColour(), AColour(), BColour(), label);
    }

    static std::string UsageName(RenderTargetUsage u)
    {
        return u == RenderTargetUsage::DiscardContents  ? "DiscardContents"
             : u == RenderTargetUsage::PreserveContents ? "PreserveContents"
                                                        : "PlatformContents";
    }

    /// One complete unbind/rebind depth sequence on a fresh target.
    void RunDepthCase(GraphicsDevice& dev, RenderTargetUsage usage, DepthFormat depth,
                      const std::string& id, const std::string& what)
    {
        auto rt = MakeRT(dev, usage, depth);
        dev.SetRenderTarget(rt.get());
        DepthCycle1(dev);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        dev.SetRenderTarget(rt.get());      // rebind, no clear
        DepthCycle2(dev);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        const Bands b = ReadTarget(*rt);
        if (kContract.readback != Support::Exact || !kContract.depthInRT)
        {
            check(!b.threwSomethingElse, id + " " + what + " -- no functional render-target depth "
                  "readback on this renderer; the sequence must still be legal" + b.detail);
            return;
        }
        ExpectDepthCycleResult(b, usage, id + " " + what + " (" + UsageName(usage) + ")");
    }

    void RunDepth(GraphicsDevice& dev)
    {
        RunDepthCase(dev, RenderTargetUsage::PreserveContents, DepthFormat::Depth24,
                     "D1", "depth survives an unbind/rebind cycle, Depth24");
        RunDepthCase(dev, RenderTargetUsage::PreserveContents, DepthFormat::Depth24Stencil8,
                     "D2", "depth survives an unbind/rebind cycle, Depth24Stencil8");
        RunDepthCase(dev, RenderTargetUsage::PlatformContents, DepthFormat::Depth24Stencil8,
                     "D3", "PlatformContents maps to preservation for depth too");
        RunDepthCase(dev, RenderTargetUsage::DiscardContents, DepthFormat::Depth24Stencil8,
                     "D4", "DiscardContents resets depth to Viewport.MaxDepth deterministically");
        RunDepthCase(dev, RenderTargetUsage::PreserveContents, DepthFormat::Depth16,
                     "D5", "depth survives an unbind/rebind cycle, Depth16");

        // D6: target A -> target B -> target A. Each target must keep its OWN depth; a renderer
        // that shares one depth attachment between two live targets fails here and nowhere else.
        {
            auto a = MakeRT(dev, RenderTargetUsage::PreserveContents, DepthFormat::Depth24Stencil8);
            auto bt = MakeRT(dev, RenderTargetUsage::PreserveContents, DepthFormat::Depth24Stencil8);
            dev.SetRenderTarget(a.get());   DepthCycle1(dev);
            dev.SetRenderTarget(bt.get());  DepthCycle1(dev);
            dev.SetRenderTarget(a.get());   DepthCycle2(dev);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            if (kContract.readback != Support::Exact || !kContract.depthInRT)
                skip("D6 two targets interleaved A->B->A");
            else
                ExpectDepthCycleResult(ReadTarget(*a), RenderTargetUsage::PreserveContents,
                                       "D6 A->B->A: target A's own depth survives a trip through "
                                       "an unrelated render target");
        }

        // D7: the route out of the bind cycle is irrelevant -- a full backbuffer round trip,
        // including a backbuffer Clear, must be transparent to the target's depth.
        {
            auto rt = MakeRT(dev, RenderTargetUsage::PreserveContents, DepthFormat::Depth24Stencil8);
            dev.SetRenderTarget(rt.get());  DepthCycle1(dev);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
                      Color(20, 20, 20, 255), 1.0f, 0);
            dev.SetRenderTarget(rt.get());  DepthCycle2(dev);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            if (kContract.readback != Support::Exact || !kContract.depthInRT)
                skip("D7 backbuffer round trip");
            else
                ExpectDepthCycleResult(ReadTarget(*rt), RenderTargetUsage::PreserveContents,
                                       "D7 backbuffer round trip: clearing the BACKBUFFER's depth "
                                       "between the two cycles does not touch the target's");
        }

        // D8: multisampled depth is never resolved, so preserving it means the MULTISAMPLE depth
        // attachment itself survived the bind cycle.
        if (!kContract.msaaDepthRT2D)
            skip("D8 multisampled depth (a multisampled depth-backed RenderTarget2D cannot be "
                 "built here -- see the msaaDepthRT2D contract field)");
        else if (kContract.readback != Support::Exact || !kContract.msaaReadback ||
                 !kContract.depthInRT)
            skip("D8 multisampled depth (no resolved readback on this renderer)");
        else
        {
            auto rt = MakeRT(dev, RenderTargetUsage::PreserveContents,
                             DepthFormat::Depth24Stencil8, kMsaaRequest);
            appliedMsaa_ = rt->getMultiSampleCountProperty();
            if (appliedMsaa_ <= 0)
                skip("D8 multisampled depth (this renderer applies no MSAA to a render target, "
                     "applied " + std::to_string(appliedMsaa_) + ")");
            else
            {
                dev.SetRenderTarget(rt.get());  DepthCycle1(dev);
                dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
                dev.SetRenderTarget(rt.get());  DepthCycle2(dev);
                dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
                const std::string label = "D8 multisampled depth at applied " +
                                          std::to_string(appliedMsaa_) + "x survives a bind cycle";
                if (kContract.msaaRt2dPreserves)
                    ExpectDepthCycleResult(ReadTarget(*rt), RenderTargetUsage::PreserveContents,
                                           label);
                else
                    // Declared defect (see msaaRt2dPreserves): the pass discards colour AND depth,
                    // so the far draw is accepted and everything it did not cover comes back at
                    // the load action's clear colour -- the last value a Clear() left behind.
                    ExpectBands(ReadTarget(*rt), BColour(), BgColour(), BColour(),
                                label + " -- declared NOT preserved on this renderer");
            }
        }
    }

    // -----------------------------------------------------------------------------------------
    // S -- stencil across a bind cycle, RenderTarget2D
    // -----------------------------------------------------------------------------------------

    void ExpectStencilCycleResult(const Bands& b, RenderTargetUsage usage, const std::string& label)
    {
        const bool discards = (usage == RenderTargetUsage::DiscardContents);
        if (discards)
            // Colour AND stencil are both reset on bind, so the gate passes nowhere and every band
            // stays at the discard colour. Pre-fix the shared layer cleared colour and depth but
            // NOT stencil, so bands 0/1 came back as the gate colour -- which is exactly what this
            // check turns red on.
            ExpectBands(b, DiscardColour(), DiscardColour(), DiscardColour(), label);
        else if (kContract.stencilPreserves)
            ExpectBands(b, GateColour(), GateColour(), BgColour(), label);
        else
            // Declared defect: stencil came back zero, so the gate passed nowhere and the stamp
            // colour is still what bands 0/1 carry.
            ExpectBands(b, StampColour(), StampColour(), BgColour(), label);
    }

    void RunStencilCase(GraphicsDevice& dev, RenderTargetUsage usage, const std::string& id,
                        const std::string& what)
    {
        auto rt = MakeRT(dev, usage, DepthFormat::Depth24Stencil8);
        dev.SetRenderTarget(rt.get());
        StencilCycle1(dev);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        dev.SetRenderTarget(rt.get());      // rebind, no clear
        StencilCycle2(dev);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        const Bands b = ReadTarget(*rt);
        if (kContract.readback != Support::Exact || !kContract.stencilInRT)
        {
            check(!b.threwSomethingElse, id + " " + what + " -- no functional render-target "
                  "stencil on this renderer; the sequence must still be legal" + b.detail);
            return;
        }
        ExpectStencilCycleResult(b, usage, id + " " + what + " (" + UsageName(usage) + ")");
    }

    void RunStencil(GraphicsDevice& dev)
    {
        RunStencilCase(dev, RenderTargetUsage::PreserveContents,
                       "S1", "stencil survives an unbind/rebind cycle");
        RunStencilCase(dev, RenderTargetUsage::PlatformContents,
                       "S2", "PlatformContents maps to preservation for stencil too");
        RunStencilCase(dev, RenderTargetUsage::DiscardContents,
                       "S3", "DiscardContents resets stencil to 0 deterministically");

        // S4: each target keeps its own stencil across a trip through another target.
        {
            auto a = MakeRT(dev, RenderTargetUsage::PreserveContents, DepthFormat::Depth24Stencil8);
            auto bt = MakeRT(dev, RenderTargetUsage::PreserveContents, DepthFormat::Depth24Stencil8);
            dev.SetRenderTarget(a.get());   StencilCycle1(dev);
            dev.SetRenderTarget(bt.get());  StencilCycle1(dev);
            dev.SetRenderTarget(a.get());   StencilCycle2(dev);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            if (kContract.readback != Support::Exact || !kContract.stencilInRT)
                skip("S4 two targets interleaved A->B->A (stencil)");
            else
                ExpectStencilCycleResult(ReadTarget(*a), RenderTargetUsage::PreserveContents,
                                         "S4 A->B->A: target A's own stencil survives a trip "
                                         "through an unrelated render target");
        }
    }

    // -----------------------------------------------------------------------------------------
    // K -- interaction with REMED-GFX-129's ordered explicit Clear
    // -----------------------------------------------------------------------------------------

    void RunClearInteraction(GraphicsDevice& dev)
    {
        const bool canDepth = (kContract.readback == Support::Exact) && kContract.depthInRT;
        const bool canStencil = (kContract.readback == Support::Exact) && kContract.stencilInRT;

        // K1: an explicit ClearDepth on the SECOND bind must beat the load action on a preserving
        // target -- depth goes back to 1.0, so the far draw is accepted. Band 1 simultaneously
        // proves the colour was NOT touched: depth cleared, colour preserved.
        if (!canDepth) skip("K1 Preserve bind + explicit depth Clear");
        else
        {
            auto rt = MakeRT(dev, RenderTargetUsage::PreserveContents, DepthFormat::Depth24Stencil8);
            dev.SetRenderTarget(rt.get());  DepthCycle1(dev);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            dev.SetRenderTarget(rt.get());
            dev.Clear(ClearOptions::DepthBuffer, BgColour(), 1.0f, 0);
            DepthCycle2(dev);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            ExpectBands(ReadTarget(*rt), BColour(), AColour(), BColour(),
                        "K1 Preserve + ClearDepth: the explicit clear overrides the preserving "
                        "load action for DEPTH and leaves COLOUR preserved");
        }

        // K2: the same for stencil alone -- gate passes nowhere, colour untouched.
        if (!canStencil) skip("K2 Preserve bind + explicit stencil Clear");
        else
        {
            auto rt = MakeRT(dev, RenderTargetUsage::PreserveContents, DepthFormat::Depth24Stencil8);
            dev.SetRenderTarget(rt.get());  StencilCycle1(dev);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            dev.SetRenderTarget(rt.get());
            dev.Clear(ClearOptions::Stencil, BgColour(), 1.0f, 0);
            StencilCycle2(dev);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            ExpectBands(ReadTarget(*rt), StampColour(), StampColour(), BgColour(),
                        "K2 Preserve + ClearStencil: the explicit clear overrides the preserving "
                        "load action for STENCIL and leaves COLOUR preserved");
        }

        // K3: clearing DEPTH must not touch STENCIL. Stamp, unbind, rebind, clear depth only,
        // then gate -- the gate must still pass where the stamp was.
        if (!canStencil) skip("K3 attachment-specific clear flags do not cross aspects");
        else
        {
            auto rt = MakeRT(dev, RenderTargetUsage::PreserveContents, DepthFormat::Depth24Stencil8);
            dev.SetRenderTarget(rt.get());  StencilCycle1(dev);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            dev.SetRenderTarget(rt.get());
            dev.Clear(ClearOptions::DepthBuffer, BgColour(), 1.0f, 0);
            StencilCycle2(dev);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            ExpectStencilCycleResult(ReadTarget(*rt), RenderTargetUsage::PreserveContents,
                                     "K3 ClearDepth does not clear STENCIL: the gate still passes "
                                     "exactly where the stamp was");
        }

        // K4: and the converse -- clearing STENCIL must not touch DEPTH.
        if (!canDepth || !canStencil) skip("K4 ClearStencil does not clear DEPTH");
        else
        {
            auto rt = MakeRT(dev, RenderTargetUsage::PreserveContents, DepthFormat::Depth24Stencil8);
            dev.SetRenderTarget(rt.get());  DepthCycle1(dev);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            dev.SetRenderTarget(rt.get());
            dev.Clear(ClearOptions::Stencil, BgColour(), 1.0f, 0);
            DepthCycle2(dev);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            ExpectDepthCycleResult(ReadTarget(*rt), RenderTargetUsage::PreserveContents,
                                   "K4 ClearStencil does not clear DEPTH: the retained depth "
                                   "still rejects the farther draw");
        }

        // K5: clearing COLOUR alone must not touch DEPTH -- depth preserved while colour is
        // explicitly cleared, the exact converse of K1's pairing.
        if (!canDepth) skip("K5 Clear(Target) alone does not clear DEPTH");
        else
        {
            auto rt = MakeRT(dev, RenderTargetUsage::PreserveContents, DepthFormat::Depth24Stencil8);
            dev.SetRenderTarget(rt.get());  DepthCycle1(dev);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            dev.SetRenderTarget(rt.get());
            dev.Clear(ClearOptions::Target, BgColour(), 1.0f, 0);
            DepthCycle2(dev);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            // Band 0's far draw is still rejected (depth kept) so it shows the fresh clear colour;
            // band 1 shows it too, because the colour clear wiped A; band 2's nearer draw wins.
            const Color b0 = kContract.depthPreserves ? BgColour() : BColour();
            ExpectBands(ReadTarget(*rt), b0, BgColour(), BColour(),
                        "K5 Clear(Target) alone: COLOUR is replaced and DEPTH is not");
        }

        // K6: a DiscardContents bind followed by an explicit Clear must still be exact -- the
        // discard clear and the explicit clear are two ordered commands, not one folded action.
        if (!canDepth) skip("K6 Discard bind + explicit Clear");
        else
        {
            auto rt = MakeRT(dev, RenderTargetUsage::DiscardContents, DepthFormat::Depth24Stencil8);
            dev.SetRenderTarget(rt.get());  DepthCycle1(dev);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            dev.SetRenderTarget(rt.get());
            dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
                      BgColour(), 1.0f, 0);
            DepthCycle2(dev);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            ExpectBands(ReadTarget(*rt), BColour(), BgColour(), BColour(),
                        "K6 Discard bind + explicit Clear: the explicit clear colour wins over "
                        "the bind's own discard clear");
        }

        // K7: a Clear issued AFTER a draw in the same cycle wipes it, and depth with it, so the
        // following far draw is accepted. Proves the load action never reorders an ordered clear.
        if (!canDepth) skip("K7 draw -> Clear -> draw ordering inside one cycle");
        else
        {
            auto rt = MakeRT(dev, RenderTargetUsage::PreserveContents, DepthFormat::Depth24Stencil8);
            dev.SetRenderTarget(rt.get());  DepthCycle1(dev);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            dev.SetRenderTarget(rt.get());
            PrepareDraw(dev, DepthTesting());
            DrawBand(dev, 1, kNear, StampColour());   // drawn, then wiped by the clear below
            dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
                      BgColour(), 1.0f, 0);
            DepthCycle2(dev);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            if (kContract.orderedClearInCycle)
                ExpectBands(ReadTarget(*rt), BColour(), BgColour(), BColour(),
                            "K7 draw -> Clear -> draw: the mid-cycle clear wipes both the earlier "
                            "draw and the depth it wrote");
            else
                // Declared defect (see orderedClearInCycle): the clear is hoisted to the front of
                // the cycle, so the draw recorded BEFORE it survives in band 1. Depth still ends
                // up cleared, so band 0 is unchanged -- which is why only band 1 tells them apart.
                ExpectBands(ReadTarget(*rt), BColour(), StampColour(), BColour(),
                            "K7 draw -> Clear -> draw: this renderer delivers a mid-cycle Clear "
                            "through the pass load action, so the earlier draw is NOT wiped "
                            "-- declared, recorded separately");
        }
    }

    // -----------------------------------------------------------------------------------------
    // U -- RenderTargetCube
    // -----------------------------------------------------------------------------------------

    void RunCube(GraphicsDevice& dev)
    {
        if (!kContract.cubeTargets)
        {
            skip("U1 cube face depth across a bind cycle (no RenderTargetCube here)");
            skip("U2 cube depth/stencil is ONE per-target resource shared by all six faces");
            skip("U3 cube face stencil across a bind cycle");
            skip("U4 multisampled cube depth across a bind cycle");
            return;
        }
        const bool canRead = (kContract.cubeReadback == Support::Exact);

        // U1: exactly the RenderTarget2D depth sequence, on one cube face.
        {
            auto cube = MakeCube(dev, RenderTargetUsage::PreserveContents,
                                 DepthFormat::Depth24Stencil8);
            dev.SetRenderTarget(cube.get(), CubeMapFace::PositiveX);  DepthCycle1(dev);
            dev.SetRenderTargets({});
            dev.SetRenderTarget(cube.get(), CubeMapFace::PositiveX);  DepthCycle2(dev);
            dev.SetRenderTargets({});
            if (!canRead || !kContract.depthInRT)
                skip("U1 cube face depth across a bind cycle");
            else
                ExpectDepthCycleResult(ReadFace(*cube, 0), RenderTargetUsage::PreserveContents,
                                       "U1 cube face depth survives an unbind/rebind cycle");
        }

        // U2: the per-target-versus-per-face discriminator, and the reason this file does NOT grow
        // six depth surfaces per cube. FNA allocates ONE glDepthStencilBuffer per RenderTargetCube,
        // so face B inherits whatever depth face A last wrote.
        //
        //   1. paint face A entirely, depth-disabled, and clear depth to 1.0
        //   2. paint face B entirely, depth-disabled, and clear depth to 1.0
        //   3. rebind face A, depth-testing, draw band 0 at kNear -> depth in band 0 becomes 0.25
        //   4. rebind face B, depth-testing, draw band 0 at 0.50
        //        shared depth  -> 0.50 > 0.25, REJECTED, face B band 0 keeps its paint colour
        //        per-face depth -> 0.50 < 1.0, accepted, face B band 0 becomes the probe colour
        if (!canRead || !kContract.depthInRT)
            skip("U2 cube depth/stencil is ONE per-target resource shared by all six faces");
        else
        {
            auto cube = MakeCube(dev, RenderTargetUsage::PreserveContents,
                                 DepthFormat::Depth24Stencil8);
            for (int face = 0; face < 2; ++face)
            {
                dev.SetRenderTarget(cube.get(), static_cast<CubeMapFace>(face));
                dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
                          face == 0 ? AColour() : BgColour(), 1.0f, 0);
                dev.SetRenderTargets({});
            }
            dev.SetRenderTarget(cube.get(), CubeMapFace::PositiveX);
            PrepareDraw(dev, DepthTesting());
            DrawBand(dev, 0, kNear, StampColour());
            dev.SetRenderTargets({});
            dev.SetRenderTarget(cube.get(), CubeMapFace::NegativeX);
            PrepareDraw(dev, DepthTesting());
            DrawBand(dev, 0, 0.50f, GateColour());
            dev.SetRenderTargets({});
            const Bands b = ReadFace(*cube, 1);
            // Where depth is not preserved at all, face B's bind resets it and the probe lands --
            // the same observable as a per-face buffer, so this check can only speak on a renderer
            // that preserves.
            const Color expect = kContract.depthPreserves ? BgColour() : GateColour();
            ExpectBands(b, expect, BgColour(), BgColour(),
                        "U2 cube depth is ONE per-target resource shared by all six faces "
                        "(FNA allocates one glDepthStencilBuffer per RenderTargetCube), so face B "
                        "sees the depth face A wrote");
        }

        // U3: stencil on a cube face across a bind cycle.
        {
            auto cube = MakeCube(dev, RenderTargetUsage::PreserveContents,
                                 DepthFormat::Depth24Stencil8);
            dev.SetRenderTarget(cube.get(), CubeMapFace::PositiveY);  StencilCycle1(dev);
            dev.SetRenderTargets({});
            dev.SetRenderTarget(cube.get(), CubeMapFace::PositiveY);  StencilCycle2(dev);
            dev.SetRenderTargets({});
            if (!canRead || !kContract.stencilInRT)
                skip("U3 cube face stencil across a bind cycle");
            else
                ExpectStencilCycleResult(ReadFace(*cube, 2), RenderTargetUsage::PreserveContents,
                                         "U3 cube face stencil survives an unbind/rebind cycle");
        }

        // U4: multisampled cube depth. REMED-GFX-141 gave every face its own multisample COLOUR
        // storage; depth stays one per-target attachment, and this proves it still survives.
        if (!kContract.msaaDepthCube)
            skip("U4 multisampled cube depth (a multisampled depth-backed RenderTargetCube cannot "
                 "be built here)");
        else if (!canRead || !kContract.msaaCubeReadback || !kContract.depthInRT)
            skip("U4 multisampled cube depth (no resolved readback of a multisampled cube FACE on "
                 "this renderer -- a different route from the RenderTarget2D one REMED-GFX-154 "
                 "fixed, and REMED-GFX-134's separately recorded boundary)");
        else
        {
            auto cube = MakeCube(dev, RenderTargetUsage::PreserveContents,
                                 DepthFormat::Depth24Stencil8, kMsaaRequest);
            const int applied = cube->getMultiSampleCountProperty();
            if (applied <= 0)
                skip("U4 multisampled cube depth (this renderer applies no MSAA to a cube target, "
                     "applied " + std::to_string(applied) + ")");
            else if (!canRead || !kContract.msaaCubeReadback || !kContract.depthInRT)
                skip("U4 multisampled cube depth (no resolved readback of a multisampled cube "
                     "FACE on this renderer -- REMED-GFX-134's separately recorded boundary)");
            else
            {
                dev.SetRenderTarget(cube.get(), CubeMapFace::PositiveZ);  DepthCycle1(dev);
                dev.SetRenderTargets({});
                dev.SetRenderTarget(cube.get(), CubeMapFace::PositiveZ);  DepthCycle2(dev);
                dev.SetRenderTargets({});
                ExpectDepthCycleResult(ReadFace(*cube, 4), RenderTargetUsage::PreserveContents,
                                       "U4 multisampled cube face depth at applied " +
                                       std::to_string(applied) + "x survives a bind cycle");
            }
        }
    }

    // -----------------------------------------------------------------------------------------
    // X -- capability and failure boundaries
    // -----------------------------------------------------------------------------------------

    void RunBoundaries(GraphicsDevice& dev)
    {
        // X1: a stencil clear on a target whose format carries no stencil is a legal no-op, not an
        // exception -- ClearOptions is a request, and GFX-018 already masks what is absent.
        {
            bool threw = false; std::string what;
            try
            {
                auto rt = MakeRT(dev, RenderTargetUsage::PreserveContents, DepthFormat::Depth24);
                dev.SetRenderTarget(rt.get());
                dev.Clear(ClearOptions::Stencil, BgColour(), 1.0f, 7);
                dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            }
            catch (const std::exception& e) { threw = true; what = e.what(); }
            catch (...) { threw = true; what = "non-std exception"; }
            check(!threw, "X1 ClearOptions::Stencil on a Depth24 (no-stencil) target is a legal "
                          "no-op" + (threw ? (" [threw: " + what + "]") : std::string()));
        }

        // X2: and a depth clear on a DepthFormat::None target likewise.
        {
            bool threw = false; std::string what;
            try
            {
                auto rt = MakeRT(dev, RenderTargetUsage::PreserveContents, DepthFormat::None);
                dev.SetRenderTarget(rt.get());
                dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
                          BgColour(), 1.0f, 0);
                dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            }
            catch (const std::exception& e) { threw = true; what = e.what(); }
            catch (...) { threw = true; what = "non-std exception"; }
            check(!threw, "X2 a depth/stencil Clear on a DepthFormat::None target is a legal "
                          "no-op" + (threw ? (" [threw: " + what + "]") : std::string()));
        }

        // X3: a stencil-tested draw into a depth-only target must not raise either. Whether the
        // gate passes is a capability question; that it does not throw is a contract question.
        {
            bool threw = false; std::string what;
            try
            {
                auto rt = MakeRT(dev, RenderTargetUsage::PreserveContents, DepthFormat::Depth24);
                dev.SetRenderTarget(rt.get());
                StencilCycle1(dev);
                StencilCycle2(dev);
                dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            }
            catch (const std::exception& e) { threw = true; what = e.what(); }
            catch (...) { threw = true; what = "non-std exception"; }
            check(!threw, "X3 stencil operations against a depth-only (Depth24) render target are "
                          "legal" + (threw ? (" [threw: " + what + "]") : std::string()));
        }

        // X4: a disposed target must not be usable, and must not take the device with it. The
        // whole depth sequence runs again afterwards on a fresh target to prove the device is
        // still healthy -- a renderer that left a dangling depth attachment fails there.
        {
            bool threw = false;
            {
                auto rt = MakeRT(dev, RenderTargetUsage::PreserveContents,
                                 DepthFormat::Depth24Stencil8);
                dev.SetRenderTarget(rt.get());
                DepthCycle1(dev);
                dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
                rt->Dispose();
            }
            auto fresh = MakeRT(dev, RenderTargetUsage::PreserveContents,
                                DepthFormat::Depth24Stencil8);
            dev.SetRenderTarget(fresh.get());  DepthCycle1(dev);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            dev.SetRenderTarget(fresh.get());  DepthCycle2(dev);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            if (kContract.readback != Support::Exact || !kContract.depthInRT)
                check(!threw, "X4 a disposed depth-backed target leaves the device usable");
            else
                ExpectDepthCycleResult(ReadTarget(*fresh), RenderTargetUsage::PreserveContents,
                                       "X4 after a depth-backed target is disposed, a fresh one "
                                       "still preserves its own depth");
        }
    }

protected:
    void Initialize() override
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBBW);
        gdm_->setPreferredBackBufferHeightProperty(kBBH);
        if (kContract.preferMultiSampling) gdm_->setPreferMultiSamplingProperty(true);
        if (kContract.wantHiDefProfile)
            gdm_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        gdm_->ApplyChanges();
        Game::Initialize();
        if (kContract.drawsUserPrimitives)
            fx_ = std::make_unique<BasicEffect>(getGraphicsDeviceProperty());
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        auto& dev = getGraphicsDeviceProperty();
        // Frame 0 is a deliberate warm-up, the same one REMED-GFX-134/136/141 use: bgfx's very
        // first readback of a frame reports black regardless of what was rendered, so the first
        // check in the battery -- which is the capability control everything else is gated on --
        // would measure nothing on that renderer. One throwaway backbuffer frame first, then the
        // whole sequence runs in frame 1. Every other renderer is unaffected: nothing here reads
        // anything back, and frame 1 rebuilds every target it uses from scratch.
        if (!warmedUp_)
        {
            warmedUp_ = true;
            dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
                      BgColour(), 1.0f, 0);
            PrepareDraw(dev, DepthTesting());
            DrawBand(dev, -1, 0.5f, AColour());
            return;
        }
        done_ = true;

        std::printf("=== REMED-GFX-142 render-target depth/stencil lifetime -- %s ===\n",
                    kContract.name);

        try
        {
            RunCapability(dev);
            RunDepth(dev);
            RunStencil(dev);
            RunClearInteraction(dev);
            RunCube(dev);
            RunBoundaries(dev);
        }
        catch (const std::exception& e)
        {
            check(false, std::string("unexpected exception escaped a section: ") + e.what());
        }
        catch (...)
        {
            check(false, "unexpected non-std exception escaped a section");
        }

        std::printf("=== %d/%d checks passed on %s ===\n", passCount_, totalCount_,
                    kContract.name);
        result_ = (passCount_ == totalCount_) ? 0 : 1;
        Exit();
    }

public:
    int getResult() const { return result_; }
};

int main()
{
    RenderTargetDepthStencilUsageTest game;
    return game.Run(), game.getResult();
}
