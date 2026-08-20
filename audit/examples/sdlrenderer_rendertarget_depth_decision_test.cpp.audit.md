# Audit: examples/sdlrenderer_rendertarget_depth_decision_test.cpp

## Metadata

- Source file: `examples/sdlrenderer_rendertarget_depth_decision_test.cpp` (140 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlrenderer` shard — render-target depth-buffer design-decision test
- Build/CTest registration: `cna_sdl_test(cna_test_sdl_rendertarget_depth_decision …)` /
  `cna_register_backend_test(NAME SDL_Renderer_RenderTarget_DepthDecision …)`,
  `cmake/Tests/SdlRendererTests.cmake:277-281`. Header traces to Task 708 (confirmed live: `git log` shows
  `ce15e028 fix(Task 708): render-target depth-buffer decision + fix stale bind crash`).
- XNA/FNA relevance: `RenderTarget2D.DepthStencilFormat` (`Microsoft::Xna::Framework::Graphics`),
  `GraphicsDevice.SetRenderTarget`'s auto-clear-on-bind semantics.
- Related production code: `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp`
  (`SetRenderTarget(RenderTarget2D*)` lines 1821-1859, `Clear(ClearOptions, ...)` lines 284-335),
  `include/CNA/Internal/Backends/SdlRenderer/SdlGraphicsBackend.hpp`
  (`SdlRenderTargetBackend::HasRealDepthBuffer`, unconditional `return false`, line 50).

## Purpose

Documents and tests a deliberate design decision: SDL_Renderer's render targets silently ignore any requested
`DepthFormat` rather than throwing, because this backend's 2D sprite pipeline never performs real depth testing
under any circumstance. The test proves three things: (1) constructing a `RenderTarget2D` with
`DepthFormat::Depth24Stencil8` must not throw and must echo the format back verbatim; (2) binding it (default
`DiscardContents` usage, which triggers an auto-clear-on-bind) must not throw despite the "requested" depth
format; (3) an explicit depth-buffer clear request on the (unbound, real) backbuffer must still throw — ignoring
a *render target's* requested `DepthFormat` must not accidentally unlock real depth functionality this 2D-only
backend never has.

## Executive Verdict

**Needs attention.** The header comment's narrative about the Task 708 bug/fix
(`GraphicsDevice::SetRenderTarget`'s auto-clear-on-bind previously checked the XNA-level *requested*
`DepthStencilFormat` rather than asking the backend, crashing on bind) is accurate and independently verified.
However, the file's own **fourth and final check no longer tests what it claims**: a later commit
(`90f5db2c`/`41b36c67`, 2026-07-13, five days after this file was authored) deliberately changed
`GraphicsDevice::Clear(ClearOptions, ...)` to mask `DepthBuffer`/`Stencil` out of *any* clear request whenever
the active target has no real depth-stencil support — including the bare backbuffer on SDL_Renderer, exactly
the scenario this test's last check exercises. This means `dev.Clear(ClearOptions::Target |
ClearOptions::DepthBuffer, ...)` (line 111) now silently degrades to a color-only clear instead of throwing,
directly contradicting `check(clearDepthThrew, ...)` (line 117). This is **not a new discovery** — the project's
own `NEXT.md`/`plans/plan_graphics.md` (Task 1113, still `⬜` open) already documents this exact regression, including
that `SDL_Renderer_RenderTarget_DepthDecision` (this file's own registered CTest name) currently fails — see F1.

## Checklist Results

### API / XNA / FNA parity
`RenderTarget2D(dev, 8, 8, false, SurfaceFormat::Color, DepthFormat::Depth24Stencil8)` (line 81) matches FNA's
own 5+-arg `RenderTarget2D` constructor shape. The assertion that `DepthStencilFormat` "echoes the requested
value verbatim" (line 82-83) is correct per FNA's own plain field-store semantics for this property (no
`FNA3D_GetMaxMultiSampleCount`-style clamping exists for `DepthFormat` in FNA either) — independently confirmed
against `RenderTarget2D.cpp` line 59 (`depthFormat_(preferredDepthFormat)`, no transformation applied).

### Behavioral correctness
Traced the exact fix this test guards: `GraphicsDevice::SetRenderTarget(RenderTarget2D*)` (lines 1821-1859)
computes `depthFormatRequested = renderTarget->getDepthStencilFormatProperty() != DepthFormat::None` and then
asks `rtBackend->HasRealDepthBuffer(depthFormatRequested)` (lines 1852-1855) — since
`SdlRenderTargetBackend::HasRealDepthBuffer` unconditionally returns `false` regardless of its argument (header
line 50), the resulting `Clear(...)` call at line 1856 always requests only `ClearOptions::Target`, never
`ClearOptions::DepthBuffer`, on this backend — matching the test's expectation that binding does not throw
(line 98) even though a real depth format was requested. This is the actual, current mechanism, not merely
described secondhand. The fourth check, however, is now stale — see F1.

### Logic
The third/fourth check (lines 108-118) is the most interesting part of this test: it deliberately calls
`dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer, ...)` on the **unbound backbuffer** (after
`SetRenderTarget(nullptr)`, line 102), not on the render target — intending to verify the *decision boundary*
itself: RT construction/binding tolerates an inert depth-format request, but actually *asking* for a depth clear
should still throw. Traced against the **current** `GraphicsDevice::Clear(ClearOptions, ...)`
(lines 284-335): with no render target bound (`currentRenderTargets_.empty()`),
`hasRealDepthBuffer = backend_->SupportsDepthStencil()` (line 317) — and `SdlGraphicsBackend::SupportsDepthStencil()`
returns `false` (header line 145) — so `options &= ClearOptions::Target` (line 322) strips `DepthBuffer` out
**before** the dispatch, and `backend_->Clear(r,g,b,a)` (line 461 in `SdlGraphicsBackend.cpp`) runs a plain,
non-throwing color clear. The test's own expectation (`check(clearDepthThrew, ...)`, line 117) is therefore
**no longer met by the current code** — confirmed as a known, tracked regression, not a hypothesis; see F1.

### Memory/resource lifetime
`RenderTarget2D rt` (line 81) is a local stack object bound and then unbound within the same function; standard
RAII, no issue.

### C++ correctness
No unsafe casts; `std::string bindError`/exception capture (lines 88-99) is a standard, correct
try/catch-and-record pattern used consistently across this shard.

### Performance
N/A — single-frame test.

### Thread safety
N/A.

### Architecture
The three-way split (construct → bind → attempt-real-depth-op) is a well-designed way to test a decision
*boundary* rather than just one side of it — testing only "binding doesn't throw" would leave the "but real
depth ops still correctly throw" half of the decision completely unverified.

### Maintainability
140 lines; proportionate; the header comment's design-rationale explanation is unusually thorough (a positive,
matching the pattern already noted as a project strength in the sibling `SdlGraphicsBackend.cpp` audit).

### Portability
N/A — SDL_Renderer-specific, CMake-gated.

### Robustness
See F1 — the final check's *intent* (prove the decision boundary) is sound engineering, but a later, unrelated
fix silently changed the boundary it was verifying, and this test was never updated to match.

### Testing
This file is the dedicated test for the Task 708 depth-format decision. Its first three checks are solidly
verified against production code and currently pass; its fourth check is currently failing per the project's own
tracked regression list (F1), meaning this file's CTest registration (`SDL_Renderer_RenderTarget_DepthDecision`)
is presently a **known-red** test in the full suite, not a clean pass.

### Cross-file consistency
The `HasRealDepthBuffer(bool depthFormatWasRequested)` mechanism this test's first two checks rely on is shared,
backend-agnostic code in `GraphicsDevice.cpp`, exercised identically by
`sdlrenderer_rendertarget_usage_test.cpp`'s own auto-clear-on-rebind checks (same shard) — consistent usage
across both files. F1's regression stems from a *different*, later commit that touched the same function for an
unrelated reason.

## Detailed Findings

### F1 — Fourth check ("depth-buffer clear on the backbuffer still throws") is stale and currently fails: a later commit deliberately made `Clear(ClearOptions,...)` degrade gracefully instead of throwing, and this test was never updated — a confirmed, already project-tracked regression (Task 1113)

- Severity: HIGH
- Confidence: HIGH (confirmed via full code trace, `git log` commit dating, and the project's own tracked
  known-bug list independently corroborating the same conclusion)
- Category: test-coverage / correctness-of-test / stale-assertion
- Location/symbol: `check(clearDepthThrew, ...)` (lines 108-118); `GraphicsDevice::Clear(ClearOptions, const
  Color&, float, int)` masking logic (`GraphicsDevice.cpp` lines 296-323); `SdlGraphicsBackend::Clear(float,
  float, float, float)` (`SdlGraphicsBackend.cpp` lines 461-472)
- Evidence (chain, fully traced):
  1. This test file was authored for Task 708 (commit `ce15e028`, 2026-07-08), whose own commit message states
     "All 4 pass" for exactly this test at that time.
  2. Five days later, commit `90f5db2c`/`41b36c67` ("fix(GraphicsDevice): Clear(const Color&) no longer crashes
     on SDL_RENDERER", 2026-07-13) added the masking block now at `GraphicsDevice.cpp` lines 296-323: when no
     render target is bound, `hasRealDepthBuffer = backend_->SupportsDepthStencil()` — `false` for
     `SdlGraphicsBackend` — causing `options &= ClearOptions::Target` to strip `DepthBuffer`/`Stencil` out of
     *any* `Clear(ClearOptions, ...)` call against the bare backbuffer on this backend, "degrading to a
     color-only clear instead of forwarding a request the backend cannot honor" (that commit's own stated
     intent).
  3. With `DepthBuffer` masked out, the resulting dispatch reaches `backend_->Clear(r,g,b,a)`
     (`SdlGraphicsBackend.cpp` line 461), which only throws on a genuine SDL API failure
     (`SDL_SetRenderDrawColor`/`SDL_RenderClear` returning false) — not under normal test execution.
  4. Therefore `dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer, Color(0,0,0,255), 1.0f, 0)`
     (line 111, called on the unbound backbuffer) no longer throws, so `clearDepthThrew` stays `false` and
     `check(clearDepthThrew, ...)` (line 117) fails.
  5. **Independent corroboration, not just this audit's own trace**: `NEXT.md` (line 75) and `plans/plan_graphics.md`
     (line 1021, Task 1113, status `⬜` open) already document this exact regression by name: *"`SDL_Renderer_
     RenderTarget_DepthDecision` ... fail[s] ... `GraphicsDevice.cpp`'s own masking (`options &= ClearOptions::
     Target` when `!hasRealDepthBuffer`) strips the `DepthBuffer`/`Stencil` bits out first, so `Clear()` returns
     having called nothing on the backend at all — no throw ever happens. **Confirmed pre-existing** ... via
     `git stash` against the unmodified baseline."` This confirms the test is *currently, actually* failing in
     this project's own CTest runs, not merely theoretically contradicted by this audit's static trace.
