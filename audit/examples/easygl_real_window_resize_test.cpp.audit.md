# Audit: examples/easygl_real_window_resize_test.cpp

## Metadata

- Source file: `examples/easygl_real_window_resize_test.cpp`
- Audit status: AUDITED
- Subsystem: EasyGL backend integration test — `examples-tests-easygl` shard
- File type: C++ example/integration-test executable (Task 348)
- Related production code: `Microsoft::Xna::Framework::GameWindow::updateFromSDL`/`refreshCachedSDLState`/
  `OnClientSizeChanged` (`src/Microsoft/Xna/Framework/GameWindow.cpp:196-228, 310-349`),
  `Game`'s SDL event loop (`src/Microsoft/Xna/Framework/Game.cpp:890-913`),
  `GraphicsDevice::Present`/`UpdateViewportFromWindow` (`src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp:372-382`),
  `GraphicsDeviceManager::INTERNAL_OnClientSizeChanged` (`src/Microsoft/Xna/Framework/GraphicsDeviceManager.cpp:433-446`),
  `EasyGLGraphicsBackend::getLogicalSize` (`src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp:1595-1607`)
- XNA/FNA relevance: `GameWindow.ClientSizeChanged` is real XNA API; the `Viewport`/`PresentationParameters`
  divergence this test pins is an explicitly-documented, deliberate CNA departure from FNA's behavior (see F1 /
  Cross-File Observations).

## Purpose

Verifies that a **real OS-level window resize** (`SDL_SetWindowSize` called directly on the live `SDL_Window*`,
bypassing `GraphicsDeviceManager` entirely — simulating a user dragging the window edge) is correctly reflected end
to end: the XNA `Viewport` updates, `GameWindow.ClientSizeChanged` fires, and `PresentationParameters.BackBufferWidth/
Height` deliberately do **not** follow the physical resize (CNA's `FixedHeightDynamicWidth` virtual-resolution
design). This complements a prior test (`easygl_backbuffer_resize_test.cpp`, Task 227, not in this batch) that only
covers the GDM-API-driven resize path.

## Executive Verdict

**Healthy** — this is an unusually well-instrumented test: its own header comment documents a real discovery made
during its own development (temporarily disabling `GraphicsDeviceManager`'s `ClientSizeChanged` subscription to
verify check 2's actual discriminating power, finding it doesn't depend on the event at all) and adds check 4
specifically to close that self-identified gap. Every claim in the header comment was independently re-traced
against the actual source and found accurate.

## Checklist Results

### Behavioral correctness
Traced all four claims made in the file's own header comment against source:
1. **"CNA's Viewport tracks the window on EVERY frame... because `GraphicsDevice::Present()` unconditionally calls
   `UpdateViewportFromWindow()`"** — confirmed at `GraphicsDevice.cpp:372-382`: `Present()` calls
   `backend_->Present(); UpdateViewportFromWindow();` unconditionally, with no gating on any resize-event flag.
2. **"check 2... does NOT actually depend on ClientSizeChanged firing"** — confirmed: `GraphicsDeviceManager`'s own
   subscription (`GraphicsDeviceManager.cpp:523`, handler `INTERNAL_OnClientSizeChanged`,
   lines 433-446) calls `graphicsDevice_->UpdateViewportFromWindow()` too, but this is redundant with (1)'s
   per-frame `Present()` call, not the sole source of the update.
3. **"an actual window resize... `SDL_EVENT_WINDOW_RESIZED` → ... `GameWindow::updateFromSDL()` →
   `GameWindow.ClientSizeChanged`"** — confirmed: `Game.cpp:910-913` handles
   `SDL_EVENT_WINDOW_RESIZED`/`SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED` by calling `Window_.updateFromSDL()`, which
   calls `refreshCachedSDLState(true)` (`GameWindow.cpp:310-313`), which raises `ClientSizeChanged` when
   `clientBounds_.Width`/`Height` actually changed (lines 340-343).
