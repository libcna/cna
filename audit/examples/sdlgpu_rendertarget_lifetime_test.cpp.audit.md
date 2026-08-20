# Audit: examples/sdlgpu_rendertarget_lifetime_test.cpp

## Metadata

- Source file: `examples/sdlgpu_rendertarget_lifetime_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlgpu` shard — render-target-destroyed-before-flush use-after-free
  regression proof for the SDL_GPU backend
- File type: standalone `Game`-subclass executable, CTest-registered
  (`SdlGpu_RenderTargetLifetime`, `cmake/Tests/SdlGpuTests.cmake:122-124`, `TIMEOUT 60 LABELS
  "SdlGpu"`)
- XNA/FNA relevance: indirect — exercises `RenderTarget2D`'s XNA-facing lifetime (construction,
  binding, destruction) but the actual regression under test is a CNA/SDL_GPU-backend-internal
  resource-management concern with no direct XNA-facing symptom (real XNA/D3D9 has no equivalent
  deferred-command-buffer architecture to reproduce this class of bug in).
- Related production code: `src/CNA/Internal/Backends/SdlGpu/SdlGpuGraphicsBackend.cpp`
  (`SdlGpuRenderTarget2DState::~SdlGpuRenderTarget2DState` lines 4244-4253, `QueueTextureRelease`
  lines 602-606, `SdlGpuRenderTargetBackend::MarkUsedThisFrame`/`BindAsRenderTarget` lines
  4348-4369, `EnsureFrameRendered`'s pending-release flush lines 754-760).

## Purpose

Two-check regression test for a real, previously-hit use-after-free: this backend defers all
draws/clears to `Present()`-time (`EnsureFrameRendered()`), but a `RenderTarget2D` destroyed
earlier in the same frame (before `Present()` runs) used to release its real `SDL_GPUTexture*`
handle immediately, synchronously, at C++ scope-exit — even though already-queued, not-yet-rendered
commands (e.g. a `SpriteBatch` draw sampling its contents) could still reference that now-freed
handle. Check A creates a `RenderTarget2D` as a `Draw()`-local variable, draws into it, queues a
`SpriteBatch` draw that samples it into a second, surviving target, and lets it destruct — all
within one `Draw()` call, before `Present()` ever runs for that frame — then verifies via
`GetData()` that the sampled content genuinely landed in the surviving target. Check B repeats the
same pattern every frame for 120 frames.

## Executive Verdict

**Healthy.** This audit independently traced the actual fix (a `shared_ptr`-owned
`SdlGpuRenderTarget2DState` plus a deferred `QueueTextureRelease`/`pendingTextureReleases_`
mechanism) and confirmed it correctly handles the exact sequence this test exercises, including the
specific detail that `GetData()` itself forces an eager `EnsureFrameRendered()` flush — meaning the
regression scenario here is genuinely exercised *after* the local `RenderTarget2D`'s C++ object has
already been destroyed, not merely before `Present()` is nominally due. No defect found.

## Checklist Results

### API / XNA / FNA parity

N/A in the strict sense (no distinct XNA API surface beyond ordinary `RenderTarget2D`
construction/binding/`SpriteBatch::Draw` already covered by sibling files in this shard) — this
file's value is entirely in the backend-internal resource-lifetime property it proves.

### Behavioral correctness / Memory/resource lifetime

Traced `DrawThroughShortLivedRenderTarget` (lines 84-102) against the actual fix:

- `RenderTarget2D localRt(...)` (line 86) constructs a `SdlGpuRenderTargetBackend` whose `state_`
  is a `shared_ptr<SdlGpuRenderTarget2DState>` (confirmed in the ctor, `SdlGpuGraphicsBackend.cpp`
  line 4259).
- `dev.SetRenderTarget(&localRt)` → `BindAsRenderTarget()` → `MarkUsedThisFrame()` (lines
  4358, 4361-4369) pushes a **copy of this same `shared_ptr`** into
  `owner_->usedRenderTargetsThisFrame_` (line 4367) — this is the mechanism that keeps the
  underlying GPU state alive independent of `localRt`'s own C++ lifetime.
- The queued `SpriteBatch` draw (`sb_->Draw(localRt, ...)`, line 95) captures a raw
  `SDL_GPUTexture*` at queue time (traced via this shard's own `SpriteBatch::Draw` resolution
  path in sibling files) — it does not itself extend the state's lifetime, but does not need to,
  since `usedRenderTargetsThisFrame_` already does.
- `localRt` destructs at the end of `DrawThroughShortLivedRenderTarget()` (line 102, per the
  function's own comment) — `SdlGpuRenderTargetBackend::~SdlGpuRenderTargetBackend()` (lines
  4336-4346) only clears `owner_->currentRenderTarget_` if it still points at `this` (already
  unbound by this point) and explicitly does **not** remove `state_` from
  `usedRenderTargetsThisFrame_` (per its own comment, lines 4340-4345) — the wrapper's destruction
  drops only *this* `shared_ptr` reference; the state survives via the frame-tracking list's own
  copy.
- `rtDest_->GetData()` is called immediately afterward, in the *same* `Draw()` call (line 138) —
  confirmed via `SdlGpuRenderTargetBackend::GetData()` (traced in this shard's sibling
  `sdlgpu_rendertarget2d_test.cpp` report) that this call itself invokes
  `owner_->EnsureFrameRendered()` **eagerly**, before this frame's normal `Present()` would ever
  run. At the moment this eager flush executes, `localRt`'s C++ object is already destroyed (its
  destructor ran at the end of the prior line), but its `state_`'s `SDL_GPUTexture*` handles are
  still alive (kept alive by `usedRenderTargetsThisFrame_`'s own `shared_ptr` copy) — so
  `RenderToTarget()` can still safely process `localRt`'s queued `Clear(Red)` and the `SpriteBatch`
  draw sampling it, in first-bind order, before `rtDest_`'s own pass consumes the result.
- Only once `EnsureFrameRendered()` reaches `usedRenderTargetsThisFrame_.clear()` (line 775, after
  the render pass has already executed and the command buffer submitted) does the last `shared_ptr`
  reference to `localRt`'s state drop, triggering `~SdlGpuRenderTarget2DState()` (lines 4244-4253),
  which calls `QueueTextureRelease()` for its 3 textures — deferred to the *next*
  `EnsureFrameRendered()` call's own pending-release flush (lines 754-760), never released while
  still possibly referenced by the just-submitted command buffer.

This is a genuine, correctly-implemented fix for the exact hazard the test's header comment
describes, independently confirmed by reading the actual code rather than inferred from the test
passing.

### Behavioral correctness — GetData() timing interaction (a subtlety worth calling out explicitly)

The regression this test targets is specifically about a render target destructing **before**
`Present()` nominally runs. Because `GetData()` itself forces an eager flush (see above), the
actual sequence exercised is: bind→draw→queue-sample→destruct→(later, same `Draw()` call)
`GetData()`-forced-flush — i.e. the destruction genuinely precedes the flush that processes its
queued commands, which is exactly the scenario the fix targets, not a weaker "destructs after
Present() already ran" case that would trivially not exercise the bug at all. This was worth
tracing precisely since a naive reading of the header comment ("all as a local variable ... before
this frame's Present() ever runs") could otherwise be mistaken for describing a scenario the
in-`Draw()` `GetData()` call itself subverts by triggering an early flush — it does not, because
the destruction (end of `DrawThroughShortLivedRenderTarget()`) happens strictly before the
`GetData()` call that triggers the flush (both on line 126-138 of `Draw()`, in that order).

### C++ correctness

`Color px(0,0,0,0);` (line 136) is deliberately black/transparent, not the target's clear color
(`Color::Blue`, line 93) — chosen so the assertion (`Matches(px, Color::Red)`, line 139) cannot
accidentally pass from an untouched buffer defaulting to the destination's own clear color; the
only way this reads back Red is if the sampled content genuinely propagated from `localRt`.

### Robustness

Check B's 120-frame repeat of the identical local-create/draw/sample/destroy pattern (lines
113-157) is a meaningful strengthening beyond Check A alone: a one-shot pass could theoretically
succeed by accident (e.g. if the driver happens not to reuse the freed memory range before the
queued command executes, even with the deferred-release fix genuinely absent), while a
sustained 120-frame repeat makes a use-after-free far more likely to manifest as a visible
corruption or crash if the fix regressed.

### Testing

This is a targeted regression test for a specific, previously-real bug rather than a general
feature-coverage test — appropriately scoped, and its own claims were independently verified
against the actual current production code (not merely re-stated from the header comment).

## Cross-File Observations

- Directly complements `sdlgpu_mrt_test.cpp`'s own design choice in this same shard/batch: that
  file's render targets are deliberately kept as **members**, with its own header comment
  explaining this is specifically to *avoid* re-triggering the exact hazard this file exists to
  regression-test. The two files are consistent with each other: one proves the fix by
  deliberately hitting the former-crash scenario, the other avoids the scenario entirely as
  defense-in-depth. Confirmed both files' own stated rationale against the same underlying
  production mechanism (`usedRenderTargetsThisFrame_` + deferred `QueueTextureRelease`).
- `plans/plan_sdlgpu.md`'s SDLGPU-37 row (secondary context per D-3) independently confirms this was a
  real, git-log-documented finding ("a real, non-test-specific finding from writing this test: a
  `RenderTarget2D`/`RenderTargetCube` that goes out of scope ... before this backend's deferred
  `Present()`-time render pass actually executes is a real use-after-free ... caught via a genuine
  segfault when this test's first draft used `Draw()`-local `RenderTarget2D` instances instead of
  members ... fixed for real 2026-07-15 — see execution-order item 12 (a `shared_ptr`-owned
  GPU-state redesign, not just a deferred-release patch)") — this file is very likely the
  dedicated regression test written as a direct consequence of that finding (its file name and
  header comment both track that exact narrative), and this audit's independent code trace
  confirms the described fix is genuinely present and correctly ordered.
- Fog and skinned-normal-transform cross-cutting bugs are **not applicable**: this file has no
  lighting/effect code path at all — its only draws are `Clear()` calls and a plain
  `SpriteBatch::Draw` of an untextured-effect quad.
- Also notes (per `plans/plan_sdlgpu.md`'s own text) that `currentExtraMrtTargets_` (the MRT-sibling
  bookkeeping `sdlgpu_mrt_test.cpp` exercises) is explicitly **not** converted to the same
  `shared_ptr`-based fix and remains "its own narrower, separate version of this risk ... out of
  scope in that same item" — i.e. a render target used only as an MRT *secondary* attachment
  (never `currentRenderTarget_` itself) may not benefit from the identical protection this file
  proves for the primary-binding case. This file's own two checks do not exercise the MRT-sibling
  path, so this narrower residual risk is neither proven nor disproven by this specific file —
  flagged here as a scope boundary worth a dedicated test if the codebase's own documented
  "out of scope" carve-out is ever revisited.

## Missing or Weak Tests

- Per the Cross-File Observations note above: no test in this shard (including this file)
  specifically exercises a short-lived **MRT-sibling** render target destructing mid-frame (as
  opposed to a short-lived **primary** render target, which this file does cover) — the project's
  own plan document explicitly flags this as a known, separate, unconverted risk. Not a defect in
  this file (out of its stated scope), but worth flagging as an open gap for a future test.

## Positive Findings

- The actual fix (shared_ptr-owned state + deferred texture release) was independently traced and
  confirmed to correctly handle the exact sequence this test exercises, including the non-obvious
  interaction between `GetData()`'s own eager-flush behavior and the destruction ordering the test
  relies on.
- Check B's sustained 120-frame repeat meaningfully strengthens Check A's single-shot proof against
  the possibility of an accidental one-off pass.
- This file's design (deliberately re-creating the exact former-crash pattern, rather than merely
  asserting the fix's presence indirectly) is a strong, direct regression-test shape — precisely
  the kind of "test that would have caught the original bug" this checklist's Testing section asks
  every file to be judged against.

## Final Assessment

A well-targeted, correctly-verified regression test for a real, previously-hit use-after-free.
This audit independently traced the production fix line-by-line against the exact sequence the
test exercises and found it sound, including a subtle timing detail (this file's in-`Draw()`
`GetData()` call forcing an eager flush) that could easily have been misread as weakening the
test's own claimed coverage but does not. One residual, explicitly-scoped-out risk (MRT-sibling
render targets) is noted for future test coverage, not as a defect in this file.