- Why it matters: this is exactly the kind of stale/incorrect-assertion issue this audit was asked to hunt for —
  a numeric/behavioral claim in a test (and in its own header comment, lines 1-34, which still describes the
  old, no-longer-true decision boundary as current fact) that a later, unrelated commit silently invalidated.
  The test file's header comment was **not** updated when the Task-1113-introducing commit changed the very
  behavior this file's fourth check depends on — meaning anyone reading this file's header today (without
  independently checking `NEXT.md`/`git log`, as this audit did) would be misled into believing SDL_Renderer
  still throws on an explicit backbuffer depth-clear request, when it currently does not. Rated HIGH rather than
  MEDIUM because this is a live, currently-red CTest case in a project whose own CLAUDE.md mandates "every task
  is complete, not partial" — a known-failing, unfixed test sitting in the registered suite understates the
  project's actual current SDL_Renderer pass rate for anyone trusting a green/total CTest summary without
  reading the known-failures list.
- FNA/XNA comparison: N/A directly — this is a CNA-internal backend-decision test, not a stock XNA behavior
  question. (Task 1113's own text notes the broader masking question — "no depth buffer, silently degrade" vs.
  "caller explicitly asked for something impossible, should surface that" — should be resolved by checking real
  FNA/XNA `GraphicsDevice.Clear(ClearOptions,...)` behavior, which this audit did not independently re-derive.)
