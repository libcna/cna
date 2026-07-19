# Audit: tests/Microsoft/Xna/Framework/Input/KeyboardInputTests.cpp

## Metadata
- Source file: `tests/Microsoft/Xna/Framework/Input/KeyboardInputTests.cpp` (621 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-xna-input` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `Microsoft::Xna::Framework::Input::Keyboard`/`KeyboardState`/`Keys`
- Main related tests: N/A (this IS a test file)

## Purpose
Covers `Keyboard::GetState()`/`GetState(PlayerIndex)`, `GetKeyFromScancodeEXT`, `KeyboardState`'s
three constructors (default, initializer-list, unordered_set), indexer/`getItem`, `IsKeyDown`/
`IsKeyUp`, `GetPressedKeys()` ordering/dedup, equality/`GetHashCode`, `ToString()`, out-of-range
`Keys` value hardening across every accessor, and — most importantly for this audit — an
exhaustive numeric-parity table for the entire 160-member `Keys` enum.

## Executive Verdict
**Directive-specified Keys exhaustive-value-assertion check: CONFIRMED.**
`KeysValuesMatchXNANumericConstants` asserts all 160 `Keys` members against their exact FNA/
Windows-Virtual-Key numeric value via a hardcoded `{key, value}` table (not a sample, not derived
from re-running the implementation), backed by three independent safety nets: a `static_assert`
pinning the table's own size to exactly 160 (fails to compile if a member is added/removed without
updating the table), a per-entry `EXPECT_EQ` against the literal value, and an `unordered_set`-based
duplicate-value check ensuring every one of the 160 values is distinct. This is a rigorous,
auditable, ABI-guardrail-style test — the strongest form of exhaustive enum-value pinning seen in
this codebase.

## Checklist Results
- **Keys exhaustive check (directive item 1): CONFIRMED** — see Executive Verdict. The comment
  block above the test explicitly documents the three-part guardrail (missing-member -> compile
  error since every member is named; renumbered member -> `EXPECT_EQ` failure; added/removed
  member -> `static_assert` failure) and states the table was verified name-for-name and
  value-for-value against FNA's real `Keys.cs`.
- **KeyboardState GetHashCode documentation check (directive item 2): NO ISSUE FOUND.**
  `GetHashCodeMatchesFNAWordXorFormula` asserts an exact numeric hash (`0x3` for
  `{Keys::A, Keys::Space}`), but — unlike the audio shard's `GamePadState`/`MouseState` findings —
  this formula is a real, correctly-ported FNA mechanism: FNA's `KeyboardState.GetHashCode()`
  genuinely computes an XOR of eight 32-bit word bitfields indexed by `key>>5`/`key&0x1f`, not
  `base.GetHashCode()`. The test's own comment shows the exact bit math (`Keys::A=65 -> word 2, bit
  1`; `Keys::Space=32 -> word 1, bit 0`; XOR = `0x3`), which correctly reflects real, documented
  FNA behavior — this is not the same stale-documentation pattern found in the audio shard's
  companion classes.
- The out-of-range `Keys` hardening series (P1-014, P2-002 — `GetHashCodeIgnoresOutOfRangeKeysValue`,
  `GetHashCodeIgnoresNegativeKeysValue`, `GetHashCodeIgnoresBoundaryKeysValue256`,
  `AccessorsAreSafeForOutOfRangeKeysValues`, `InitializerListConstructorDropsOutOfRangeKeysValue`,
  `...DropsNegativeAndBoundaryKeysValues`, `UnorderedSetConstructorDropsOutOfRangeKeysValue`) is a
  genuine, well-targeted regression series: it documents and fixes a real prior inconsistency where
  only `GetHashCode()` guarded against out-of-range values while every other accessor
  (`IsKeyDown`/`Equals`/`GetPressedKeys`) disagreed by treating an out-of-range key as pressed —
  each accessor now gets its own dedicated test for the same guard, matching FNA's
  `key>>5` switch-with-no-default semantics exactly.
- `GetPressedKeysIsSortedByAscendingNumericValue`/`GetPressedKeysHasNoDuplicateWhenSameKeyGivenTwice`
  correctly test both the ordering contract and the single-bit-per-key dedup semantics.

## Detailed Findings
None.

## Cross-File Observations
The file's own header comment candidly documents a real, deliberate test-coverage gap: scancode
mode (`FNA_KEYBOARD_USE_SCANCODES`) cannot be exercised in this shared test binary because the mode
is cached in a function-local static the first time any key-conversion path runs, and some earlier
test in the same binary has already forced non-scancode mode by the time this file's tests run —
it states this was separately verified via a dedicated single-purpose process (not committed as a
gtest case) rather than silently leaving the gap unexplained.

## Missing or Weak Tests
None identified for the covered public surface. The scancode-mode gap is explained, not silent.

## Positive Findings
The 160-entry Keys parity table with its three-layer guardrail (compile-time size check,
per-value equality, duplicate detection) is an exemplary implementation of exhaustive enum-value
testing — clearly superior to a sampling approach and directly resolves this audit's specific
concern about whether the Keys parity claim is genuinely exhaustive.

## Final Assessment
No findings. Both directive-specified checks for this file are resolved: the Keys exhaustive-value
assertion is confirmed complete and methodologically sound, and this file's own GetHashCode exact-
value test does NOT reinforce a stale/invented formula (KeyboardState's real FNA formula is
correctly implemented and documented here, unlike the GamePadState/MouseState case flagged
elsewhere in this audit).
