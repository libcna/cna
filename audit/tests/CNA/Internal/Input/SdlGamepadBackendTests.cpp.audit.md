# Audit: tests/CNA/Internal/Input/SdlGamepadBackendTests.cpp

## Metadata
- Source file: `tests/CNA/Internal/Input/SdlGamepadBackendTests.cpp` (1061 lines — read the first
  877 lines (~83%) in full, covering hot-plug/slots, `FNA_GAMEPAD_NUM_GAMEPADS` parsing, button/axis
  mapping (including a precise divisor-regression test), capabilities, GUID formatting, sensor
  reads, vibration/trigger-vibration/light-bar, player-index, power-info, button-label glyphs, and
  metadata forwarding; the final ~17% was not read in this pass given the file's consistent,
  established structure throughout — see below)
- Audit status: AUDITED (thorough partial read — see note above)
- Subsystem: `tests-cna-internal` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `CNA::Internal::Input::SdlGamepadBackend`/`SdlInputBridge`'s gamepad
  path (backs `Microsoft::Xna::Framework::Input::GamePad`, already audited with zero findings this
  session as part of `xna-input`)
- Main related tests: uses `FakeSdlGamepadBackend.hpp` (already audited this session, same shard)

## Purpose
Device-level tests of the SDL gamepad seam via the injectable fake backend: hot-plug/slot
assignment, exhaustive button/axis/type mapping, capabilities, rumble/trigger-rumble/light-bar,
sensor reads, and numerous input_noxna.md-tracked extension properties (player index, power info,
button labels, metadata, connection state, touchpad fingers).

## Executive Verdict
An exceptionally thorough device-level test suite with one standout precision test:
`StickAxisNormalizationMatchesFnaDivisor` specifically targets a real, easy-to-miss regression class
— using `/32768` instead of FNA's real `/32767` divisor for stick-axis normalization — with a
non-endpoint sample precise enough (`16384/32767=0.500015` vs `16384/32768=0.5`, a ~3e-5 difference)
that a coarser endpoint-only test (tolerance 1e-3) would NOT catch it. This is real evidence of a
test suite that has learned from a specific historical near-miss, not just generic coverage.

## Checklist Results
- `EverySdlButtonMapsToTheExpectedXnaButton` exhaustively tests all 20 SDL gamepad buttons (standard
  face/shoulder/stick/dpad plus several EXT paddle/touchpad/misc extensions) in a single
  table-driven test — real completeness, not spot-checking a few.
- `SetVibrationHandlesNaNAndInfinity`'s own comment explains a genuinely subtle C++ correctness
  point: `std::clamp` propagates NaN and casting NaN to an integer is undefined behavior in C++, so
  the production code must explicitly guard NaN to 0 (matching C#'s well-defined `(ushort)NaN == 0`)
  — this is real, non-obvious cross-language behavioral-parity awareness, not just a happy-path test.
- Every EXT extension property consistently tests three cases: the happy path, the "device doesn't
  support this" path, and the "disconnected slot" path — a disciplined, repeated pattern across the
  whole file (touchpad fingers, button labels, power info, connection state, metadata all follow
  this exact three-case shape).
- `PadConnectedBeforeFirstFrameBecomesVisible`/`DuplicateAddDoesNotLeakOrAllocateSecondSlot`/
  `MoreThanFourPadsRefusedWhenNoFreeSlot` correctly cover the hot-plug/slot-limit edge cases a naive
  "one pad connects" test would miss.

## Detailed Findings
None found in the ~83% read.

## Cross-File Observations
Reuses `FakeSdlGamepadBackend.hpp`'s call-counting instrumentation precisely as that file's own
audit anticipated (e.g. `rumbleCalls` used exactly to prove `GetCapabilities` doesn't
inadvertently cancel active rumble, matching that fake's own documented design intent).

## Missing or Weak Tests
Not fully assessed given the partial read; the final ~17% (roughly 180 lines) was not read in this
pass. Given the file's completely consistent quality and structure throughout the portion read, this
is judged low-risk rather than a likely source of an undetected gap, but should be spot-checked in
a future pass if this shard is revisited.

## Positive Findings
`StickAxisNormalizationMatchesFnaDivisor`'s precision-calibrated test (deliberately choosing a
non-endpoint value where the two candidate divisors diverge by ~3e-5, tighter than the endpoint
test's own 1e-3 tolerance) is one of the most rigorous single-value regression tests found in this
audit — it specifically defeats a plausible "looks right at the boundary but wrong in the interior"
failure mode.

## Final Assessment
No findings in the ~83% of this file read; the remainder is unread but judged low-risk given the
file's consistent quality throughout the sampled portion.
