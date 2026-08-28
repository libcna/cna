# OcclusionQuery: support and limitations

Covers `Microsoft::Xna::Framework::Graphics::OcclusionQuery` across the original four-renderer
audit and the later Skia raster decision. Written as the closing documentation task for Phase 50
(Tasks 441-450), which audited FNA's real API surface,
verified CNA's own `Begin()`/`End()`/`IsComplete()`/`PixelCount()` behavior against it, added real
pixel/query correctness tests, and implemented a genuine fix on Bgfx. Vulkan's own real
architecture blocker (Task 447), investigated without guessing at the time, was later fully
resolved once the project owner picked a direction for its 3 open design questions (Task 854,
2026-07-10) — see its own section below.

## FNA's real API surface (Task 441's audit)

FNA's `OcclusionQuery` (`src/Graphics/OcclusionQuery.cs`) is a remarkably minimal `GraphicsResource`
subclass:

- `IsComplete`/`PixelCount` — one-line forwards to `FNA3D_QueryComplete`/`FNA3D_QueryPixelCount`.
- Constructor — one-line forward to `FNA3D_CreateQuery`.
- `Dispose(bool)` — an `Interlocked.Exchange` swap-and-null then `FNA3D_AddDisposeQuery` if the
  query wasn't already null.
- `Begin()`/`End()` — one-line forwards to `FNA3D_QueryBegin`/`FNA3D_QueryEnd`.

**Critical finding: FNA has ZERO C#-level validation of Begin/End call sequence.** There is no
guard anywhere against `End()` before `Begin()`, double `Begin()`, or double `End()` — whatever
happens for those is entirely up to the native FNA3D library / GPU driver, never surfaced as a
.NET exception by FNA itself. This directly corrected this project's own original task framing
(Tasks 442-444 were titled "...Match FNA exception" — there is no such exception to match).

CNA's own `OcclusionQuery.hpp`/`.cpp` matches this shape closely and correctly: the constructor
creates an `IOcclusionQueryRenderer` via `device.GetRenderer().CreateOcclusionQuery()`,
`getIsCompleteProperty()`/`getPixelCountProperty()`/`Begin()`/`End()` are all simple forwards with
a null-`renderer_` guard, and CNA also does zero sequence validation — correctly matching FNA's own
lack of one rather than inventing stricter behavior FNA never had (Tasks 442-444, all confirmed via
sabotage-and-revert).

**`Dispose()` note** (Task 449): CNA's `OcclusionQuery` has no `Dispose(bool)` override of its own
— the base `GraphicsResource::Dispose(bool)` never touches the renderer at all, so real renderer
teardown only happens in `~OcclusionQuery()`, not the XNA `Dispose()` method. This is a project-wide
convention (neither `Texture2D` nor `VertexBuffer` override it either), not an `OcclusionQuery`-
specific gap. Confirmed safe to destroy a query that's still "active" (`Begin()` called, no
matching `End()`) via a 50-iteration stress test — no crash, no resource-tracking leak.

## Per-renderer support matrix

| Renderer | Attaches to real GPU work? | Sequence validation | Pixel/query correctness | Status |
|---|---|---|---|---|
| **EasyGL** | ✅ Yes — `glBeginQuery`/`glEndQuery`, asking for `GL_SAMPLES_PASSED` and falling back to the boolean `GL_ANY_SAMPLES_PASSED` | None (matches FNA) | ✅ Verified both directions (Tasks 445/446) **and count-vs-flag** (SAMPLE-041) | **Correct; the count is precise only where the driver has `GL_SAMPLES_PASSED`** |
| **Vulkan** | ✅ Yes (Task 447, 2026-07-10) — real per-draw-call tagging + `vkCmdBeginQuery`/`vkCmdEndQuery` recording | None (matches FNA) | ✅ Verified both directions plus multi-draw-span (Task 854) — genuinely discriminating in this sandbox (Mesa Lavapipe) | **Fully correct** |
| **Bgfx** | ✅ Yes (Task 448) — real `bgfx::submit(id, program, occlusionQuery)` attachment | None (matches FNA) | ⚠️ Not verifiable in this sandbox (see below); dedicated-view gap open (Task 917) | **Fixed, with caveats** |
| **SDL_Renderer** | N/A — construction itself throws | N/A | N/A | **Correctly unsupported** (2D-only renderer, Task 727) |
| **Skia raster** | N/A — no 3D submission/depth surface | N/A | Raster emulation disproved (SKIA-104) | **Correctly unsupported** (SKIA-105) |

