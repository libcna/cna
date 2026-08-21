# CNA Known Bugs


## FNA3D resource renderers that outlive their device free through a dangling FNA3D_Device

**Backend:** FNA3D (found on the SDL_GPU/Vulkan driver; the ownership bug is driver-independent).

**Status:** OPEN. Found 2026-08-14 by the plans/plan_fx.md FX-054 full-suite regression run; not caused
by the compiled-effect work, which never touches these types.

A `Fna3dRenderTargetCubeRenderer` (and, by the same pattern, the other `Fna3dResources.cpp`
renderers) keeps a raw `FNA3D_Device*` and guards its destructor only with `device_ == nullptr`.
When the resource outlives its `GraphicsDevice` -- which is exactly what
`MetalResourceHealth.RenderTargetCubeRendererEscapesThroughTextureCubeBaseMove` deliberately
arranges by moving a renderer out through the `TextureCube` base and holding the `shared_ptr` past
device destruction -- that pointer is dangling rather than null, so the destructor calls
`FNA3D_AddDisposeTexture` on freed memory.

Effect: running the whole `CnaTests` binary under the FNA3D renderer ends in a segmentation fault
after the last test, so the run produces no gtest summary even though every test passed. The suite
passes when run standalone, which is why it went unnoticed.

AddressSanitizer, from a full-suite run of `cmake-build-fna3d-asan`:

```
ERROR: AddressSanitizer: heap-use-after-free
    #0 FNA3D_AddDisposeTexture FNA3D.c:754
    #1 Fna3dRenderTargetCubeRenderer::~Fna3dRenderTargetCubeRenderer Fna3dResources.cpp:452
   ...
    #10 MetalResourceHealth_RenderTargetCubeRendererEscapesThroughTextureCubeBaseMove_Test::TestBody
        MetalResourceHealthTests.cpp:240
freed by:
    #1 SDLGPU_DestroyDevice FNA3D_Driver_SDL.c:4263
    #2 FNA3D_DestroyDevice FNA3D.c:247
    #3 Fna3dRenderer::~Fna3dRenderer Fna3dRenderer.cpp:428
```

**Fix direction:** give the FNA3D renderer a shared liveness token that its destructor
invalidates, and have every `Fna3dResources.cpp` renderer hold a weak reference to it and skip
native disposal once the device is gone -- the discipline several other renderers already apply,
and the discipline the neighbouring `MetalResourceHealth.*RejectAfterDeviceDeath` cases exist to
enforce.

## Multiple SpriteBatch Begin/End in one frame discards all but the last

**Backend:** Vulkan (confirmed), others unknown.

**Symptom:** If `SpriteBatch::Begin()` / `SpriteBatch::End()` is called more than once
within a single `Draw()` frame, only the draws from the **last** Begin/End pair are
visible. All earlier sprite draws are silently discarded.

**Example:**
```cpp
// Frame Draw():
spriteBatch->Begin();
spriteBatch->Draw(background, Vector2::Zero, Color(255,255,255,255));
spriteBatch->End();   // ← this batch is LOST

spriteBatch->Begin();
spriteBatch->Draw(tank, tankPos, Color(255,255,255,255));
spriteBatch->End();   // ← only this batch renders
```

**Workaround:** Merge all sprite draws into a single `Begin()` / `End()` per frame.
If mixing SpriteBatch with PrimitiveBatch (`DrawUserPrimitives`), call
`spriteBatch->End()` first, then draw primitives, then start a new SpriteBatch
only if strictly necessary — but prefer keeping everything in one batch.

**Discovered in:** cna-samples #021 PathDrawing port (2026-06-27).

---

## LLGL backend: 3D pipeline cache ignores `ColorWriteChannels`/blend factors for `DrawPrimitives` — OPEN

**Status:** open, discovered during LLGL-33, not fixed. `BlendState.MultiSampleMask` itself
(the actual LLGL-33 feature) is unaffected and shipped normally — see `docs/llgl-backend.md`.

