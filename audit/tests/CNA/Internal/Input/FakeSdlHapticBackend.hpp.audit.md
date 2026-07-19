# Audit: tests/CNA/Internal/Input/FakeSdlHapticBackend.hpp

## Metadata
- Source file: `tests/CNA/Internal/Input/FakeSdlHapticBackend.hpp` (326 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-cna-internal` shard
- File type: C++ test-support header (fake/mock implementation, not a `TEST`-containing file)
- XNA/FNA relevance: Test infrastructure for `CNA::Internal::Input::ISdlHapticBackend` (CNA-internal
  SDL seam, no direct FNA equivalent)
- Main related tests: consumed by `SdlHapticBackendTests.cpp` (in this same shard)

## Purpose
A test-only fake for the internal SDL haptic (force-feedback) seam, covering enumeration,
capabilities, effect creation/update/lifecycle, and rumble, with no real hardware required.

## Executive Verdict
Correct, and notable for one genuinely subtle, correctly-handled lifetime detail:
`SnapshotCustomData()` explicitly guards against a real dangling-pointer risk that a naive fake
would introduce.

## Checklist Results
- `SnapshotCustomData()`'s own comment correctly explains the real hazard it avoids: real SDL copies
  a haptic effect's `Custom` sample data *synchronously* during `Create`/`UpdateHapticEffect` (the
  caller's buffer need not outlive the call) — a naive fake that just stored a shallow copy of the
  `SDL_HapticEffect` union would leave `.custom.data` pointing at the caller's now-destroyed
  temporary buffer. This fake correctly snapshots the sample data into its own owned
  `std::vector<Uint16>` storage and repoints `.custom.data` at that snapshot, matching real SDL's
  actual synchronous-copy semantics rather than introducing a fake-specific use-after-free that a
  consuming test could then spuriously "pass" against garbage memory.
- `CreateHapticEffect`/`UpdateHapticEffect` both correctly apply this snapshot before returning,
  for both `lastCreatedEffect`/`lastCreatedCustomData` and `lastUpdatedEffect`/`lastUpdatedCustomData`
  respectively — the pattern is applied consistently at both call sites, not just one.
- `liveEffects`/`playingEffects` set-based tracking gives a reasonable model of SDL's real
  effect-lifecycle state machine (created → run → stop → destroy) for a fake, letting consumer
  tests assert on run/stop/destroy behavior meaningfully.

## Detailed Findings
None.

## Cross-File Observations
None beyond the general observation (also true of `FakeSdlGamepadBackend.hpp`) that this project's
test-fake infrastructure shows real attention to matching subtle real-API lifetime/synchronization
semantics, not just providing a superficially-matching interface.

## Missing or Weak Tests
N/A — this is test infrastructure, not itself a test file.

## Positive Findings
The `SnapshotCustomData` lifetime-matching design is a genuinely well-thought-out piece of test-fake
engineering — it would have been easy to write a shallow-copy fake that "works" in the common case
but silently introduces a use-after-free specific to the test double, undermining confidence in any
test that exercises `SDL_HAPTIC_CUSTOM` effects.

## Final Assessment
No findings.