### EasyGL — correct, with a precision boundary that depends on the profile

`EasyGLOcclusionQueryRenderer` is a thin, unvalidated wrapper over `easygl::Query`'s own
`glBeginQuery`/`glEndQuery(GL_ANY_SAMPLES_PASSED)` calls, with zero internal state tracking — all 3
invalid call sequences (End-before-Begin, double-Begin, double-End) just produce a silent,
unchecked `GL_INVALID_OPERATION`, never a crash or C++ exception (Tasks 442-444). Two real pixel/
query correctness tests prove the query genuinely reports the right answer, not just "doesn't
crash": `EasyGL_OcclusionQuery_VisibleQuad` (Task 445, a fully visible quad reports `PixelCount() >
0`) and `EasyGL_OcclusionQuery_OccludedQuad` (Task 446, a quad hidden behind a nearer opaque
occluder — rejected by `DepthStencilState::Default`'s `LessEqual` compare — reports `PixelCount()
<= 0`). Both independently confirmed via sabotage-and-revert. This is the only renderer where
occlusion queries are both wired up AND pixel-verified correct.

#### The count is a count only where the driver has `GL_SAMPLES_PASSED` (SAMPLE-041, 2026-08-27)

XNA's `PixelCount` is a **tally of the fragments that passed**, which is what Direct3D 9 returns.
**OpenGL ES 3.0 and WebGL 2 have no query target that produces one**: their core occlusion target
is the boolean `GL_ANY_SAMPLES_PASSED`, whose result is 0 or 1 whatever the geometry covered.

Both tests above assert `PixelCount() > 0` and `PixelCount() <= 0`, and **a boolean passes both**.
That is why this row said "fully correct" while a game dividing `PixelCount()` by an area — the
lensflare idiom, and the whole subject of SAMPLE-041 — got `1/area` instead of a coverage fraction
and faded its effect to nothing. Measured there: the query rectangle covered **9788** pixels of
the frame and `PixelCount()` answered **1**.

`EasyGLOcclusionQueryRenderer` now resolves its target from the driver on the first query: it
begins `GL_SAMPLES_PASSED` and keeps it if GL accepts the enum, otherwise it falls back to the
boolean for the life of the process. The distinction is askable rather than folklore —
`OcclusionQuery::isPixelCountPreciseEXT()` (CNAEXT), forwarding
`IOcclusionQueryRenderer::PixelCountIsPreciseEXT()`, which defaults to `true` so no other backend
had to change.

FNA is **worse** here rather than equivalent: `FNA3D_Driver_OpenGL.c` uses `GL_SAMPLES_PASSED`
unconditionally and asserts `supports_ARB_occlusion_query`; on its ES3 path a missing extension
only logs *"Occlusion queries unsupported, beware..."*, and off ES it is a fatal device-creation
error. FNA therefore declares occlusion queries unsupported on ES rather than degrading them to a
flag. CNA keeps them working as a flag and upgrades to a real count wherever the driver has the
precise target.

The asymmetry is real and driver-side, not a CNA choice. Listing what each profile exposes on the
**same Mesa 25.0.7** settles it: the ES 3.2 profile offers `GL_EXT_occlusion_query_boolean` and
nothing else, while the desktop profile offers `GL_ARB_occlusion_query` (a count),
`GL_ARB_occlusion_query2` and the EXT boolean. No ES extension anywhere adds a precise count, and
the WebGL registry has none either. Asking the driver for the enum answers the same way:

| Context | `GL_SAMPLES_PASSED` accepted | Fragments reported for a fully covered viewport |
|---|---|---|
| OpenGL 4.5 compatibility (`spikes/occlusion-count-spike/`) | yes | **4096** — the 64x64 viewport's own area |
| OpenGL ES 3.2, which CNA's `OPENGLES3` profile creates | no | 1 |

`modules/graphics/tests/.../OcclusionQueryPixelCountPrecisionTests.cpp` pins the pair: a
full-viewport quad must report a real tally when the query claims precision and at most 1 when it
does not, with back-buffer readbacks proving the quad genuinely covered the frame. Confirmed to
fail when the precision claim is falsified.

### Vulkan — fixed, all 3 design questions resolved (Task 447/854, 2026-07-10)

`VulkanOcclusionQueryRenderer::Begin()`/`End()` previously never injected `vkCmdBeginQuery`/
`vkCmdEndQuery`, because this renderer defers ALL 3D and 2D draw calls into `pending3D_`/
`activeBatches_` snapshots, recorded into real Vulkan commands only once per frame inside
`RecordCommandBuffer` — well after `Begin()`/`End()` (called synchronously by game code around a
draw call) already returned. `Pending3DDraw` had no query-association field at all, so occlusion
queries on Vulkan always reported 0 visible pixels regardless of real visibility.

Fixing this required resolving 3 genuinely non-obvious design questions, all now implemented per
the project owner's decision to do the full fix:

1. **Tagging** — a new `VulkanOcclusionQueryRenderer* occlusionQuery` field on `Pending3DDraw`, set
   uniformly by a new `VulkanRenderer::PushPending3DDraw()` choke point (all 6
   `pending3D_.push_back` call sites now route through it) from a new `activeOcclusionQuery_`
   member, set by `Begin()` and cleared by `End()` (mirrors Bgfx's own convention).
2. **Multi-draw-span policy** — a query MAY span multiple draw calls, as long as they all land in
   the same render pass (i.e. target the same render target/backbuffer with no intervening
   `SetRenderTarget` switch): `RecordCommandBuffer`'s `draw3DFor()` tracks contiguous runs of draws
   sharing the same query tag and wraps each run in one real `vkCmdBeginQuery`/`vkCmdEndQuery`
   pair. A query spanning a render-pass boundary (or a non-contiguous 2nd run within the same
   render pass) is NOT re-opened — a `recordedThisFrame_` flag ensures only the first contiguous
   run each frame is ever actually recorded, avoiding a Vulkan validation error (double
   `vkCmdBeginQuery` without an intervening reset) rather than attempting to correctly sum results
   across multiple render passes (a real FNA capability this implementation doesn't fully cover,
   documented here rather than silently assumed).
3. **Per-frame `vkCmdResetQueryPool` sequencing** — previously reset only once, at construction;
   `RecordCommandBuffer()` now scans `pending3D_` for every distinct query tagged that frame and
   issues one reset per query, before any render pass begins (Vulkan requires resets outside a
   render pass instance) — so a query reused every frame (the idiomatic real-world usage pattern)
   gets a fresh reset each time, not just the first.

New `modules/renderers/vulkan/examples/vulkan_occlusionquery_pixelcount_test.cpp` (`Vulkan_OcclusionQuery_PixelCount`, 6/6
pass): a fully-visible 64×64 quad reports `PixelCount()==4096` (the exact pixel count); a quad
fully hidden behind a nearer opaque occluder reports `PixelCount()==0`; a 3rd scenario draws 2
non-overlapping half-quads inside ONE `Begin()`/`End()` span and confirms `PixelCount()==4096`
(summed across both draws), directly exercising decision 2's multi-draw-span policy. Unlike Bgfx's
own identical-shaped test, this sandbox's software Vulkan renderer (Mesa Lavapipe) reports fully
accurate, discriminating pixel counts — no sandbox-limitation caveat needed. Verified via `git
stash` revert-and-rebuild (reverting reproduced exactly the predicted failure, the 2
query-correlation checks failing with `IsComplete()` never becoming true).

### Bgfx — fixed, with two honestly-documented caveats

Before Task 448, `BgfxOcclusionQueryRenderer::Begin()`/`End()` were literal empty no-ops — no
`bgfx::setCondition()` or occlusion-query `submit()` overload was used anywhere, so the created
query handle was never wired to any draw call at all.

**Fixed**: `BgfxRenderer` now tracks an `activeOcclusionQuery_` handle, set by `Begin()` and
cleared by `End()`; a new `SubmitViewProgram()` helper routes all 12 3D-draw `submit()` call sites
through bgfx's own dedicated `submit(id, program, occlusionQuery, depth, flags)` overload whenever
a query is active — this exactly matches bgfx's documented API contract and its own official
`26-occlusion` example's usage of the same overload. Since bgfx submits every 3D draw call
synchronously (unlike Vulkan's deferred command recording), no correlation/tagging machinery is
needed for the attachment itself.

**Caveat 1 — discriminating power could not be established in this sandbox.** Sabotaging
`Begin()`/`End()` back to pure no-ops (the exact pre-fix state) and rerunning the new
`Bgfx_OcclusionQuery` test produced IDENTICAL `IsComplete()`/`PixelCount()` behavior to the fixed
version — this sandbox's software Mesa GL 2.1 (llvmpipe) renderer returns a non-`NoResult` value
even for a query handle that was NEVER submitted anywhere at all. Neither value can serve as a
discriminating signal in this specific environment (a genuine software-driver limitation, matching
this project's own already-established `Bgfx_RenderTarget2D_MsaaResolve`/Vulkan-DRI3-unavailable
precedent for this exact sandbox — not a CNA code defect). The shipped test honestly asserts only
what does discriminate here (no throw, correct rendering unaffected by the new plumbing), reporting
`IsComplete()`/`PixelCount()` informationally only, with the limitation explained in the test
file's own header comment.

**Caveat 2 — new Task 917, dedicated-view architecture gap.** bgfx's own official example attaches
its occlusion-measurement `submit()` to a SEPARATE, dedicated view from the "real" visible-scene
view, specifically so the query's own sample count isn't polluted by other geometry drawn earlier
in the same view/depth buffer. CNA's fix currently attaches the query to whichever view the game's
normal 3D draw already targets, shared with everything else drawn there that frame. Reproducing
bgfx's dedicated-view pattern (likely reusing the existing `Detail::AllocateRtViewId()`/
`ReleaseRtViewId()` free-list infrastructure already used for render targets, Task 910) is needed
for a query's result to reflect ONLY its own geometry's true visibility against the real scene's
already-drawn depth. Not attempted — a real further architecture addition, deferred, and one this
specific software-rendering sandbox couldn't verify anyway.

### SDL_Renderer — correctly unsupported

`CreateOcclusionQuery()` correctly calls `ThrowNo3D("CreateOcclusionQuery")` (Task 727), matching
this 2D-only renderer's established "throw at construction for unsupported 3D constructs" pattern.
Since construction itself throws, `Begin()`/`End()` are unreachable — consistent, no gap.

### Skia raster — correctly unsupported

The selected CPU raster `SkCanvas` exposes completed colour pixels, not per-draw samples that pass
depth/stencil testing. `Skia_OcclusionQuery_Feasibility` proves framebuffer differences cannot
even recover EasyGL's boolean result: a full same-colour/destination-preserving draw and a draw with
zero coverage have byte-identical output. The pinned raster build excludes Ganesh/Graphite and has
no depth attachment. A safe refusal object therefore reports false/zero properties while Begin/End
throw the stable Skia 3D diagnostic; capability reporting stays false. The complete reasoning is in
`docs/skia-occlusion-query-feasibility.md`.

## Summary

| Area | Status |
|---|---|
| FNA API surface + Begin/End sequence behavior | ✅ Fully audited; CNA correctly matches FNA's own lack of validation on every renderer that reaches user code (Tasks 441-444) |
| `Dispose()`/active-query-destruction safety | ✅ Verified safe on EasyGL via 50-iteration stress test (Task 449) |
| EasyGL pixel/query correctness | ✅ Both directions (visible → positive, occluded → zero) pixel-verified (Tasks 445-446) |
| Vulkan | ✅ Real per-draw-call query correlation implemented (Task 447/854, 2026-07-10); pixel/query correctness verified both directions plus multi-draw-span, genuinely discriminating in this sandbox |
| Bgfx | ✅ Wiring fixed per bgfx's documented API (Task 448); pixel-level correctness unverifiable in this sandbox; dedicated-view gap for true scene-depth correctness still open (Task 917) |
| SDL_Renderer | ✅ Correctly throws at construction (2D-only renderer, Task 727) |
| Skia raster | ✅ Framebuffer/mask/GPU alternatives audited; deterministic false/zero/throw refusal retained (SKIA-104–105) |
