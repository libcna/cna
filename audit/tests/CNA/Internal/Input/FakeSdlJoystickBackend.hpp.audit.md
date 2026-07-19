# Audit: tests/CNA/Internal/Input/FakeSdlJoystickBackend.hpp

## Metadata
- Source file: `tests/CNA/Internal/Input/FakeSdlJoystickBackend.hpp` (177 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-cna-internal` shard
- File type: C++ test-support header (fake/mock implementation, not a `TEST`-containing file)
- XNA/FNA relevance: Test infrastructure for `CNA::Internal::Input::ISdlJoystickBackend`
  (CNA-internal SDL seam, no direct FNA equivalent)
- Main related tests: consumed by `SdlJoystickBackendTests.cpp` (in this same shard)

## Purpose
A test-only fake for the internal SDL raw-joystick seam (distinct from the gamepad seam), covering
hot-plug, capabilities, and raw axis/button/hat/ball state.

## Executive Verdict
Correct, simple, consistent in design with the sibling `FakeSdlGamepadBackend.hpp`/
`FakeSdlHapticBackend.hpp` fakes in this same directory.

## Checklist Results
- Every accessor (`GetJoystickAxis`/`GetJoystickButton`/`GetJoystickHat`/`GetJoystickBall`)
  correctly bounds-checks its index against the registered config's vector size before indexing,
  returning a safe default (`0`/`false`/`SDL_HAT_CENTERED`/`false`) for an out-of-range probe rather
  than crashing — consistent with the bounds-checking discipline already praised in
  `FakeSdlGamepadBackend.hpp.audit.md`.
- `GetJoystickPowerInfo`'s null-device path correctly sets `*percent = -1` and returns
  `SDL_POWERSTATE_ERROR` (a meaningful "unknown device" sentinel) rather than leaving `*percent`
  uninitialized.

## Detailed Findings
None.

## Cross-File Observations
Structurally near-identical in design to `FakeSdlGamepadBackend.hpp`/`FakeSdlHapticBackend.hpp` —
the three fakes together form a consistent, well-established pattern for this project's SDL-seam
test infrastructure.

## Missing or Weak Tests
N/A — this is test infrastructure, not itself a test file.

## Positive Findings
Minimal, correct, consistent with its sibling fakes.

## Final Assessment
No findings.