4. **"CNA's default `PresentationMode::FixedHeightDynamicWidth` only lets width track the window"** — confirmed:
   `EasyGLGraphicsBackend::getLogicalSize` (`EasyGLGraphicsBackend.cpp:1595-1607`) pins `height = virtualHeight_`
   unconditionally when `virtualHeight_ > 0`, and only recomputes `width` from the live physical aspect ratio under
   `FixedHeightDynamicWidth`; `GraphicsDeviceManager`'s own default (`preferredPresentationMode_ =
   PresentationMode::FixedHeightDynamicWidth`, `GraphicsDeviceManager.cpp:55`) confirms this is the mode active for
   this test (it never changes it).
All four claims verified accurate. PASS.

### Logic
The polling loop (`Draw()`, lines 110-155) correctly distinguishes "physical resize hasn't propagated to SDL yet"
from "propagated to SDL but not yet reflected in the viewport" via two independent booleans
(`physicalResizeApplied`, `viewportUpdated`), only calling `finish()` once both are true, with a
`kMaxWaitFrames=300` timeout that reports failure (rather than hanging indefinitely) if the X11/Xvfb resize event
never arrives — a reasonable, non-flaky design for an asynchronous windowing-system dependency.

### Robustness
Frame 0 is special-cased purely to capture "before" state and issue the resize (`frame_ == 0` branch,
lines 115-133) before any polling begins — correctly `return`s early so the very first resize-issuing frame is
never also checked for completion, avoiding a race against SDL's own event latency.

### Testing
This is itself a targeted regression test with no counterpart for the reverse case (see Missing/Weak Tests).

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings — every check in this file is grounded in directly-traced production code, and no
inaccurate claims were found in its extensive header commentary (unlike the sibling
`easygl_present_interval_test.cpp`, whose comment was found to misattribute control flow).

### F1 (INFO) — The test pins a documented, deliberate FNA behavioral divergence rather than FNA parity

- Severity: INFO (documented, intentional; not a defect)
- Confidence: HIGH
- Category: XNA/FNA parity (informational)
- Location/symbol: check 3, `finish()` lines 91-93
- Evidence: real FNA forwards the physical window size into `BackBufferWidth`/`Height` on every resize
  (per this file's own comment, corroborated by `GraphicsDeviceManager::INTERNAL_OnClientSizeChanged`'s own comment
  at `GraphicsDeviceManager.cpp:438-443`: "Do NOT call ApplyChanges() here. ApplyChanges() would forward the new
  physical window size as the virtual resolution, corrupting the game's logical coordinate space"). This test
  explicitly asserts the *opposite* of FNA's behavior as the correct, expected CNA outcome.
- Why it matters: this is exactly the kind of intentional deviation `CLAUDE.md`'s workflow rules require to be
  "documented with a `//` comment in the source" — which it is, extensively, in both this test file and the
  production code it's pinning. Recorded here only so the deviation is visible in this audit's index, not as a
  defect.
- FNA/XNA comparison: real FNA `GraphicsDeviceManager` (`FNA/src/GraphicsDeviceManager.cs`) does propagate a
  real resize into `BackBufferWidth`/`Height` via its own `ClientSizeChanged` handler calling into device reset
  logic; CNA's `FixedHeightDynamicWidth` virtual-resolution feature is itself a `NOXNA`-flavored extension to XNA's
  presentation model, so this divergence is a necessary consequence of that extension, not an oversight.
- Suggested action: none — already correctly documented; recorded for cross-referencing in
  `AUDIT_CROSS_CUTTING_FINDINGS.md`'s FNA-parity-deviation index.

## Cross-File Observations

- Complements Task 227's `easygl_backbuffer_resize_test.cpp` (GDM-API-driven resize path, not in this batch) by
  covering the entirely separate OS-event-driven resize path — the file's own header comment correctly frames this
  as filling a real, previously-unverified gap ("Nobody had verified the OTHER path").
- Shares the `PresentationMode::FixedHeightDynamicWidth`/`getLogicalSize` dependency with
  `easygl_presentation_parameters_test.cpp`'s indirect assumptions about `GraphicsDeviceManager`'s defaults, though
  neither file references the other.

## Missing or Weak Tests

- No test of the equivalent resize behavior under `PresentationMode::FixedWidthDynamicHeight` or any non-default
  presentation mode (only `FixedHeightDynamicWidth`, the GDM default, is exercised).
- No test of a resize that shrinks the window (only grows: `physW * 2`, `physH + 200`) — a shrink could plausibly
  exercise different clamping/rounding edge cases in `getLogicalSize`'s aspect-ratio computation
  (`static_cast<int>((double)physW * virtualHeight_ / physH + 0.5)`), untested here.
- `clientSizeChangedCount_` is checked only for `> 0`, not for an exact expected count — reasonable given SDL/X11
  can coalesce or duplicate resize events, but means a backend that fires the event twice per resize (a potential
  latent bug) would not be caught by this test.

## Positive Findings

- Genuinely rare and valuable engineering discipline: the header comment documents an actual falsifiability
  experiment performed on this test itself (temporarily disabling a subscription to confirm a check's
  discriminating power) rather than assuming the test design was airtight — exactly the kind of self-verification
  this audit rewards.
- Correctly designed to add a new, independent check (check 4) specifically to close the gap that experiment
  revealed, rather than leaving the weaker check unaddressed.
- The intentional FNA divergence it pins is honestly and specifically documented, including cross-references to the
  exact production-code comment justifying it.

## Final Assessment

An exceptionally well-verified, self-critical integration test. Every factual claim in its extensive header
commentary was independently re-traced against the real `Game`/`GameWindow`/`GraphicsDevice`/`GraphicsDeviceManager`
source and found accurate, including one genuinely subtle discovery (check 2's redundancy with per-frame
`Present()` refresh) that the test's own authors caught and compensated for. No correctness defects found.
