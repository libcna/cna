# Audit: examples/easygl_fullscreen_field_test.cpp

## Metadata

- Source file: `examples/easygl_fullscreen_field_test.cpp`
- Audit status: AUDITED
- Subsystem: EasyGL backend integration test (`examples-tests-easygl` shard)
- File type: C++ example/integration test, registered as CTest via
  `cmake/Tests/EasyGLTests.cmake:1002` (`cna_test_easygl_fullscreen_field`)
- Related production code: `GraphicsDeviceManager::setIsFullScreenProperty`/`getIsFullScreenProperty`/
  `ToggleFullScreen`/`ApplyChanges` (`src/Microsoft/Xna/Framework/GraphicsDeviceManager.cpp:107-116,
  206-252`), `GraphicsDevice::getPresentationParametersProperty`.
- XNA/FNA relevance: `GraphicsDeviceManager.IsFullScreen`/`ToggleFullScreen()` are real XNA 4.0 API
  members (`Microsoft.Xna.Framework.GraphicsDeviceManager`); `PresentationParameters.IsFullScreen` is
  the corresponding `GraphicsDevice`-side mirror FNA keeps in sync.
- Main related tests: none of the other files in this batch overlap; this is the dedicated coverage
  for the `IsFullScreen` field round-trip.

## Purpose

`FullScreenFieldTest` verifies that `GraphicsDeviceManager.IsFullScreen` is stored consistently in the
device's `PresentationParameters` across `setIsFullScreenProperty()` + `ApplyChanges()` and
`ToggleFullScreen()`, and — per its own header comment — that a headless/virtual-display environment
where `SDL_SetWindowFullscreen` can fail does not throw, because the PP value is stored before the SDL
call is attempted. Correct placement as an EasyGL example/integration test.

## Executive Verdict

**Healthy.** All seven checks are genuine state-consistency assertions (not "doesn't crash"), each one
independently meaningful, and cross-checked directly against `GraphicsDeviceManager.cpp`'s actual
implementation, which confirms the exact round-trip path (`setIsFullScreenProperty` → `markPreferencesChanged()`
→ `ApplyChanges()` → `applyToExistingBackend()` → `GraphicsDevice::Reset()` → stored
`PresentationParameters`) this test exercises.

## Checklist Results

### API / XNA / FNA parity
`getIsFullScreenProperty`/`setIsFullScreenProperty`/`ToggleFullScreen`/`ApplyChanges` match FNA's
`GraphicsDeviceManager.IsFullScreen`/`ToggleFullScreen()`/`ApplyChanges()` names and semantics exactly.
`getPresentationParametersProperty().getIsFullScreenProperty()` correctly reads the mirrored
`GraphicsDevice`-side copy rather than re-reading the manager's own preference field, which is the
right way to prove the value actually propagated end-to-end rather than merely being stored locally in
the manager.

### Behavioral correctness
Traced against `GraphicsDeviceManager.cpp`:
- `setIsFullScreenProperty(bool value)` (line 112-116) sets `isFullScreen_` and calls
  `markPreferencesChanged()` — a pure preference-staging call, no immediate device effect, matching
  FNA's own deferred-apply model (`ApplyChanges()` must be called to take effect).
- `ApplyChanges()` (line 206-246): early-returns if `!prefsChanged_ && !useResizedBackBuffer_` (line
  214-217) — since `setIsFullScreenProperty` always calls `markPreferencesChanged()`, every
  `set...+ApplyChanges()` pair in this test genuinely reaches `applyToExistingBackend()` (line 242),
  which calls `graphicsDevice_->Reset(pp, *gdi.getAdapterProperty())` — the real per-property forwarding
  path, not a no-op.
- `ToggleFullScreen()` (line 248-252) is exactly `setIsFullScreenProperty(!getIsFullScreenProperty());
  ApplyChanges();` — the test's "toggle flips and stores" and "toggle back is idempotent" checks (lines
  55-65) are a correct, direct exercise of this exact implementation.
- The test's own claim (header comment, lines 4-7) that a fullscreen-switch failure at the SDL layer
  must not throw is architecturally supported: `Reset()`'s `applyPresentationParametersToWindow()` is
  documented elsewhere in this codebase (see `GraphicsDeviceManager.cpp` line 574-576) as "non-fatal on
  fullscreen failure" — this test does not itself force an SDL failure (it runs in a real/Xvfb
  environment), so it verifies the *storage* half of that contract, not the *SDL-failure-doesn't-throw*
  half directly; that second half is implicitly relied upon rather than independently forced. See F1.

### Logic
Each `check()` call (line 25-29) increments `pass_`/`fail_` and prints immediately — straightforward,
no ordering bugs. The final default-value check (line 37) runs before any mutation, correctly
establishing the baseline (`IsFullScreen=false` by default) before the round-trip checks that follow.

### Memory/resource lifetime
`gdm_` (`std::unique_ptr<GraphicsDeviceManager>`) is constructed in the `FullScreenFieldTest()`
constructor (line 79) before `Game::Run()` starts the loop — matching the standard XNA pattern of
constructing the device manager before the game runs. No manual disposal in this file; relies on the
`Game` subclass's own destruction order, which is outside this file's scope to verify further.

