# Audit: tests/Microsoft/Xna/Framework/Input/ButtonsTests.cpp

## Metadata
- Source file: `tests/Microsoft/Xna/Framework/Input/ButtonsTests.cpp` (71 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-xna-input` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `Microsoft::Xna::Framework::Input::Buttons`
- Main related tests: N/A (this IS a test file)

## Purpose
Pins every named `Buttons` flags-enum bit value to its real XNA/FNA numeric constant, split into
the 25 core XNA button bits and the 6 FNA-extension bits (`Misc1EXT`, `Paddle1EXT`-`Paddle4EXT`,
`TouchPadEXT`) that occupy the gaps in the XNA bit layout, plus a test for the bitwise operators
(`|`, `&`, `|=`, `&=`, `~`).

## Executive Verdict
**Directive-specified Buttons exhaustive-value-assertion check: CONFIRMED.** This file asserts
every one of the 31 named `Buttons` values individually against a hardcoded hex constant (25 in
`CoreXnaValuesMatchXnaBitConstants`, 6 in `FnaExtensionValuesMatchTheExtensionBits`) — not a
sample, and not derived from re-running the implementation. Cross-checked against the real FNA
`Buttons` enum layout: 25 core XNA bits (0x1 through 0x40000000, skipping the gaps FNA's own
extensions fill) + 6 FNA extension bits filling exactly those gaps (bit 10 = `Misc1EXT`, bits
16-20 = `Paddle1-4EXT`/`TouchPadEXT`) = 31 named values total, with bit 31 (0x80000000)
deliberately unassigned (matching real FNA, which also leaves it unused). This test file's
constant list matches that real layout exactly, value for value.

## Checklist Results
- All 31 named values individually asserted with hardcoded hex constants (not a sample, not a loop
  over "all defined enumerators" reflection-style, but an explicit, auditable list — the strongest
  form of ABI-pinning test for a flags enum).
- Bitwise operators (`|`, `&`, `|=`, `&=`, `~`) are covered with both combination and masking
  checks (`combined & Buttons::X` correctly asserts zero for a bit that wasn't set).
- The two-test split (core XNA vs. FNA-extension) makes the origin of each bit's value legible in
  the test output itself, rather than one large undifferentiated list.

## Detailed Findings
None.

## Cross-File Observations
This is the file the audit directive specifically asked to verify for exhaustive Buttons coverage
(cross-referencing the already-committed `xna-input` production-code shard audit's automated
FNA-diff finding) — confirmed complete and methodologically sound (hardcoded expected list, not a
derived-from-current-output sample).

## Missing or Weak Tests
None identified for this enum's public surface (all named values, all bitwise operators).

## Positive Findings
The core-vs-extension test split, and the explicit gap-filling design comment, make this an
unusually easy-to-audit exhaustive-value test — a reviewer can directly compare the hex list
against the real FNA source without needing to run anything.

## Final Assessment
No findings. Directive check (Buttons exhaustive-value assertion) CONFIRMED PASS.
