# Audit: tests/CNA/Internal/Input/SdlInputBridgeFuzzTests.cpp

## Metadata
- Source file: `tests/CNA/Internal/Input/SdlInputBridgeFuzzTests.cpp` (153 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-cna-internal` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests `CNA::Internal::Input::SdlInputBridge::ProcessEvent` (the internal SDL
  event dispatch backing `Microsoft::Xna::Framework::Input::{Keyboard,Mouse,GamePad}` and
  `Touch::TouchPanel`), INPUT-TEST-009
- Main related tests: the crash-safety counterpart to `SdlInputBridgeGoldenTests.cpp`'s
  correctness-of-value tests (explicitly documented in both files' header comments)

## Purpose
Deterministically fuzzes `ProcessEvent` with 5000 pseudo-random but well-typed keyboard/mouse/text/
touch SDL events (fixed-seed LCG, no wall-clock/`std::random_device`) and asserts no crash and every
public state-snapshot API stays callable throughout.

## Executive Verdict
Correct, genuinely deterministic (no clock/OS-entropy seeding — a real requirement for CI
reproducibility, satisfied here), and reasonably targeted at real edge cases: the text-input corpus
specifically includes a lone continuation byte (`"\xFF"`) and a truncated multi-byte sequence
(`"x\xC3"`) alongside valid ASCII/2-byte/4-byte UTF-8 — genuinely adversarial malformed-UTF-8 input,
not just well-formed random text.

## Checklist Results
- The `Rng` struct is a documented, minimal LCG (fixed multiplier/increment, `state >> 33` for
  output) — same reproducibility justification pattern already seen and praised in
  `XactParserFuzzTests.cpp` earlier in this shard.
- `randomEvent()`'s finger-event case deliberately reuses a small ID pool (`rng.below(12)`) rather
  than fully random 64-bit finger IDs — a correct choice, since it exercises the touch-slot-map's
  collision/reuse handling (the actual bug-prone path) rather than mostly producing disjoint,
  uninteresting single-touch sequences.
- The test correctly excludes gamepad events with a documented rationale (gamepad device-open/
  sensor/rumble is exercised separately through the injectable fake backend, not through raw SDL
  events) — an honest, explicit scoping decision rather than a silent gap.
- Every iteration calls all four public state-snapshot getters (`Keyboard::GetState()`,
  `Mouse::GetState()`, `Touch::TouchPanel::GetState()`, `GamePad::GetState()`) plus periodically
  `TouchPanel::Update()` and drains any available gesture — a reasonably complete "stays readable"
  check across every input subsystem the bridge touches, not just the one under direct fuzzing.

## Detailed Findings
None — this is a crash/UB-safety net, not a correctness oracle, and it performs that role
correctly and honestly (its own header comment explicitly disclaims asserting output *values*,
correctly deferring that job to `SdlInputBridgeGoldenTests.cpp`).

## Cross-File Observations
The fuzz/golden split documented in both files' header comments is a clean, deliberate separation
of concerns (crash-safety vs. value-correctness) — a good pattern already seen with the
`XactParserFuzzTests.cpp`/`XactParserTests.cpp` pairing in this same shard.

## Missing or Weak Tests
None identified — the malformed-UTF-8 test corpus is a meaningful adversarial choice, and the event
mix (9 event types weighted evenly) gives reasonable breadth for a single fuzz test.

## Positive Findings
The deliberate small-pool finger-ID reuse to stress-test slot-map collision handling, and the
malformed-UTF-8 byte sequences in the text corpus, both show real thought about what inputs are
likely to expose bugs rather than generating maximally "random" (and less useful) data.

## Final Assessment
No findings.
