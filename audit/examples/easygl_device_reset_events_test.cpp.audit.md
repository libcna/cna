# Audit: examples/easygl_device_reset_events_test.cpp

## Metadata

- Source file: `examples/easygl_device_reset_events_test.cpp`
- Audit status: AUDITED
- Subsystem: EasyGL backend integration test (`examples-tests-easygl` shard), Task 183
- File type: standalone `Game` subclass test exercising `GraphicsDeviceManager` events
- Related production code: `GraphicsDeviceManager::ApplyChanges()` (`GraphicsDeviceManager.cpp:206-246`),
  `OnDeviceResetting`/`OnDeviceReset` (`GraphicsDeviceManager.cpp:392-399`), `Game::DoInitialize()`
  (`Game.cpp:644-652`)
- FNA reference: `Graphics/GraphicsDeviceManager.cs` (`DeviceResetting`/`DeviceReset` events, `ApplyChanges()`
  ordering)
- Build registration: `cmake/Tests/EasyGLTests.cmake:905-906`

## Purpose

Verifies that `GraphicsDeviceManager::DeviceResetting` and `DeviceReset` fire (in that order) when `ApplyChanges()`
is called on an *already-initialized* device, using a monotonically-increasing sequence counter captured by each
handler to prove ordering rather than just "both eventually fired."

## Executive Verdict

**Healthy.** Traced the full call path this test depends on (`Game::DoInitialize()` → `GraphicsDeviceManager::
CreateDevice()` on first init, vs. a later explicit `ApplyChanges()` → `OnDeviceResetting`/`applyToExistingBackend`/
`OnDeviceReset`) and confirmed the test's central assumption — that the *first* device creation does not fire these
two events, while a *subsequent* `ApplyChanges()` on an existing device does, in `DeviceResetting`-then-`DeviceReset`
order — is exactly what the production code does.

## Checklist Results

### Purpose
PASS — small, correctly-scoped, single-responsibility test.

### API / XNA / FNA parity
PASS — `GraphicsDeviceManager::DeviceResetting`/`DeviceReset` are `System::EventHandler<System::EventArgs>` members
matching FNA's `event EventHandler<EventArgs> DeviceResetting`/`DeviceReset`. The `+=` subscription syntax used
(lines 38-45) matches this project's documented event-subscription convention (`CLAUDE.md`'s "Events" section).

