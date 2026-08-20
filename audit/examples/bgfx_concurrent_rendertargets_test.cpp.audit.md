# Audit: examples/bgfx_concurrent_rendertargets_test.cpp

## Metadata

- Source file: `examples/bgfx_concurrent_rendertargets_test.cpp` (153 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — concurrent multi-`RenderTarget2D` per-frame bind/fill
  regression test (Task 910)
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_bgfx_test(cna_test_bgfx_concurrent_rendertargets …)` /
  `cna_register_backend_test(NAME Bgfx_ConcurrentRenderTargets …)`,
  `cmake/Tests/BgfxTests.cmake:520-522`).
- XNA/FNA relevance: indirect — `Microsoft.Xna.Framework.Graphics.RenderTarget2D`/
  `GraphicsDevice.SetRenderTarget` are real XNA API surface; the specific bug this test targets
  (a shared bgfx view id across concurrently-live render targets) is a pure Bgfx-backend
  implementation detail with no FNA analogue (FNA/D3D9-family backends have no equivalent
  per-view-per-frame resource-binding model).
- Related production code: `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp`
  (`Detail::AllocateRtViewId`/`ReleaseRtViewId`, lines 629-666;
  `BgfxRenderTargetBackend::BindAsRenderTarget`/`UnbindAsRenderTarget`, lines 668-727;
  `BgfxGraphicsBackend::SetRenderTarget2D`, lines 740-761).

## Purpose

Reproduces the exact scenario Task 910 fixed: bind `RenderTarget2D` A, clear it; **without any
`bgfx::frame()` boundary**, bind `RenderTarget2D` B, clear it; unbind; then verify **both** RTs
independently show their own real content, not just the last one bound. Structured as a 4-step state
machine across separate `Draw()` calls (prime A → green baseline; fill A→red, B→blue with no frame
boundary between; read A; read B), specifically so the priming read and the two "real" reads each land
in genuinely separate rendered frames while the two fills under test do **not**.

## Executive Verdict

**Mostly healthy** — this audit independently traced the entire production fix
(`Detail::AllocateRtViewId`/`ReleaseRtViewId`'s free-list pool, each `RenderTarget2D`'s own distinct,
stable `viewId_`, and `SetRenderTarget2D`'s use of it) and confirmed it is real, present, and matches
both the test's own expectations and `plans/plan_graphics.md` row 910's documented `git stash`
revert-and-rebuild verification. One minor test-coverage gap was found (F1: the priming check omits a
blue-channel bound), which does not affect the test's actual bug-catching power for the real defect
under test.

## Checklist Results

### API / XNA / FNA parity
`RenderTarget2D(device, kRTSize, kRTSize)` (line 74-75, using the 3-argument constructor confirmed to
exist at `RenderTarget2D.hpp:28`), `GraphicsDevice.SetRenderTarget(RenderTarget2D*)` (used throughout,
matching FNA's real overload accepting a single render target or `nullptr` to restore the backbuffer)
— both genuine XNA-facing API surface, used correctly. N/A beyond this: the specific view-id bug is a
CNA/Bgfx-internal concern with no XNA-facing behavioral contract of its own (XNA/FNA don't expose or
depend on any concept resembling a bgfx "view").

### Behavioral correctness
Traced the fix directly: `BgfxRenderTargetBackend`'s constructor (`BgfxGraphicsBackend.cpp:668-670`)
calls `Detail::AllocateRtViewId()` and stores the result in `viewId_` for the RT's entire lifetime;
its destructor (line 712) releases it back to the free list. `SetRenderTarget2D()` (lines 740-761)
sets `currentViewId_ = bgfxRt->viewId_` — i.e., each RT is bound to its **own**, distinct bgfx view,
not a shared hardcoded id. Confirmed this directly falsifies the pre-fix bug this test's header
comment describes (lines 6-9: *"Every render target previously shared ONE hardcoded view id (1)…"*)
by reading the current, non-hardcoded allocation code — the described root cause and its fix are both
real and consistent with the current source, not just plausible-sounding narrative.

### Logic
Re-traced the exact frame-boundary sequencing the test relies on: `Present()`
(`BgfxGraphicsBackend.cpp:1391-1395`) calls `bgfx::frame()` and is invoked once per `Draw()` return by
the game loop; separately, `ReadBackbuffer()` (lines 303-325) *also* triggers its own `bgfx::frame()`
internally via `bgfx::requestScreenShot()+bgfx::frame()` as part of the screenshot mechanism. This
means: (1) `step_==0`'s priming clear is flushed by the loop's own end-of-`Draw()` `Present()`; (2)
`step_==1`'s `ReadOnePixel(*rtA_)` call performs its *own* internal frame flush via `ReadBackbuffer`,
reading back the primed content; (3) still within that *same* `step_==1` `Draw()` invocation, **after**
that internal flush, the real test payload (bind+clear A→red, bind+clear B→blue, unbind) is queued
with no further `bgfx::frame()` call in between — satisfying the "no frame boundary between the two
fills" requirement the test's own header comment describes; (4) the loop's own `Present()` at the end
of that `Draw()` call flushes both fills together in one frame; (5) `step_==2`/`step_==3`'s
`ReadOnePixel()` calls each perform their own independent flush-and-read of the now-settled content.
This sequencing was independently reconstructed from the backend source (not merely assumed from the
comment) and is internally consistent with the fix being genuinely exercised as claimed.

### C++ correctness
No dangling references: `rtA_`/`rtB_`/`sb_` are `std::unique_ptr` members constructed in
`Initialize()` (lines 69-76) and outlive the whole `Draw()` state machine; `ReadOnePixel()` takes
`RenderTarget2D&` by reference, always to a still-live member. No move/copy of `RenderTarget2D` is
attempted (it correctly has a deleted copy constructor per `RenderTarget2D.hpp:65`, not exercised
here anyway).

### Robustness
The `done_` guard and monotonic `step_` counter (0→1→2→3) correctly prevent re-entrant re-execution
of any given step if `Draw()` is somehow called again after `Exit()` is requested — though `Exit()`
is only called once, at the very end (line 135), consistent with every other file in this batch's
`done_`-guard convention.

### Testing
This is one of the more structurally sophisticated tests in this shard (a genuine 4-step state
machine spanning multiple real frames, rather than a single-frame pixel check), and it is the only
file in this batch that exercises a Bgfx-specific implementation detail (view-id allocation) with no
FNA-facing behavioral equivalent — appropriately reflected in this report's Purpose/parity sections
above.

## Detailed Findings

### F1 — The priming-baseline check omits a blue-channel bound, so a hypothetical cyan-tinted priming bug would be misreported as PASS

- Severity: LOW
- Confidence: HIGH (directly visible in the assertion; confirmed no other check compensates for it)
- Category: test-coverage
- Location/symbol: `primeOk_ = (primed.getGProperty() >= 200 && primed.getRProperty() <= 40);`
  (line 96)
- Evidence: the two "real" checks in this same file, `aOk_` (line 114:
  `a.getRProperty() >= 200 && a.getGProperty() <= 40 && a.getBProperty() <= 40`) and `bOk_` (line
  122: `b.getBProperty() >= 200 && b.getRProperty() <= 40 && b.getGProperty() <= 40`), both correctly
  bound all 3 color channels. `primeOk_` alone checks only G (must be high) and R (must be low),
  leaving B completely unconstrained — a primed result of, e.g., `(0,255,255)` (cyan) would still
  report `[PASS] RT_A primed to a known green baseline`.
- Why it matters: this is purely a weakening of the *sanity-check* step (confirming the test's own
  setup reached a known-good starting state before the real bug-under-test is exercised), not of the
  actual discriminating logic — `aOk_`/`bOk_` (the checks that actually detect Task 910's bug) are
  fully rigorous. A false-positive `primeOk_` could only mask a *different*, unrelated priming-path
  bug (e.g., some other test/production code accidentally leaving blue channel content in the RT
  before this test's own clear takes effect) — it does not weaken this file's actual coverage of the
  concurrent-render-target defect it is named for.
- FNA/XNA comparison: N/A — internal test-assertion completeness, not an XNA/FNA behavior question.
- Related files: none — self-contained to this file.
- Suggested future action (not implemented by this audit): add `&& primed.getBProperty() <= 40` to
  the `primeOk_` expression at line 96, mirroring `aOk_`/`bOk_`'s existing 3-channel pattern.

## Cross-File Observations

- This file's `Detail::AllocateRtViewId()`/`ReleaseRtViewId()` free-list design (traced above) is
  shared, backend-wide plumbing also relied upon by every other Bgfx render-target test in the full
  suite (not limited to this batch) — this test is the one that specifically targets the *concurrency*
  aspect (two RTs live and filled within one un-flushed frame), as opposed to sequential-use tests
  elsewhere that would not have exercised the pre-fix bug at all.
- Unlike every other file in this batch, this file does not draw any triangle primitive directly (it
  only clears render targets and uses `SpriteBatch` to sample them back) — correctly has no
  `RasterizerState::CullNone`/winding concern, and does not need one.

## Missing or Weak Tests

See F1 (minor). No other coverage gaps identified for this file's stated, narrow scope (proving two
concurrently-bound-and-filled render targets within one frame do not clobber each other).

## Positive Findings

- The 4-step `Draw()`-call state machine, and specifically the choice to perform the priming read via
  its own separate frame *before* the real concurrent-fill scenario, is a well-reasoned test-harness
  design that correctly avoids conflating "did the RT ever hold the right content at all" with "does
  concurrent binding within one frame corrupt it" — independently confirmed both by reading the code
  and by tracing the actual frame-boundary sequencing (see Logic section above).
- The header comment's stated root cause (`bgfx::setViewFrameBuffer` being a per-view-per-*frame*,
  not per-submit, setting) was independently verified against the actual fix
  (`Detail::AllocateRtViewId`) and against `plans/plan_graphics.md` row 910's own description — both
  consistent, not just asserted.
- `[INFO]` diagnostic (lines 128-131) correctly and specifically names the exact regression class a
  failure would indicate.

## Final Assessment

A well-engineered, structurally sound regression test for a genuine, previously-real Bgfx concurrency
bug, with production-fix cross-references independently confirmed accurate. One trivial,
non-load-bearing test-coverage gap (F1) in the priming sanity check only.