### C++ correctness
No raw pointers, no casts, no undefined behavior surface in this file — plain property get/set calls
and boolean comparisons.

### Performance
N/A — single-shot test, six device-reset round-trips total, not a hot path.

### Thread safety
N/A — single-threaded `Game`/`Initialize()` sequence.

### Architecture
Correctly scoped to the public `Microsoft::Xna::Framework`/`GraphicsDeviceManager` API surface; no
backend-specific (`EasyGLGraphicsBackend`) symbols referenced, appropriate for an integration test at
this layer.

### Maintainability
91 lines, single responsibility, clear per-check labels printed to stdout, no dead code.

### Portability
Deliberately headless-tolerant by design (per the header comment) — this is the entire point of the
test, and it is correctly built to not depend on fullscreen actually taking visual effect, only on the
CNA-side state being consistent regardless of what the OS/windowing layer permits.

### Robustness
**F1**: The test asserts that state is stored correctly, but never actually forces or detects a
genuine `SDL_SetWindowFullscreen` failure path — if the sandbox happens to have a working fullscreen
window manager, the "must not throw even if SDL cannot switch" claim in the header comment is never
truly exercised by this specific run; it is exercised only incidentally, whenever the CI/sandbox
environment happens to be one where fullscreen actually fails. This is an inherent limitation of
testing "doesn't throw on an environment-dependent failure" without fault injection, not a bug in this
file, but worth naming precisely rather than treating the header comment's claim as automatically
proven by every green run.
- Severity: LOW
- Confidence: HIGH (structural — the test has no fault-injection hook to force the SDL call to fail)
- Category: testing / robustness
- Suggested future action (not implemented by this audit): if a lightweight way to force
  `SDL_SetWindowFullscreen` to fail existed in this project's SDL abstraction (e.g. an env var or a
  headless video driver already known to reject fullscreen), wiring it into this specific test would
  make the "doesn't throw" claim independently verified rather than incidentally true.

### Testing
This file is itself the dedicated test for `IsFullScreen`'s round-trip; no other file in this batch or
its known siblings duplicates this coverage. All getter/setter/toggle combinations for this one field
are covered: default, explicit true, explicit false, toggle-to-true, toggle-to-false — a complete state
matrix for a boolean field.

### Cross-file consistency
`gdm_->getIsFullScreenProperty()` (line 45, 58) and `dev.getPresentationParametersProperty().getIsFullScreenProperty()`
(lines 37, 43, 51, 56, 64) are checked for equality at multiple points (line 58-60) — correctly
verifying the two independent storage locations (`GraphicsDeviceManager`'s own preference field vs.
`GraphicsDevice`'s live `PresentationParameters`) stay in sync, which is exactly the kind of
two-copies-of-the-same-fact bug class this project's own `CLAUDE.md` "Behavior Fidelity" section calls
out (state-transition/lifetime correctness).

## Detailed Findings

### F1 — Header comment's "must not throw on SDL failure" claim is not independently fault-injected

(See Robustness section above for full detail — reproduced here per the checklist format.)

- Severity: LOW
- Confidence: HIGH
- Category: testing / robustness
- Location/symbol: whole file; no fault-injection hook exists for `SDL_SetWindowFullscreen`.
- Why it matters: a genuine regression in the "PP value is stored before the SDL call" ordering
  invariant would only be caught by this test in an environment where the SDL call actually fails —
  i.e., the test's headline claim is conditionally, not unconditionally, verified by a passing run.
- FNA/XNA comparison: N/A.

## Cross-File Observations

None beyond the two-copies-of-truth pattern noted above, which recurs across other
`GraphicsDeviceManager`-facing tests in this shard (e.g. the vsync test in this same batch) and is a
reasonable, consistent verification style across the shard rather than a one-off.

## Missing or Weak Tests

- No fault-injected coverage of the actual "SDL_SetWindowFullscreen fails, PP is still correct, no
  throw" scenario (see F1) — would require either an SDL video-driver mode known to reject fullscreen
  transitions, or a CNA-side seam to simulate the failure, neither of which currently exists in this
  test.

## Positive Findings

- Verifies both directions of the round-trip (manager-side getter and device-side `PresentationParameters`
  mirror) rather than trusting just one, catching a class of bug (state gets set on one side but not
  synced to the other) that a shallower test would miss.
- Explicitly designed to be headless/CI-safe by construction, matching this project's stated concern
  (per the header comment) about SDL fullscreen failing in virtual-display environments — a
  well-reasoned, not accidental, design choice.

## Final Assessment

A focused, correctly cross-checked test of `GraphicsDeviceManager.IsFullScreen`'s full state-transition
matrix, verified directly against `GraphicsDeviceManager.cpp`'s real implementation. Its only gap is
inherent to testing an "doesn't throw under an environment-dependent failure" claim without fault
injection (F1) — a minor, low-severity limitation rather than a defect.