### Behavioral correctness
PASS, independently traced:
- The test subscribes to both events inside its own `Initialize()` override, *after* calling the base
  `Game::Initialize()` (line 34) — but the comment's real claim is about ordering relative to `Game::DoInitialize()`,
  which is a separate, earlier method: `Game::DoInitialize()` (`Game.cpp:644-652`) unconditionally calls
  `graphicsDeviceManager_->CreateDevice()` directly (not via `ApplyChanges()`) before the subclass's `Initialize()`
  override ever runs. Confirmed `GraphicsDeviceManager::ApplyChanges()`'s own logic (lines 206-212): `if
  (graphicsDevice_ == nullptr) { CreateDevice(); return; }` — i.e., the *first* time a device is created (whether
  via `DoInitialize()`'s direct `CreateDevice()` call or an early `ApplyChanges()` call before a device exists),
  `OnDeviceResetting`/`OnDeviceReset` are never reached; the function returns immediately after `CreateDevice()`.
  This confirms the test's premise that subscribing to the events *after* `Game::Initialize()` correctly excludes
  the initial device-creation event-free path — though the test's own comment phrasing ("the constructor's implicit
  ApplyChanges()") slightly mischaracterizes the actual mechanism (it's `Game::DoInitialize()`'s direct
  `CreateDevice()` call, not the `GraphicsDeviceManager` constructor or an implicit `ApplyChanges()` call) — a
  cosmetic comment-accuracy nit, not a functional defect, since the actual behavior (no events on first creation)
  holds regardless of which call path produces it.
- `setPreferredBackBufferWidthProperty(400)` (line 48) calls `markPreferencesChanged()`
  (`GraphicsDeviceManager.cpp:158-159`), setting `prefsChanged_ = true`; the subsequent `gdm_->ApplyChanges()` (line
  49) then takes the "device already exists" branch (`graphicsDevice_ != nullptr`, `prefsChanged_` true), which
  unconditionally calls `OnDeviceResetting(...)` (line 240) then, after `applyToExistingBackend(gdi)`,
  `OnDeviceReset(...)` (line 244) — confirmed by direct reading of `GraphicsDeviceManager.cpp:206-246` that these
  two calls are unconditional and strictly sequential in source order, matching the test's sequence-counter
  expectation (`resettingSeq_ < resetSeq_`, and specifically `==1`/`==2` — lines 53-55).
- The sequence-counter design (`seqCounter_`, incremented and captured into `resettingSeq_`/`resetSeq_` by each
  handler) is a stronger check than a simple boolean "both fired": it would catch a hypothetical regression where
  both events fire but in the wrong relative order, which a pair of independent booleans could not.

### Logic
PASS — `check(resettingSeq_ == 1, ...)` and `check(resetSeq_ == 2, ...)` (lines 54-55) are redundant with (but
stricter than) `resettingSeq_ < resetSeq_` (line 53) given only two handlers exist in this test — correctly range
from a general ordering check to an exact-position check, both valid, no logic error.

### C++ correctness
PASS — lambda captures `this` by value (correct, since the lambdas are only invoked synchronously within the same
object's lifetime, during `ApplyChanges()`, never stored beyond it).

### Testing
This file is itself a test — see Behavioral correctness above for its own internal validity check.

## Detailed Findings

No HIGH, CRITICAL, or MEDIUM findings.

- LOW / comment-accuracy nit: the in-code comment "so the constructor's implicit ApplyChanges() is not counted"
  (line 36-37) describes the mechanism slightly inaccurately — the actual first-device-creation call is
  `Game::DoInitialize()`'s direct `graphicsDeviceManager_->CreateDevice()` (`Game.cpp:651`), not an implicit
  `ApplyChanges()` call from the `GraphicsDeviceManager` constructor (whose own comment,
  `GraphicsDeviceManager.cpp:75-82`, explicitly states it deliberately does *not* call `ApplyChanges()`). The
  test's actual behavior and pass/fail outcome are unaffected — both paths correctly avoid firing the events on
  first creation — but the comment names the wrong call site. Confidence HIGH (both call sites were read directly).

## Missing or Weak Tests

- Only one subscriber per event is tested; multi-subscriber fan-out (does `EventHandler<T>::Raise` correctly invoke
  *all* subscribers, in subscription order) is not exercised here — likely covered elsewhere in the `System`/
  `EventHandler` test suite (reference-only per `sharp-runtime` policy), but not in this file.
- Does not test `DeviceResetting`/`DeviceReset` firing from a genuine backbuffer-resize-driven `ApplyChanges()` path
  (only a manual preference mutation) — a reasonable scope boundary for a focused unit test, not a defect.

## Positive Findings

- The monotonic sequence-counter technique is a genuinely stronger correctness check than a same-outcome boolean
  pair — it can detect a same-frame ordering regression that independent booleans would miss.
- Correctly reasons about (and the test design correctly relies on) the real distinction between the
  first-device-creation code path (no events) and a subsequent `ApplyChanges()` on an existing device (both events,
  in order) — verified against the actual `GraphicsDeviceManager::ApplyChanges()` implementation.

## Final Assessment

A small, correctly-designed test whose core ordering claim was independently verified against the real
`GraphicsDeviceManager::ApplyChanges()`/`Game::DoInitialize()` code paths. Only issue found is a cosmetic
comment-accuracy nit misnaming which call site skips the first-creation events; the test's actual behavior and
result are correct regardless.
