# Audit: examples/viewport_reset_after_resize_test.cpp

## Metadata

- Source file: `examples/viewport_reset_after_resize_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-generic` shard — explicitly and correctly documented by the project's
  own `BgfxTests.cmake` comment as "100% backend-agnostic... Exercises only
  `GraphicsDevice`/`GraphicsDeviceManager` C++ state..., no pixel readback, no backend-specific
  code at all." Registered against **three** backends from the identical shared source:
  `cmake/Tests/EasyGLTests.cmake:1017-1020` (`EasyGL_ViewportResetAfterResize`),
  `cmake/Tests/VulkanTests.cmake:467-470` (`Vulkan_ViewportResetAfterResize`),
  `cmake/Tests/BgfxTests.cmake:155-158` (`Bgfx_ViewportResetAfterResize`).
- XNA/FNA relevance: direct — `GraphicsDevice.Viewport` reset semantics around
  `GraphicsDeviceManager.ApplyChanges()`/backbuffer resize, and (as CNA-specific behavior beyond
  FNA's own model) `GraphicsDevice.Present()`'s viewport-preserving guarantee.
- FNA reference: `GraphicsDeviceManager.cs`/`GraphicsDevice.cs` (`Reset()` unconditionally resets
  `Viewport` to the new `PresentationParameters` size; `Present()` never touches `Viewport`).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp`
  (`UpdateViewportFromWindow()` lines 1513-1561, `Present()` lines 372-381),
  `src/Microsoft/Xna/Framework/GraphicsDeviceManager.cpp` (`ApplyChanges()` lines 206-246,
  `applyToExistingBackend()` lines 551-585).

## Purpose

Proves two FNA-matching behaviors around a genuine backbuffer resize: (1) a custom sub-region
`Viewport` (e.g. for split-screen) survives a frame's `Present()` when no resize occurs — matching
FNA's `Present()` never touching `Viewport` — and (2) that same custom `Viewport` **is** eventually
reset to `(0,0,newW,newH)` once `GraphicsDeviceManager::ApplyChanges()` genuinely changes the
backbuffer size — matching FNA's `Reset()` semantics. The header comment documents two prior
findings this test guards against: a bug where `UpdateViewportFromWindow()` compared the new size
against `viewport_`'s own current dimensions (silently stomping any custom viewport every frame
even with no resize), and a separate, inherent async-resize race on X11/Xvfb (querying window size
immediately after `SDL_SetWindowSize()` with no event pump in between can observe a stale size).

## Executive Verdict

**Healthy** — both documented production claims were independently traced and confirmed correct in
`GraphicsDevice.cpp`/`GraphicsDeviceManager.cpp`, the polling-based test design correctly
accommodates the documented async-resize race without weakening what it actually proves, and the
file's "100% backend-agnostic" self-description (from the Bgfx registration's own comment) was
independently confirmed true — the file includes no backend-specific headers and touches no
rendering/pixel state.

## Checklist Results

### API / XNA / FNA parity
`GraphicsDeviceManager::setPreferredBackBufferWidthProperty`/`HeightProperty`/`ApplyChanges()`
(lines 94-96, 136-138) and `GraphicsDevice::getViewportProperty`/`setViewportProperty` (via
`Viewport` getters/setters throughout) map directly to FNA's equivalents.

### Behavioral correctness — independently traced both production claims
**Claim 1 (custom Viewport survives a no-resize `Present()`):** traced
`GraphicsDevice::UpdateViewportFromWindow()` (`GraphicsDevice.cpp:1513-1561`). The comparison at
line 1540 (`if (width == lastKnownViewportWidth_ && height == lastKnownViewportHeight_) return;`)
is explicitly against two dedicated tracking fields (`lastKnownViewportWidth_`/`Height_`), **not**
against `viewport_`'s own current width/height — confirmed this is exactly the Task 349 fix the
header comment describes, and the in-code comment at lines 1533-1539 states the identical
rationale in near-verbatim language to the test file's own header comment. This directly explains
why the test's step 2 (frame 1: no resize yet, custom Viewport still `(10,20,100,60)`) correctly
expects the custom Viewport to be untouched even though `Present()`
(`GraphicsDevice.cpp:372-381`) unconditionally calls `UpdateViewportFromWindow()` on every single
frame (line 380) — the "did anything change" gate, not the absence of the call, is what preserves
the custom viewport.

**Claim 2 (genuine resize does reset it):** traced `GraphicsDeviceManager::ApplyChanges()`
(`GraphicsDeviceManager.cpp:206-246`) → `applyToExistingBackend()` (lines 551-585) →
`graphicsDevice_->Reset(pp, adapter)` (line 582) → immediately `graphicsDevice_->
UpdateViewportFromWindow()` (line 584) with **no** `SDL_PumpEvents()` or event-loop turn anywhere
in that call chain — confirmed this matches the header comment's claim about the async-resize race
verbatim (the window-size query genuinely can observe a stale physical size on X11/Xvfb, since
`Reset()`'s own internal `applyPresentationParametersToWindow()` call, which performs the actual
`SDL_SetWindowSize()`, precedes this `UpdateViewportFromWindow()` call within the same call stack).

### Logic
The polling design (lines 144-168, `kMaxWaitFrames=300`) correctly does **not** assert
success/failure until *both* `physicalResizeApplied` (real SDL window size query) *and*
`viewportSettled` (the `Viewport` property) agree — avoiding a false negative from asserting too
early relative to the documented async race, and avoiding a false positive by never trusting the
`Viewport` value alone without also cross-checking the real OS-level window size. A genuine timeout
(`frame_ >= kMaxWaitFrames`) still fails the test explicitly (line 160-164) rather than silently
passing or hanging.

### C++ correctness
`reinterpret_cast<SDL_Window*>(getWindowProperty().getHandleProperty())` (line 146) is the
established cross-cutting-noted pattern for reaching the raw SDL handle from CNA's `Window`
wrapper — consistent with other files in this codebase that need direct SDL queries for
test-verification purposes only (not part of the public XNA API surface).

### Testing
Four `checkDim()` assertions in `finish()` (lines 88-91) precisely verify all four `Viewport`
fields (`X`, `Y`, `Width`, `Height`) independently rather than a single aggregate check, making a
future regression easy to localize to exactly which field broke. The test also restores the
original backbuffer size before exiting (lines 94-96) — good hygiene, leaving the process in a
clean state rather than exiting mid-resize.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings.

### F1 — Timeout path reports one generic failure message rather than diagnosing which of the two settle-conditions never converged
- Severity: LOW
- Confidence: HIGH
- Category: testing / diagnosability
- Location: lines 160-164 (`check(false, "Timed out waiting for the backbuffer resize to
  propagate to Viewport");`)
- Evidence: the timeout branch does not report whether `physicalResizeApplied` or
  `viewportSettled` (or both) were the ones still false, nor their actual last-observed values.
- Why it matters: minor — if this test ever timed out in a real CI run (e.g. due to an actual
  regression versus an environment-specific resize delay), the failure message alone wouldn't
  distinguish "the OS never resized the window" from "the window resized but `Viewport` never
  caught up," which are different root causes pointing at different code (SDL/window-manager vs.
  `UpdateViewportFromWindow()`).
- Suggested follow-up (not implemented by this audit): include the last-observed `physW`/`physH`/
  `vp.Width`/`vp.Height` values in the timeout failure message.

## Cross-File Observations

- This file, `rendertarget2d_depth_test.cpp`, and (implicitly, per its own header comment
  referencing "Task 348, and Game::EndDraw() which calls Present() after every Draw()") this
  batch's other files all rely on the same confirmed invariant: `Game::EndDraw()` calls
  `Present()` unconditionally after every `Draw()`, regardless of what the overridden `Draw()` body
  does — independently confirmed via `Game.cpp:449` (`EndDraw();` inside the frame-tick sequence)
  and `Game.cpp:493-501` (`EndDraw()` calling `getGraphicsDeviceProperty().Present()`
  unconditionally when a `graphicsDeviceManager_` doesn't intercept it). This underpins the
  validity of this test's frame-by-frame state machine (`frame_==0`/`==1`/polling), since each
  `Draw()` return is guaranteed to trigger exactly one `Present()`.
- Explicitly reuses the "poll across frames rather than assert immediately" precedent the header
  comment attributes to `examples/easygl_real_window_resize_test.cpp` (Task 348) — a consistent,
  deliberate methodology for this class of X11/Xvfb async-resize test, not a one-off improvisation.
- Genuinely one of the strongest examples in this batch of a file that is *actually* backend-agnostic
  rather than merely "doesn't happen to use backend-specific types" — it never issues a single draw
  call or pixel readback, only `GraphicsDevice`/`GraphicsDeviceManager`/raw SDL window-size queries,
  matching the Bgfx registration comment's explicit claim precisely.

## Missing or Weak Tests

- No test in this file (nor, as far as this batch covers, elsewhere) for a resize that **shrinks**
  the window below the custom `Viewport`'s own region (e.g. custom Viewport `(10,20,100,60)`
  resized down to a backbuffer smaller than `100×60`) — FNA's `Reset()` behavior here is simply "the
  Viewport is unconditionally replaced," so this isn't a gap in proving the reset itself, but a
  shrink-below-custom-region scenario would be a slightly more adversarial variant worth having
  somewhere in the broader `GraphicsDevice`/`Viewport` test coverage.

## Positive Findings

- Both of the header comment's specific production-code claims (the `lastKnownViewportWidth_`/
  `Height_` fix, and the async X11 resize race) were independently traced in
  `GraphicsDevice.cpp`/`GraphicsDeviceManager.cpp` and confirmed accurate down to the exact line
  regions and call order — not merely plausible-sounding narrative.
- Robust against the exact race condition it documents: polls on two independent signals
  (physical SDL window size and the `Viewport` property) rather than asserting on a fixed frame
  count, with an explicit, diagnosable (if generic — see F1) timeout failure rather than a silent
  hang or false pass.
- Genuinely shared, byte-identical source registered against three backends — a real cross-backend
  regression sentinel for this specific `GraphicsDevice`/`GraphicsDeviceManager` behavior, not
  merely "backend-agnostic in principle."

## Final Assessment

A carefully-designed test whose header comment's historical and technical claims were
independently verified against the actual `GraphicsDevice.cpp`/`GraphicsDeviceManager.cpp` call
chains and found accurate in every particular. Its async-resize-tolerant polling methodology is
appropriate for the documented X11/Xvfb race and does not weaken the strength of what it proves.
Only a minor diagnosability nit (F1) was found.
