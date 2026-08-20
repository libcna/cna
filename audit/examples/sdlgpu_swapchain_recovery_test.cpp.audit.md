# Audit: examples/sdlgpu_swapchain_recovery_test.cpp

## Metadata

- Source file: `examples/sdlgpu_swapchain_recovery_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlgpu` shard — hard swapchain-acquisition-failure recovery proof
  (plans/plan_sdlgpu.md SDLGPU-11)
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_sdlgpu_test(cna_test_sdlgpu_swapchain_recovery …)` /
  `cna_register_backend_test(NAME SdlGpu_SwapchainRecovery …)`,
  `cmake/Tests/SdlGpuTests.cmake:116-119`, `TIMEOUT 60`).
- XNA/FNA relevance: indirect — `GraphicsDevice.Present()`/device-loss recovery is an XNA-level
  concept (`GraphicsDevice.DeviceReset`/`DeviceLost` events on real XNA), but this file exercises
  the CNA-internal `SdlGpuGraphicsBackend::Present()`/`EnsureFrameRendered()` machinery directly
  rather than any public XNA device-loss event.
- Related production code: `src/CNA/Internal/Backends/SdlGpu/SdlGpuGraphicsBackend.cpp`
  (`EnsureFrameRendered()` lines 646-785, `Present()` lines 1008-1013, `Clear()` lines 914-932).

## Purpose

Five-check proof that the hard swapchain-acquisition-failure path (`SDL_
WaitAndAcquireGPUSwapchainTexture` returning `false`) is genuinely recoverable, not merely "throws
and leaves the backend in an unknown state" — the row's own plan-doc Notes column is quoted as
having flagged this as "not yet proven recoverable in practice." Uses a real, controlled way to
force the exact failure (`SDL_ReleaseWindowFromGPUDevice` un-claims the window, so the next
acquisition genuinely fails), rather than trying to simulate a driver-loss event. Check A: 9 normal
frames render with no exception. Check B: releasing the window then forcing `Present()` throws.
Check C: `SDL_ClaimWindowForGPUDevice` re-claims successfully. Check D: a subsequent `Present()`
succeeds with no exception. Check E: 30 further frames after recovery continue to render with no
exception.

## Executive Verdict

**Mostly healthy.** The central recoverability claim is real and was independently confirmed by
tracing `EnsureFrameRendered()`'s exact failure/retry code path against the current production
source — this is not a stale or aspirational claim. The one gap (F1) is that the test's own runtime
assertions only prove "no exception was thrown," not the stronger claim the header comment makes
about the queued frame's content surviving the failed attempt — that stronger claim is true (per
source-code tracing, not per this test's own checks), so the file's central conclusion holds, but a
future regression in the specific "content preserved across retry" property would not be caught by
this test as written.

## Checklist Results

### API / XNA / FNA parity

N/A directly — no single named XNA API is under test; the underlying contract (`Present()` fails
loudly on a genuine device/surface problem, and recovers cleanly once the problem is resolved) is an
implicit expectation any backend must satisfy for `GraphicsDevice.Present()` to be XNA-plausible.

### Behavioral correctness

- Independently traced `EnsureFrameRendered()` (`SdlGpuGraphicsBackend.cpp:646-785`), which
  `Present()` (line 1008-1013) delegates to unconditionally: it acquires a command buffer (line
  651), then calls `SDL_WaitAndAcquireGPUSwapchainTexture` (line 658-659). On failure (`!acquired`,
  line 660), it submits the (now-empty) command buffer per the SDL_gpu contract comment ("it is an
  error to cancel a command buffer once `SDL_WaitAndAcquireGPUSwapchainTexture` has been called on
  it -- must always submit") and **throws before reaching `framePending_ = false;`** (line 783,
  reached only at the very end of the success path). Since `Clear()` (lines 914-932, called at the
  top of every `Draw()` in this test) unconditionally sets `framePending_ = true;` and (for the
  default no-render-target case) `clearColorPending_ = true;`/`clearColor_`, and neither is ever
  reset on the failure branch, the queued clear from frame 10 genuinely survives the failed
  `Present()` attempt exactly as the header comment (lines 19-21) claims — confirmed by reading the
  code, not merely by the test's own "no exception" observation (see F1).
- Confirmed the failure point (line 660) is reached **before** any of `UploadSpriteVertexData`/
  `UploadSceneDrawData`/per-render-target `RenderToTarget`/`RenderToTargetCubeFace` processing
  (lines 683-699) — i.e. the failed attempt consumes none of the frame's queued draw/clear state, so
  the retry on the next `Present()` call genuinely starts from the exact same queued state, not a
  partially-consumed one.
- Check B's induced failure is real, not simulated: `SDL_ReleaseWindowFromGPUDevice` (line 85) is a
  real SDL_gpu API un-claiming the window from the device, so the subsequent
  `SDL_WaitAndAcquireGPUSwapchainTexture` call genuinely fails with a real `SDL_GetError()` message
  — this is a materially stronger proof than a test that merely asserts the exception-throwing code
  exists by inspection.
- Check D/E: after `SDL_ClaimWindowForGPUDevice` succeeds (line 100), the next `Present()` call
  re-enters `EnsureFrameRendered()` with `framePending_` still `true` from frame 10's `Clear()` —
  this genuinely re-attempts (not skips) the same queued frame, and since the window is reclaimed,
  acquisition now succeeds and the frame renders normally. Confirmed no stale/leaked state from the
  first (failed) command buffer — it was already fully submitted (line 665) before the throw, so
  there is nothing left dangling for the retry to conflict with.
- After the manual `dev.Present()` calls inside `Draw()` at frame 10 succeed, `framePending_` becomes
  `false` again (line 783); the automatic post-`Draw()` `Present()` that `Game::Tick()`/`EndDraw()`
  still issues afterward correctly no-ops (`if (!framePending_) return true;`, line 648-649) rather
  than double-submitting a second, empty frame.

### Logic

`kInduceFailureFrame = 10`, `kTotalFrames = 40` (lines 48-49) — Check A's label ("frames 1..9
rendered with no exception") and Check E's label ("30 further frames…", `kTotalFrames -
kInduceFailureFrame = 30`) are both arithmetically correct and match what the test structure
actually exercises (frames 11-40 inclusive = 30 frames).

### C++ correctness

The `try`/`catch (const std::exception&)` pairs (lines 87-97, 103-112) correctly scope the induced
failure to a local, expected exception rather than letting it propagate and terminate the process —
matching this test's own explicit purpose (proving the throw is catchable and recoverable, not
merely that it occurs). The second `catch` block (lines 108-112) also prints `e.what()` on an
*unexpected* recovery failure, which is good diagnostic hygiene distinguishing "recovery genuinely
failed" from "recovery succeeded" at the log level.

### Robustness

Correctly distinguishes the "hard acquisition failure" path this file targets from the *separate*,
already-documented non-error case (`swapchainTexture == nullptr`, e.g. a minimized window,
`EnsureFrameRendered()` lines 669-674) — the header comment (lines 6-8) explicitly calls this
distinction out, and the induced-failure mechanism (`SDL_ReleaseWindowFromGPUDevice`) genuinely
reaches the `!acquired` branch (line 660), not the null-texture branch, since an un-claimed window
makes the *acquisition call itself* fail rather than merely returning a null texture for a claimed
window.

### Testing

Strong for what it explicitly sets out to prove (the failure is real and reachable; recovery does
not leave the backend in a broken, un-recoverable state). See F1 for the one claim the test asserts
in its own header comment but does not itself verify at the pixel/content level.

## Detailed Findings

### F1 — The header comment's claim that "the same frame's queued Clear() is not lost" is established by source review, not by anything this test's own assertions check

- Severity: MEDIUM
- Confidence: MEDIUM (the claim is independently confirmed *true* by this audit's own source
  tracing — see Behavioral correctness above — so this is a test-coverage gap, not evidence of an
  actual bug; the concern is specifically about what a *future* regression would or would not be
  caught by)
- Category: test-coverage / correctness-of-test
- Location/symbol: header comment lines 19-21 ("Check D -- … framePending_ correctly stays set
  across the failed attempt, so the same frame's queued Clear() is not lost"); the corresponding
  runtime check (lines 103-114, `Check(!threwAfterRecovery, "Present() succeeds again after
  re-claiming the window…")`).
- Evidence: the only runtime signal Check D observes is the *absence* of a thrown exception on the
  retried `Present()` call. It never reads back a pixel (this backend does have a real,
  already-proven `RenderTarget2D::GetData()` readback path, per
  `sdlgpu_rendertargetcube_test.cpp.audit.md`'s and `sdlgpu_draworder_test.cpp.audit.md`'s own
  findings elsewhere in this shard — but this test targets the swapchain surface directly, which has
  no equivalent readback API on this backend) to confirm the frame-10 `Color::CornflowerBlue` clear
  was actually the color that ended up presented, as opposed to, say, a hypothetical regression that
  reset `clearColorPending_`/`framePending_` to `false` somewhere on the failure branch (which would
  make the *retried* `Present()` silently skip rendering that frame's clear entirely while still not
  throwing — indistinguishable from correct behavior by this test's own checks).
- Why it matters: the test's *stated* goal (per its own header, and per the "recovers cleanly, not
  left in a broken state" framing that IS actually verified by "no exception + subsequent frames
  keep working") is satisfied. But the *stronger*, more specific claim about `framePending_`/queued
  clear-content preservation across the retry is a claim about internal implementation state that
  this test cannot observe and is not actually asserting — it is true today (confirmed via this
  audit's own reading of `EnsureFrameRendered()`), but a regression that broke specifically that
  property while still avoiding an exception on retry would pass this test unnoticed.
- FNA/XNA comparison: N/A — internal recovery-semantics test-design question, not an XNA/FNA
  behavioral question.
- Related files: none — no production change implied; this is purely about test strength.
- Suggested future action (not implemented by this audit): if a way to read back the swapchain
  surface itself is ever added to this backend (or by rendering into an offscreen
  `RenderTarget2D`-mirrored scene instead of directly to the swapchain for this specific test), add
  an assertion that the post-recovery frame actually shows `Color::CornflowerBlue`, not merely that
  no exception was thrown.

## Cross-File Observations

- This file's approach (force a *real* SDL-level failure via `SDL_ReleaseWindowFromGPUDevice` rather
  than mocking/stubbing) is a stronger validation technique than a unit test could achieve without a
  real GPU/window — worth calling out as a positive pattern relative to the shard's median.
- Confirms one specific item from `AUDIT_CROSS_CUTTING_FINDINGS.md`'s "mutate-before-validate"
  watch-list does **not** apply here: `Clear()`'s `framePending_ = true;` (line 931) is set
  *unconditionally* before any backend call that could reject it, which sounds superficially like the
  documented "optimistic mutation" anti-pattern (`SpriteBatch::Begin()`, `GraphicsDevice::
  SetRenderTargets`) — but here it is exactly the *correct* behavior this file is proving: the whole
  point is that `framePending_` must survive an unrelated *later* failure (the swapchain acquisition,
  which is decoupled from `Clear()` itself and only happens at `Present()`/`EnsureFrameRendered()`
  time), not that `Clear()` itself can fail and needs to roll back. Worth noting so a future
  synthesis pass does not mis-classify this as the same anti-pattern.
- Confirmed the project-wide `SKIP_RETURN_CODE 77` CTest property (`cmake/UnitTests.cmake:269`)
  applies to this test's registration even though `cmake/Tests/SdlGpuTests.cmake`'s own
  `cna_register_backend_test` call for it does not set `SKIP_REGULAR_EXPRESSION`/`SKIP_RETURN_CODE`
  directly — not a per-file gap.

## Missing or Weak Tests

- See F1 — no pixel-level confirmation that the recovered frame actually presented the expected
  clear color, only that no exception occurred.
- No check exercises a *second* induced failure later in the same run (i.e., fail → recover → fail
  again → recover again) — a reasonable robustness case for a "recovers cleanly" claim, though not
  strictly necessary to prove the specific SDLGPU-11 gap this file targets.

## Positive Findings

- The core recoverability claim was independently verified true against the current production
  source, not merely trusted from the header comment — `EnsureFrameRendered()`'s failure branch
  genuinely preserves all queued frame state (draws, clears) across a failed acquisition attempt.
- Uses a real, controlled SDL_gpu API sequence to force the exact hard-failure path rather than a
  simulated/mocked error — a materially stronger proof technique.
- Correctly distinguishes the hard-failure path from the separate documented "minimized window"
  non-error case, and the two `try`/`catch` blocks are scoped precisely to the induced failure and
  the recovery attempt respectively.

## Final Assessment

A well-targeted, accurate recovery test whose central engineering claim was independently confirmed
against current source. The one gap (F1) is a test-strength question (no pixel-level proof that
recovered content is correct, only that no exception occurs) rather than an indication that the
recovery itself is broken or the header comment is wrong.