**Symptom:** `AcquirePrimitivePipeline` (the 3D/`DrawPrimitives` pipeline cache used by
`BasicEffect` and friends, as opposed to `AcquireSpritePipeline`/`LlglEffectBackend::AcquirePipeline`
used by `SpriteBatch`/custom effects) folds only the **low 16 bits** of
`MakeBlendPipelineKey()`'s ~48-bit result into its own cache key. That truncation silently discards
`colorSrcBlend_`/`colorDstBlend_`/`alphaSrcBlend_`/`alphaDstBlend_`/`colorBlendFunc_`/
`alphaBlendFunc_`/`colorWriteChannels_[0]` entirely (they occupy the sub-key's higher-order bits).
Two 3D draws differing ONLY in one of those fields can be assigned the SAME cached
`LLGL::PipelineState*`, so the second draw silently keeps the FIRST draw's blend/write-mask state
baked in instead of its own.

**Found by:** registering the shared, cross-backend `examples/gfx077_colorwritechannels_3d_test.cpp`
(already used by `SdlGpu`/`WebGpu`) against this backend — its very first check (differential
`ColorWriteChannels::None` vs `::All` baselines through `BasicEffect`+`DrawPrimitives`) failed, both
baselines reading back identically, proving `ColorWriteChannels` had no effect on that path. This
test is **not** currently registered as an LLGL CTest (see `cmake/Tests/LlglTests.cmake`) precisely
because it fails against this open bug; do not register it again until the underlying issue is
fixed.

**Attempted fixes, both abandoned:** widening the outer truncation to include the missing fields —
first via an XOR + FNV-prime multiply (matching `MakeVertexLayoutKey`'s own `mix` style), then via a
plain positional multiply-add fold (matching this function's own established style, avoiding the XOR
approach entirely) — fixed the `ColorWriteChannels_3D` test in isolation but broke an unrelated,
previously-passing test: `Llgl_BasicEffect`'s alpha-blend check
(`"Alpha blends the white texel halfway to the background"`) started rendering
`(64,64,64,191)` instead of the correct `(128,128,128,128)`, with BOTH fold strategies, even though
instrumented tracing showed the pipeline built for that draw is provably correctly configured
(`src=SourceAlpha, dst=InverseSourceAlpha`, its own uniquely-computed key, its own freshly-built
`LLGL::PipelineState*`, no cache collision with any other draw).

**The genuinely confusing part:** reverting to the OLD, truncated key — the one that provably causes
the alpha-blend draw to be assigned the SAME cached `PipelineState*` as an EARLIER, unrelated Opaque
draw (identical pointer, identical key, confirmed via instrumented tracing) — renders the alpha-blend
draw CORRECTLY. Reusing a pipeline object whose baked-in blend state is `Opaque`
(`blendEnabled=false, srcColor=One, dstColor=Zero`) for a draw that needs real
`SourceAlpha`/`InverseSourceAlpha` blending should, under ordinary GPU fixed-function blend
semantics, produce an unblended result — it does not. No explanation was found before this
investigation was shelved in favour of shipping the safe, unaffected parts of LLGL-33. Hypotheses
considered but not confirmed or ruled out: (a) LLGL/lavapipe pipeline object interning/de-duplication
keyed on something coarser than full descriptor content (e.g. shader/layout identity or
`debugName`), causing distinct `LLGL::PipelineState*` C++ wrapper objects to alias the same
underlying GPU pipeline; (b) some sensitivity to the total number of distinct
`GraphicsPipelineDescriptor`s created across the process's lifetime; (c) an incorrect assumption
elsewhere in this reasoning about how this backend actually applies blend state, not yet identified.

**Current state:** `AcquirePrimitivePipeline` keeps the original, known-buggy `& 0xFFFFu`
truncation (see its own doc comment in `LlglGraphicsBackend.cpp`) rather than shipping either
attempted fix. `BlendState.MultiSampleMask` is NOT affected by this limitation:
`MakeBlendPipelineKey` folds `multiSampleMask_` in LAST, so it occupies the sub-key's lowest 4 bits
and survives the `& 0xFFFFu` truncation intact — confirmed by `Llgl_MultiSampleMask`/`_OpenGL`
passing (4/4 PASS on the Vulkan module, 3 PASS + a correctly-detected `[SKIP]` on the OpenGL
module, which never applies a sample mask at all — see `LlglGraphicsBackend.cpp`'s own
`FillCurrentBlendAndRasterStateEXT` doc comment).

**Next step for whoever picks this up:** do not retry the same two fold strategies without new
information. Consider instrumenting at the LLGL/Vulkan API call level (not just this backend's own
descriptor construction) to see whether two DIFFERENT `LLGL::PipelineState*` C++ objects might be
returned pointing at the same underlying `VkPipeline` handle, or whether `vkCmdBindPipeline` is
actually being issued with the value this backend thinks it queued.

**Tracked as:** `plans/plan_llgl.md` task `LLGL-21`.

---

## LLGL backend: `backbuffer_pass_order_test.cpp`'s own Contract was stale after LLGL-45; correcting it exposes a new, real, narrower gap (V1/V2) — OPEN

**Status:** discovered 2026-08-03 while running a broad regression sweep after `LLGL-45`/`LLGL-46`/
`LLGL-49`/`LLGL-50`. `backbuffer_pass_order_test.cpp`'s own `CNA_BACKEND_LLGL` `Contract` branch
still declared `orderedBackbufferSegments = false` (the pre-`LLGL-45` bucket-by-identity behavior)
after `LLGL-45` had already fixed the underlying replay engine -- a stale test assumption, not a code
regression. Corrected the declaration to `true` and re-ran: **dozens of checks that were previously
silently `skip()`ped** ("this backend replays all backbuffer work in one trailing pass") **now
genuinely evaluate and PASS** (A3-A6, C1, C3, C5, C6, O1-O4, U1, U2, M1, and A1 itself, which had
started FAILING once the stale `false` value made its own COLLAPSED-result assertion wrong) --
strong, broad, independent confirmation that `LLGL-45`'s own fix is correct. This is registered
here as its own entry (not folded into the `LLGL-45` entry above) because it also surfaces something
`LLGL-45` did NOT fix.

**The new, real, open finding:** V1 ("viewport per cycle") and V2 ("scissor per cycle") now FAIL for
real, for the first time ever measured -- previously masked by the stale `false` contract, which
made these checks silently skip too. Both drive the SAME shape: three backbuffer cycles
(A: sub-Viewport/scissor X, draw; B: a DIFFERENT target bound in between; A again: a DIFFERENT
sub-Viewport/scissor, a SECOND draw only covering part of the first cycle's own area) and expect the
backbuffer to show BOTH the first cycle's surviving (non-overdrawn) content AND the third cycle's
new content, each still clipped to its OWN viewport/scissor rectangle from when it was actually
queued. V1 fails its combined position assertion outright (`ok == false`, no per-pixel detail
captured); V2 fails with 18/? probes wrong, first probe reading the CLEAR colour instead of the
expected red -- i.e. the FIRST cycle's own draw appears to be MISSING from the final image, not
merely at the wrong scissor position. Not yet root-caused: this is a narrower, more specific defect
than the general ordering `LLGL-45` fixed (segment placement itself is now empirically correct, per
every OTHER check in this same file passing), so the cause is more likely in how per-command
viewport/scissor state interacts with a target being revisited (e.g. something about how the
SWAP CHAIN specifically -- as opposed to an off-screen `RenderTarget2D`, which is what
`deferred_viewport_capture_test.cpp`'s own already-passing K2 leg exercises for a similar
same-target-two-cycles shape -- carries its own per-segment viewport captures across a Load-reload).

**Not yet verified whether this also affects the Vulkan module**, the actual default target this
file's own registered `Llgl_BackBuffer_PassOrder` CTest runs against -- this sandbox's Vulkan module
cannot present (no DRI3), the same infrastructure gap affecting several other findings this session.
The file also independently crashes (`ValidateGLTextureType: hasCubeTextures not supported`) on a
LATER, cube-texture-dependent leg under the OpenGL module specifically -- confirmed PRE-EXISTING
(reproduces identically against the pre-`LLGL-45` binary too), the same OpenGL-module capability gap
already documented elsewhere in this file, unrelated to this entry.

**Tracked as:** `plans/plan_llgl.md` Phase LLGL-8 (a follow-up correction to `LLGL-45`, not its own
numbered ticket). `Llgl_BackBuffer_PassOrder` remains registered (unchanged from before this
session) since this sandbox cannot re-verify it against Vulkan either way; whoever next has real
Vulkan or a DRI3-capable Xvfb should re-run it and, if V1/V2 fail there too, open a dedicated
follow-up ticket for the per-cycle viewport/scissor-on-backbuffer gap specifically.

---

## LLGL backend: `Orthographic` + `CreateLookAt` scenario reports geometry off-screen — OPEN

**Status:** open, discovered while wiring `plans/plan_llgl.md`'s Phase LLGL-7 (LLGL-39); narrowed
considerably during `LLGL-52` (2026-08-03) by following this entry's own previously-suggested next
step, and narrowed FURTHER on 2026-08-04 on real hardware (this machine's own physical desktop,
`DISPLAY=:0`, has a real AMD Radeon 780M with a working RADV Vulkan driver -- confirmed via
`vulkaninfo`/`glxinfo`, not assumed). Root cause STILL not identified, but two decisive new facts
are now established (see the 2026-08-04 findings below) and one earlier claim in this entry (no
LLGL-specific depth-range remap exists) is corrected below. Not fixed here.

**2026-08-04 findings (real Vulkan + real hardware, `DISPLAY=:0 CNA_LLGL_RENDERER=vulkan`):**
- **The bug is ARCHITECTURAL, not OpenGL-module-specific.** `cna_test_llgl_rasterizerstate_
  cullmode_camera` run against the REAL Vulkan module (not Xvfb -- this sandbox's Xvfb `:99`/`:101`
  still lack DRI3, but the machine's own real desktop display does not) reproduces scenario (b)'s
  failure IDENTICALLY: `geometry off-screen (A found=1, B found=0)`, same as the OpenGL module.
  This rules out anything specific to either renderer module (`GLStateManager`, `glDepthRangef`,
  GL's own clip-space convention, `VkViewport` specifics) -- the defect must be in code SHARED
  between both modules (`QueuePrimitives`, `AcquirePrimitivePipeline`, the deferred FrameCommand
  capture/replay machinery, or CNA's own CPU-side matrix/vertex handling upstream of either
  backend). It also independently confirms the `QueuePrimitives` GL-only Z-remap (see the
  correction below) cannot be the cause: Vulkan's `clippingRange` is `ZeroToOne`, so that remap
  code never even RUNS on the Vulkan module, yet the failure is identical.
- **The failure tracks POSITION, not winding.** Swapping which triangle (`worldTriA`/`worldTriB`)
  is built at `centerA` (`target + camRight*-80`, world Z=+80) vs `centerB` (`target +
  camRight*+80`, world Z=-80) while keeping each variable's OWN winding function
  (`MakeCwBasis`/`MakeCcwBasis`) attached to its own NAME moved the failure WITH the position, not
  with the label or the winding: whichever triangle sits at `target + camRight*80` (world Z=-80,
  this camera's screen-right side) fails to render, regardless of whether it is called `triA` or
  `triB` and regardless of whether it is wound CW or CCW in local space. This rules out any
  winding-dependent explanation (a stray cull, a front/back stencil-slot-style swap like the one
  documented on the Vulkan backend for an unrelated stencil bug) and confirms this is purely about
  WHERE the geometry sits, specifically along the camera's own right/screen-horizontal axis, under
  Orthographic projection only.

**Symptom:** `examples/rasterizerstate_cullmode_camera_test.cpp` (no `CNA_BACKEND_` conditional
branches at all -- meant to be universal, backend-agnostic math, already registered and passing on
several other backends) fails its own internal `[FATAL]` scenario-setup check for exactly ONE of
its five scenarios: `(b) Orthographic + CreateLookAt`. The test's own probe (`FindPixel`, a
full-framebuffer scan for a matching colour) cannot find triangle B anywhere on screen at all
(`geometry off-screen (A found=1, B found=0)`), so that scenario is skipped rather than judged.
Scenarios (a) Identity, (c) Perspective+CreateLookAt (the IDENTICAL `eye`/`target`/`up` camera as
(b), just with a perspective instead of an orthographic projection), (d) and (e) (both also
Perspective+CreateLookAt, with a rotated/mirrored World matrix) all pass cleanly -- 24/24 individual
cull-mode checks PASS across those four scenarios.

**Correction to this entry's own earlier claim:** a depth-range remap DOES exist in
`LlglGraphicsBackend.cpp`, in `QueuePrimitives()` -- keyed on `renderer_->GetRenderingCaps().
clippingRange == LLGL::ClippingRange::MinusOneToOne`, it folds XNA's D3D-convention `[0,1]` depth
into GL's `[-1,1]` convention (`z' = 2*z - w`) directly into the combined `world*view*projection`
matrix, once per draw, before the textured/lit/dualTexture/envMap/skinned/pbr branch. The earlier
"no remap code found" claim searched for the wrong strings (`glDepthRange`/`DepthRange`/
`ClipControl`); grepping for `ClippingRange` specifically finds it. This is now ruled out as the
asymmetry's cause, though: this remap operates on the shared WVP matrix, identically for both
triangles in the same draw call (same `world`/`view`/`projection` arguments, only vertex
POSITIONS differ between the two `DrawUserPrimitives` calls) -- it cannot by itself explain why one
triangle renders and the other does not.

**What was ruled out this session, with live instrumentation (not guessed):**
- **Not a math/matrix-construction bug.** Printing `Vector4::Transform(vertex, wvp)` directly (CNA's
  own real pipeline, the identical call `NdcSignedArea()` already used) for every vertex of both
  triangles in scenario (b) shows: `W = 1.0` exactly for all six vertices (a true orthographic
  projection, no perspective skew introduced anywhere upstream); triangle A's NDC X/Y in
  `[-0.6,-0.2]`/`[-0.3,0.3]`, triangle B's in `[0.2,0.6]`/`[-0.3,0.3]` -- a clean mirror image, both
  comfortably inside `[-1,1]`; `Z = 0.1051` identical for BOTH triangles (they sit at the same
  camera-space depth, differing only along `camRight`), comfortably inside `[0,1]`. Nothing here is
  even close to a clip-volume boundary.
- **Not cull-mode-related.** `CullMode::None` is explicitly set for the probe draw and cannot cull
  by winding at all; scenario (c) draws the exact same CCW-in-local-basis triangle B (same
  `MakeCcwBasis` construction, same camera, just a different projection) and it DOES render under
  `CullMode::None` -- so `CullMode::None` is not silently falling back to a default cull state in
  this backend in general.
- **Not draw-order/leftover-state.** Swapping which triangle's `findOne()` call runs first (B then
  A, instead of A then B) reproduces identically: triangle B never appears, regardless of which
  Clear+Draw+readback cycle runs first or second.
- **Not a miscoloured or mislocated triangle.** A full 128x128 framebuffer scan for ANY non-black
  pixel (not just near triangle B's predicted screen location) after triangle B's own isolated
  Clear+Draw+GetBackBufferData cycle finds ZERO non-black pixels anywhere. This is a genuine
  non-render, not a wrong-colour or wrong-position one.
- **Not the per-draw-`Viewport` bug documented elsewhere in this file** -- scenario (b)'s own draw
  calls (`RunScenario`'s `findOne`/`renderBoth` lambdas) never change `GraphicsDevice.Viewport` at
  all.
- **Not fog/large-coordinate precision** -- scenarios (c)/(d)/(e) use the identical world-scale
  vertex positions (hundreds of units from the origin) and the identical `BasicEffect`/
  `VertexColorEnabled` setup, and both triangles render correctly there.
- **Not a `Matrix::CreateOrthographic` construction bug** -- read directly (`Matrix.cpp`): a
  textbook-symmetric orthographic matrix (`M11 = 2/width`, `M22 = 2/height`, both applied
  identically regardless of the sign of the transformed X/Y), no sign-asymmetric or
  absolute-value logic anywhere in it that could treat a negative-X and positive-X vertex
  differently.
- **Not OpenGL-module-specific** and **not winding-dependent** -- see the 2026-08-04 findings above.

**What remains unexplained:** the one variable that reproduces the failure is Orthographic
projection specifically, combined with a non-identity `CreateLookAt` view, drawing whichever
triangle sits on the POSITIVE side of the camera's own right-axis offset (world Z=-80 for this
specific camera; NDC X in `[0.2,0.6]`, the right half of the screen). Perspective with the exact
same view/camera/triangle construction does not reproduce it; Identity (no real Orthographic
matrix at all) does not reproduce it; the CPU-side clip-space values for the FAILING vertex are
just as unremarkable and correct as the succeeding one's (confirmed identical `W=1.0`, symmetric
NDC X, identical `Z=0.1051`). Since the defect (a) reproduces byte-for-byte identically on two
independently-implemented renderer modules that share almost no code below `QueuePrimitives`/
`AcquirePrimitivePipeline`, and (b) is provably NOT explained by anything in the CPU-side
matrix/vertex math these two modules are FED, the most likely remaining location is somewhere in
the SHARED deferred-command capture/replay path itself (`QueuePrimitives`'s own uniform-buffer
pooling, `FrameCommand` capture, or `AcquirePrimitivePipeline`'s pipeline-cache key/reuse) rather
than in either module's own native rendering calls.

**2026-08-04 follow-up: the previously-suggested next lead is now RULED OUT too.** Followed this
entry's own recommendation and instrumented `QueuePrimitives()` directly (temporary `fprintf`,
fully removed before committing -- confirmed via `git diff` showing no residual change) to compare
the actual captured `FrameCommand` for triA's (succeeding) and triB's (failing) draws in scenario
(b): vertex buffer pointer, cached pipeline pointer, WVP matrix values, `elementCount`/
`vertexStart`/`startIndex`/`baseVertex`. Every one of these is either byte-identical between the
two draws (matrix -- correctly so, since world/view/projection are the same camera for both
triangles by the test's own design; pipeline -- correctly so, since none of the state
`AcquirePrimitivePipeline` keys on differs between the two draws; `elementCount=3`/all offsets 0)
or a REUSED-BUT-EXPECTED pointer value (the vertex buffer address is identical for both draws, but
this is ordinary allocator address reuse across two temporally-separate, fully-flushed frames --
`DrawUserPrimitives`'s own per-call temp buffer is torn down via `ScheduleBufferReleaseEXT()`,
which was independently verified by reading its own source to correctly DEFER the actual release
until `frameCommands_` is next flushed, not release-then-dangle immediately -- so this is not the
same-shape bug as the pipeline-cache-key-overflow finding this entry originally suspected: there is
no aliasing here, both draws' commands are captured, transported, and would replay with fully
correct, distinct data). **This rules out the entire capture/replay-machinery layer as the cause**:
whatever is happening must be at or below the actual GPU rasterization of triB's own vertex data,
which the CPU-side `Vector4::Transform` check already showed is unremarkable (same as triA's own
passing check, just mirrored in X). Not yet checked: the ACTUAL BYTES uploaded into the vertex
buffer via `DrawUserPrimitives`'s `Pack()`/`PositionColorStream` conversion (confirmed only that
the SOURCE `Tri` vertices are correct pre-pack, not that the packed GPU-visible bytes match).

**Next step for whoever picks this up:** read back the vertex buffer's own GPU-side content right
after `SetData()` (e.g. via `LLGL::RenderSystem::ReadTexture`'s buffer-readback analogue, or by
mapping the buffer if the module supports it) for triB's own draw specifically, to rule out (or
confirm) a `Pack()`/stride/format bug that only manifests for this specific vertex data -- this is
the one remaining untested layer between "CPU math is correct" (confirmed) and "GPU draws nothing"
(confirmed). If that also comes back clean, the defect is genuinely inside actual GPU rasterization
of this specific geometry under Orthographic + this specific view matrix, which would need either
a native GPU debugger (RenderDoc, apitrace) or a from-scratch minimal repro outside this whole
engine to isolate further.

**Tracked as:** `plans/plan_llgl.md` Phase LLGL-7 / `LLGL-52` (blocks `LLGL-39`'s
`Llgl_RasterizerState_CullMode_Camera` registration).

---

## LLGL backend: `RasterizerState.SlopeScaleDepthBias`, custom `Viewport.MinDepth`/`MaxDepth`, and `RenderTarget2D` depth testing each have a genuine, unexplained defect — OPEN (LLGL-53)

**Status:** open, discovered while wiring `plans/plan_llgl.md`'s `LLGL-53`. Root cause NOT identified for
any of the three; not fixed here. Distinct from the pipeline-cache-key entry above -- confirmed via
isolation (disabling the depth-bias/stencil descriptor writes entirely, with the cache key already
fixed, does NOT make any of these three pass), so none of these are a side effect of this ticket's
own new descriptor/key code; they are real, separately-rooted defects that this ticket's own new
test coverage (`rasterizerstate_depthbias_test.cpp`, reused from Software) happened to be the first
thing to exercise them on this backend.

**D1 -- `SlopeScaleDepthBias` alone has no effect.** `rasterizerstate_depthbias_test.cpp`'s D0
(SlopeScale=0, shallow-sloped quad correctly wins over a flat one by ordinary depth) passes; D1
(the SAME geometry, SlopeScale=150 expected to push the sloped quad's far edge behind the flat
one) still reports the shallow quad winning, as if the slope factor were never applied. `D2`
(a FLAT quad under the same high SlopeScale, expected to get ZERO slope offset since a flat
polygon's screen-space depth slope is 0) correctly passes, which is at least consistent with "no
slope offset is being applied to sloped geometry either" rather than some fixed non-zero offset
firing regardless of slope. `pipelineDesc.rasterizer.depthBias.slopeFactor = slopeScaleDepthBias_;`
is wired identically to `constantFactor` (LLGL's own `GLRasterizerState.cpp` reads both the same
way, see `IsPolygonOffsetEnabled`/`Bind()`), and the CONSTANT factor demonstrably works (every
`A1`/`B1`/`C1`/`E1`/`G0` check in the same file passes) -- so this is specifically about the SLOPE
term, not depth bias wiring in general.

**H0/H1 -- ANY draw under a non-default `Viewport.MinDepth`/`MaxDepth` renders nothing at all.**
`rasterizerstate_depthbias_test.cpp`'s H0 (custom depth range `[0.2, 0.8]`, DepthBias=0 -- a pure
baseline, no bias involved at all) expects the later of two coplanar quads to win by ordinary
depth-test semantics, same as every other baseline check in the file. Instead, a full-framebuffer
readback shows the centre pixel stays pure background black -- NEITHER quad drew. Confirmed via a
`git stash`-based comparison against the exact pre-`LLGL-53` binary: this same scenario correctly
rendered green (H0 passed) BEFORE any of `LLGL-53`'s changes, so it is a genuine new regression
somewhere in this ticket's own work, most likely `SetViewport()`'s newly-wired
`minDepth`/`maxDepth` plumbing (`CaptureFrameCommandViewportEXT()`'s own `viewportMinDepth`/
`viewportMaxDepth` fields, and the `LLGL::Viewport`'s 6-argument constructor now used at replay).
Debug instrumentation confirmed the CAPTURED values are correct at replay time
(`viewport=(0,0,640,480) depth=(0.200,0.800)`, exactly matching what the test requested) -- so the
defect is not in this backend's own capture/plumbing, but somewhere between that correct
`LLGL::Viewport` call and what actually reaches the screen (LLGL's own OpenGL module's
`GLImmediateCommandBuffer::SetViewport()`/`GLStateManager::SetDepthRange()` were read and look
correct on their own too, `GLProfile::DepthRange(minDepth, maxDepth)`; not yet traced further).

**I0/I1 -- a bound `RenderTarget2D` with depth testing renders nothing, even with DepthBias=0 and
the DEFAULT `[0,1]` depth range.** Same "baseline renders nothing" symptom as H0, but reached via a
completely different path (`SetVp(dev, 0, 0, rtW, rtH)` on a bound `RenderTarget2D`, no custom
depth range at all). Also confirmed via `git stash` comparison to be a genuine NEW regression from
this ticket's own work (I0 rendered green correctly before `LLGL-53`). By `Viewport(x,y,w,h)`'s own
constructor defaulting `MinDepth`/`MaxDepth` to `[0,1]`, and H1's own block explicitly resetting the
viewport (`SetVp`) before I's block runs, I0/I1 do NOT inherit H0/H1's non-default depth range --
so despite the superficially similar symptom, **H0/H1's own root cause below (confirmed 2026-08-04)
does not apply to I0/I1**, which remain a genuinely separate, still fully unexplained defect.

**2026-08-04: H0/H1's root cause CONFIRMED, narrowed to one specific interaction -- not fixed
(the correct behavior IS what triggers it).** Live-instrumented the vendored LLGL OpenGL module
(temporary, fully reverted, `~/deps/LLGL` pristine again) end to end: `SetViewport()`'s captured
`viewport`/`depthRange` reach `glViewport`/`glDepthRangef` correctly (`GL_VIEWPORT=(0,0,640,480)`,
`GL_DEPTH_RANGE=(0.200,0.800)`, `depthTest=1`, `depthFunc=GL_LEQUAL`, `writeMask=1` -- all queried
directly via `glGetIntegerv`/`glIsEnabled`, all exactly as requested) for BOTH the red and green
draws in H0. The scissor rect computed by `ComputeEffectiveScissor()` is IDENTICAL between H0
(fails) and G0 (passes, same file, immediately before H0) -- `(0,0,640,480)` in both, since
`viewportRect_`'s own width (96) already differs from the logical width (120) for EVERY check in
this file, independent of depth range -- ruling out `viewportSet_`'s LLGL-53-widened condition
(the very first hypothesis this investigation reached for) as the cause; it was already true before
LLGL-53 touched anything.

**The actual mechanism:** `GraphicsDevice::Clear(Color)` uses `Viewport.MaxDepth` -- not a
hardcoded `1.0` -- as the depth-buffer clear value, matching FNA's own exact behavior (Task 928,
predates this ticket entirely). For H0, that is `0.8` (`Viewport.MaxDepth` from the just-applied
custom range). Confirmed via direct `glGetFloatv(GL_DEPTH_CLEAR_VALUE, ...)`: the backend correctly
requests and the GL module correctly applies a raw clear value of `0.8000`. **This combination --
clearing the depth buffer to a non-`1.0` raw value, then drawing under a narrowed
`glDepthRangef(0.2, 0.8)` -- causes the LEQUAL depth test to reject every subsequent fragment**,
even though the fragment's own computed window-depth (`~0.5`, per the test's own worked example)
is arithmetically `<= 0.8` and should pass. **Proven, not inferred:** temporarily forcing the GL
clear-depth call to always use `1.0` (ignoring the correctly-computed `0.8`, a deliberately
XNA-INCORRECT experiment, reverted immediately after) makes **H0 pass** -- conclusively isolating
the mechanism to this one interaction. H1 (which also clears under the same non-default range)
still fails even with that same experimental override, meaning it depends on something further
(most likely `DepthBias`'s OWN interaction with a non-default range, not yet isolated).

**Why this is not being "fixed" here:** `Clear()`'s use of `Viewport.MaxDepth` is CORRECT,
intentional, XNA/FNA-faithful behavior (see Task 928's own comment) -- reverting it to a hardcoded
`1.0` would violate XNA fidelity for any other legitimate use of a custom `MaxDepth`, not just
paper over this specific case. The remaining question -- WHY a raw depth-buffer clear value
combined with a narrowed `glDepthRangef` causes total fragment rejection, when the values involved
are all within their documented valid ranges -- was not resolved: candidates not yet distinguished
are a genuine `llvmpipe`/Mesa software-rasterizer quirk in how `glClear(GL_DEPTH_BUFFER_BIT)`
interprets its clear value relative to the CURRENT depth range (non-standard per the GL spec, which
says `glClear` writes the raw depth-buffer value unaffected by `glDepthRangef` -- but this is a
SOFTWARE rasterizer, not a spec-perfect reference implementation), versus something LLGL's own GL
module does differently for a narrowed range that a real GPU driver would not. Needs either a
native GPU debugger (RenderDoc/apitrace, unavailable in this sandbox) or a real, non-software GL
driver to distinguish a genuine environment limitation from an LLGL/CNA-side bug.

**Also found while adding `llgl_stencil_test.cpp` (new, LLGL-53): the stencil test does not
actually GATE.** Writing a reference value via `StencilOperation::Replace` and then reading it
back via a `CompareFunction::Equal` test against the SAME reference passes correctly (Check A), but
a mismatched reference (`CompareFunction::Equal` against a DIFFERENT value than what was written)
is NOT rejected -- the gated draw lands anyway, as if the compare function were always
`CompareFunction::Always` regardless of what was actually requested (Checks B and C's second half).
Root cause not identified; candidates not yet ruled out: the swap chain's own depth-stencil
attachment format may lack a real stencil channel despite `LLGL::SwapChainDescriptor::stencilBits`
defaulting to 8 (not yet confirmed via `swapChain_->GetDepthStencilFormat()`'s actual reported
value on this module), or `pipelineDesc.stencil.front.compareOp`/`readMask` may not be reaching the
GPU the way `pipelineDesc.depth.compareOp` demonstrably does (ordinary depth testing works
correctly elsewhere in this same backend).

**2026-08-04 cross-check on real Vulkan (`DISPLAY=:0 CNA_LLGL_RENDERER=vulkan`, this machine's own
physical AMD Radeon 780M/RADV -- see `feedback_real_display_vulkan_available` in memory for how):**
- **The stencil-doesn't-gate defect is confirmed ARCHITECTURAL**, not OpenGL-specific:
  `llgl_stencil_test.cpp` fails Checks B and C's second half IDENTICALLY under Vulkan (same exact
  output). Rules out anything GL-module-specific (`GLStateManager`'s own stencil state handling,
  the swap chain's GL depth-stencil renderbuffer format) as the sole cause -- whatever is wrong is
  in code shared between both renderer modules, most likely `AcquirePrimitivePipeline()`'s own
  `pipelineDesc.stencil` construction or how `stencilFunction_`/`stencilMask_`/etc. get captured,
  not either module's native stencil-state translation.
- **NEW, separate finding, plausible but NOT fully confirmed: constant `DepthBias` -- the ONE
  mechanism already verified WORKING on the OpenGL module (`A1`/`B1`/`C1`/`E1`/`G0` all pass, on
  Xvfb) -- appears to NOT work on the Vulkan module.** Under `CNA_LLGL_RENDERER=vulkan` on the real
  display, `rasterizerstate_depthbias_test.cpp`'s `A1`/`B1`/`C1`/`D1` (every bias-effect check)
  FAIL while every baseline (`A0`/`B0`/`C0`/`D0`/`D2`) PASSES -- a clean, internally-consistent
  split (bias-off always right, bias-on always wrong), not scattered/random failures, which is why
  this is treated as a plausible real finding rather than noise. **Two things were confirmed, not
  guessed:** (1) distinct `LLGL::PipelineState*` pointers ARE created for `DepthBias=0` vs
  `DepthBias=3000000` (`0x...b358` vs `0x...2778`, printed directly) -- this is NOT a repeat of the
  pipeline-cache-key-overflow bug fixed elsewhere in this ticket, the cache is behaving correctly.
  (2) LLGL's own `VKGraphicsPSO.cpp` (`CreateRasterizerState()`) was read directly and looks
  correct on its face: `depthBiasEnable` is computed from the same three fields, all three are
  copied verbatim into the static `VkPipelineRasterizationStateCreateInfo`, and
  `VK_DYNAMIC_STATE_DEPTH_BIAS` is correctly absent from the module's own dynamic-state list
  (meaning depth bias is INTENDED to be baked statically at creation time here, which the code does
  do) -- no LLGL-side dynamic-state override was found that could be clobbering it afterward.
  **However, a real confound was discovered during this same investigation that means this finding
  needs independent re-verification before being trusted fully:** running the SAME test suite
  against the OpenGL module on the real display (`CNA_LLGL_RENDERER=opengl DISPLAY=:0`, this
  machine's own physical desktop, GNOME/Mutter compositor) produced SCATTERED, internally
  INCONSISTENT results (`A0`/`B0`/`C0`/`D0` baselines FAIL -- unlike their reliable PASS on Xvfb --
  while `D1`, a bias-EFFECT check, unexpectedly PASSES) -- a pattern that looks like genuine
  window/compositor interference with this test's pixel-exact small-window readback assumptions
  (window decorations, DPI scaling, vsync/compositor double-buffering catching a mid-transition
  frame), NOT a real rendering defect. Since the Vulkan run was ALSO taken on this same real,
  composited display (not Xvfb), the same class of interference cannot be fully ruled out there
  either, even though its own result pattern is much cleaner/more consistent than GL's scattered
  one. **Root cause not identified either way.** Next step: re-run this exact comparison once a
  DRI3-capable Xvfb is available (`LLGL-55`'s own scope) to eliminate the real-desktop-compositor
  variable entirely and get a clean Vulkan-vs-OpenGL comparison with NEITHER run touching a real,
  composited window.
- `D1` (`SlopeScaleDepthBias`) also fails identically under Vulkan (consistent with -- not new
  information beyond -- its OpenGL failure already documented above).
- `H0`/`H1`/`I0`/`I1` were not re-tested under Vulkan this pass (the file aborts earlier under
  Vulkan on an unrelated, pre-existing, already-known limitation: `E0`'s `FillMode::WireFrame`
  throws `"not yet implemented"` on the Vulkan renderer module by design, per
  `AcquirePrimitivePipeline()`'s own `SupportsWireFrameEXT()` guard -- this is documented,
  intentional behaviour, not a new finding).

**Tracked as:** `plans/plan_llgl.md` Phase LLGL-8, `LLGL-53` -- left OPEN, now FOUR/FIVE distinct
defects, none root-caused (the Vulkan-specific `DepthBias` failure is a NEW fifth one, found only
once real Vulkan testing became possible on this machine). `Llgl_RasterizerState_DepthBias`
(12/17 on OpenGL) and `Llgl_Stencil` (2/4, same on both modules) are both registered anyway as real
regression trip-wires for whoever continues this, matching this session's own established
`Llgl_Deferred_Viewport`-style precedent for partial-pass suites.

---

## `CnaTests` gtest fixtures (not the `examples/` LLGL test binaries) have no Vulkan-WSI-unavailable guard at all, and one failure can break every later test in the same process — OPEN (LLGL-55-adjacent)

**Backend:** LLGL (Vulkan module), discovered incidentally while verifying the new
`LlglSdlSurfaceConstructor` unit test (`LLGL-56`) against a wider `--gtest_filter`.

**Symptom:** `LLGL-55`'s Vulkan-WSI-unavailable skip guard
(`examples/common/LlglVulkanWsiSkipGuard.cpp`) is linked only into the `examples/*_test.cpp`
binaries via `cna_llgl_test()` (`cmake/Tests/LlglTests.cmake`). It is **not** linked into
`CnaTests`, the general gtest binary that also builds and runs graphics-fixture tests against
whichever `CNA_GRAPHICS_BACKEND` is configured -- on this DRI3-less Xvfb with LLGL selected, a
fixture that constructs a real `GraphicsDevice` (e.g. `Texture3DTest`) throws the same
`VK_ERROR_SURFACE_LOST_KHR` mid-construction. gtest's own exception handling catches this cleanly
(reported as `[FAILED]`, not a process abort), so on its own this is "only" a false FAIL rather
than a crash. But the failing fixture's partially-torn-down `GraphicsDevice`/X11 client connection
appears to corrupt something process-wide: the *next* test run in the same process
(`TextureCubeTest` in the reproduction) immediately hits `"X connection to :99 broken (explicit
kill or server shutdown)"` and fails too. The shared Xvfb `:99` server itself is unaffected
(confirmed responsive via `xdpyinfo` immediately after) -- only that one process's own connection
dies, but since `ctest`'s own `gtest_discover_tests(... DISCOVERY_MODE PRE_TEST ...)` runs each
`TEST()` as its own separate process, ordinary `ctest` runs are NOT expected to cascade this way;
the cascade only reproduces when multiple such tests are forced into one process (e.g. a broad
`--gtest_filter` covering several graphics fixtures, as happened here).

**Reproduction:** `CNA_LLGL_RENDERER` unset (default, Vulkan-preferring), `SDL_VIDEODRIVER=x11`,
`DISPLAY=:99` (no DRI3): `./CnaTests --gtest_filter="*Window*"` (or any filter that pulls in
several `GraphicsDevice`-constructing fixtures into one process) -- the first such fixture reports
`[FAILED]` with the Vulkan swap-chain exception, and the next reports a broken X connection.

**Not investigated further this session** (out of scope for the `LLGL-56` unit test being added at
the time): whether every `CnaTests` fixture that constructs a real `GraphicsDevice` needs the same
kind of guard `LLGL-55` gave the `examples/` binaries, or whether `ctest`'s per-test process
isolation already makes this moot in normal CI use. Needs its own investigation before being
folded into `LLGL-55`'s own scope or closed as "ctest isolation already covers it."

**Tracked as:** new, not yet in `plans/plan_llgl.md`'s task table -- follow-up for whoever continues
`LLGL-55`/`LLGL-56`.

---
