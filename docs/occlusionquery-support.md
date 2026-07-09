# OcclusionQuery: support and limitations

Covers `Microsoft::Xna::Framework::Graphics::OcclusionQuery` across all 4 backends. Written as the
closing documentation task for Phase 50 (Tasks 441-450), which audited FNA's real API surface,
verified CNA's own `Begin()`/`End()`/`IsComplete()`/`PixelCount()` behavior against it, added real
pixel/query correctness tests, implemented a genuine fix on Bgfx, and investigated (without
guessing) a real architecture blocker on Vulkan.

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
creates an `IOcclusionQueryBackend` via `device.GetBackend().CreateOcclusionQuery()`,
`getIsCompleteProperty()`/`getPixelCountProperty()`/`Begin()`/`End()` are all simple forwards with
a null-`backend_` guard, and CNA also does zero sequence validation — correctly matching FNA's own
lack of one rather than inventing stricter behavior FNA never had (Tasks 442-444, all confirmed via
sabotage-and-revert).

**`Dispose()` note** (Task 449): CNA's `OcclusionQuery` has no `Dispose(bool)` override of its own
— the base `GraphicsResource::Dispose(bool)` never touches the backend at all, so real backend
teardown only happens in `~OcclusionQuery()`, not the XNA `Dispose()` method. This is a project-wide
convention (neither `Texture2D` nor `VertexBuffer` override it either), not an `OcclusionQuery`-
specific gap. Confirmed safe to destroy a query that's still "active" (`Begin()` called, no
matching `End()`) via a 50-iteration stress test — no crash, no resource-tracking leak.

## Per-backend support matrix

| Backend | Attaches to real GPU work? | Sequence validation | Pixel/query correctness | Status |
|---|---|---|---|---|
| **EasyGL** | ✅ Yes — thin `glBeginQuery`/`glEndQuery(GL_ANY_SAMPLES_PASSED)` wrapper | None (matches FNA) | ✅ Verified both directions (Tasks 445/446) | **Fully correct** |
| **Vulkan** | ❌ No — `Begin()`/`End()` never inject `vkCmdBeginQuery`/`vkCmdEndQuery` | N/A (unreachable) | Always reports 0 (functionally inert) | **BLOCKED** (Task 447) |
| **Bgfx** | ✅ Yes (Task 448) — real `bgfx::submit(id, program, occlusionQuery)` attachment | None (matches FNA) | ⚠️ Not verifiable in this sandbox (see below); dedicated-view gap open (Task 917) | **Fixed, with caveats** |
| **SDL_Renderer** | N/A — construction itself throws | N/A | N/A | **Correctly unsupported** (2D-only backend, Task 727) |

### EasyGL — fully correct

`EasyGLOcclusionQueryBackend` is a thin, unvalidated wrapper over `easygl::Query`'s own
`glBeginQuery`/`glEndQuery(GL_ANY_SAMPLES_PASSED)` calls, with zero internal state tracking — all 3
invalid call sequences (End-before-Begin, double-Begin, double-End) just produce a silent,
unchecked `GL_INVALID_OPERATION`, never a crash or C++ exception (Tasks 442-444). Two real pixel/
query correctness tests prove the query genuinely reports the right answer, not just "doesn't
crash": `EasyGL_OcclusionQuery_VisibleQuad` (Task 445, a fully visible quad reports `PixelCount() >
0`) and `EasyGL_OcclusionQuery_OccludedQuad` (Task 446, a quad hidden behind a nearer opaque
occluder — rejected by `DepthStencilState::Default`'s `LessEqual` compare — reports `PixelCount()
<= 0`). Both independently confirmed via sabotage-and-revert. This is the only backend where
occlusion queries are both wired up AND pixel-verified correct.

### Vulkan — BLOCKED, functionally inert

`VulkanOcclusionQueryBackend::Begin()`/`End()` never inject `vkCmdBeginQuery`/`vkCmdEndQuery`,
because this backend defers ALL 3D and 2D draw calls into `pending3D_`/`activeBatches_` snapshots,
recorded into real Vulkan commands only once per frame inside `RecordCommandBuffer` — well after
`Begin()`/`End()` (called synchronously by game code around a draw call) already returned.
`Pending3DDraw` has no query-association field at all. Occlusion queries on Vulkan currently always
report 0 visible pixels regardless of real visibility — safe (no crash) but functionally inert.

Fixing this for real (Task 447, investigated but left BLOCKED — not guessed at) requires resolving
3 genuinely non-obvious design questions:

1. How to tag deferred draw entries with "recorded inside query X's Begin/End span" so
   `RecordCommandBuffer` can wrap the matching `vkCmdDraw*` calls correctly.
2. Whether a query may legally span multiple draw calls (matching real FNA semantics, but Vulkan
   requires `vkCmdBeginQuery`/`vkCmdEndQuery` to stay within a single render-pass instance) or must
   be restricted to exactly one (simpler, but a real FNA-capability deviation).
3. How to correctly re-`vkCmdResetQueryPool` a reused `OcclusionQuery` object's slot before each new
   frame's Begin/End cycle (currently reset only once, at construction) — this task's own "avoid
   stale reads" hint directly names this exact hazard for the idiomatic real-world pattern of
   reusing one query object across many frames.

### Bgfx — fixed, with two honestly-documented caveats

Before Task 448, `BgfxOcclusionQueryBackend::Begin()`/`End()` were literal empty no-ops — no
`bgfx::setCondition()` or occlusion-query `submit()` overload was used anywhere, so the created
query handle was never wired to any draw call at all.

**Fixed**: `BgfxGraphicsBackend` now tracks an `activeOcclusionQuery_` handle, set by `Begin()` and
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
this 2D-only backend's established "throw at construction for unsupported 3D constructs" pattern.
Since construction itself throws, `Begin()`/`End()` are unreachable — consistent, no gap.

## Summary

| Area | Status |
|---|---|
| FNA API surface + Begin/End sequence behavior | ✅ Fully audited; CNA correctly matches FNA's own lack of validation on every backend that reaches user code (Tasks 441-444) |
| `Dispose()`/active-query-destruction safety | ✅ Verified safe on EasyGL via 50-iteration stress test (Task 449) |
| EasyGL pixel/query correctness | ✅ Both directions (visible → positive, occluded → zero) pixel-verified (Tasks 445-446) |
| Vulkan | ❌ Functionally inert (always reports 0); real fix needs a genuine architecture decision, BLOCKED (Task 447) |
| Bgfx | ✅ Wiring fixed per bgfx's documented API (Task 448); pixel-level correctness unverifiable in this sandbox; dedicated-view gap for true scene-depth correctness still open (Task 917) |
| SDL_Renderer | ✅ Correctly throws at construction (2D-only backend, Task 727) |