- Related files: `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp` (the masking logic itself, shared by
  every backend without real depth support, not SDL_Renderer-specific — Task 1113's own scope note says this
  affects "EVERY backend without a real depth buffer, not just SDL_Renderer"); `examples/
  sdlrenderer_clearoptions_audit_test.cpp` (the sibling test Task 1113 names as *also* affected, out of this
  batch's 8 files); `NEXT.md` line 75, `plans/plan_graphics.md` line 1021 (Task 1113, still open).
- Suggested future action (not implemented by this audit, and already tracked as Task 1113 for whoever picks it
  up): resolve the design question Task 1113 poses (should `SDL_Renderer_RenderTarget_DepthDecision`'s fourth
  check be updated to expect a graceful no-op, mirroring how DX3's `examples/dx3_no3d_test.cpp` Check D already
  tests unreachable-through-the-public-API backend methods directly instead; or should `Clear()`'s masking
  itself be tightened to distinguish "harmless degrade" from "caller explicitly asked for the impossible") —
  then update this file's fourth check and its header comment together, so the comment no longer describes
  stale behavior as current fact.

## Cross-File Observations

- Shares the `HasRealDepthBuffer` construction-time mechanism with `sdlrenderer_rendertarget_usage_test.cpp` —
  that file's own checks were independently confirmed unaffected by F1 (they never call `Clear(ClearOptions,
  ...)` directly with a depth flag on the bare backbuffer), so F1 is scoped to this file (and, per Task 1113,
  `sdlrenderer_clearoptions_audit_test.cpp`) specifically, not the whole shard.
- This is the second stale-assertion-style finding pattern this audit family has now surfaced (the first being
  the EasyGL specular test's stale constant in a prior batch) — except here the staleness is already
  self-documented in the project's own tracking, which is a healthier outcome than an undetected drift, but
  still means this specific `.cpp` file's header comment (not just `NEXT.md`) should have been updated when Task
  1113's regression was found and logged.

## Missing or Weak Tests

F1 — the fourth check needs either its own assertion updated to match the current, intentional masking
behavior, or `GraphicsDevice::Clear`'s masking needs to be revisited per Task 1113's own open design question.
Until one of those happens, this file's CTest registration is a known-red case in the suite.

## Positive Findings

- The three-way construct/bind/attempt-real-op split is a well-designed way to test a decision boundary rather
  than just one side of it — the test *design* is sound; only its fourth assertion has drifted from current
  behavior.
- First three checks (construction echo, bind-does-not-throw, clean draw/unbind) were fully traced and confirmed
  correct against current production code.
- Header comment's design rationale and Task 708 bug-fix narrative are detailed and accurate for everything
  except the now-stale fourth-check boundary claim.

## Final Assessment

**Needs attention.** Three of four checks are solid and pass. The fourth is a confirmed, currently-failing
assertion — independently traced by this audit and corroborated by the project's own `NEXT.md`/`plans/plan_graphics.md`
Task 1113 (still open) — caused by a later, unrelated commit that intentionally changed the exact behavior this
check depends on, without this test file (or its header comment) being updated to match. Not a new discovery,
but confirmed as real and still open as of this audit pass.
