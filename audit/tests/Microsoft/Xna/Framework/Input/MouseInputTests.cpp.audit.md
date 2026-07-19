# Audit: tests/Microsoft/Xna/Framework/Input/MouseInputTests.cpp

## Metadata
- Source file: `tests/Microsoft/Xna/Framework/Input/MouseInputTests.cpp` (953 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-xna-input` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `Microsoft::Xna::Framework::Input::MouseState`/`Mouse`/`MouseCursor`
- Main related tests: N/A (this IS a test file)

## Purpose
Covers `MouseState`'s constructors (default, 8-arg XNA, 9-arg NOXNA/EXT horizontal-wheel), the
NOXNA horizontal-wheel field's deliberate exclusion from `Equals`/`GetHashCode`, equality per-field
isolation, `GetHashCode`, `ToString` formatting, `Mouse`'s static state/position/relative-mode/
cursor API (including letterboxed-renderer coordinate conversion), and `MouseCursor`'s
lifetime/ownership semantics (stock singletons, move construction/assignment, self-move,
`FromTexture2D`).

## Executive Verdict
No MEDIUM+ findings. This file directly and concretely answers this audit's directive-specified
MouseState `GetHashCode()` documentation-mismatch check — see Detailed Findings for the specific
result, which is a clearer instance of the concern than the sibling `GamePadStateTests.cpp` case.

## Checklist Results
- `EightArgConstructorSetsEveryFieldInTheRightSlot`/`NineArgConstructorAlsoSetsHorizontalScrollWheelEXT`
  deliberately alternate `Pressed`/`Released` per field specifically so an accidental parameter-order
  swap would fail the test — a good defensive pattern for a constructor with many same-typed
  parameters.
- `HorizontalScrollWheelEXTIsExcludedFromEqualityAndHash` (N-005) is a well-targeted, correct test
  for a deliberate NOXNA-extension-field exclusion: two states differing only in the EXT field must
  still compare and hash equal, preserving byte-identical `Equals`/`GetHashCode` behavior relative
  to real FNA (which has no such field at all).
- The `SetPositionConvertsLogicalToWindowForLetterboxedRenderer`/
  `SetPositionHandlesLetterboxOffsetNotJustScale` pair correctly distinguishes uniform-scale-only
  conversion from scale-plus-offset conversion (non-square letterboxing), the latter explicitly
  designed to catch a "scale-only, ignores offset" regression (`EXPECT_GT(wx, 60.0f)` directly
  documents what a wrong, offset-ignoring implementation would produce).
- `MoveConstructorTransfersOwnershipAndNullsSource`/`SelfMoveAssignmentLeavesCursorIntact` are
  genuine regression tests for real, previously-fixed defects (task 752's non-nulling move
  constructor; a self-move-assignment UAF via `Dispose()` before reading `other`'s fields).
- `ColorCursorSurvivesSourcePixelBufferDestruction` directly verifies (against real SDL3 behavior,
  not assumption) that `SDL_CreateColorCursor` copies pixel data rather than referencing the source
  buffer — deliberately scribbling and freeing the source buffer afterward to prove no live
  reference remains.

## Detailed Findings
- **[LOW, documentation-nuance, directive-specific check — CONFIRMED PRESENT]**
  `GetHashCodeMatchesFormula` (line ~173) computes its expected value as a literal arithmetic
  expression: `3 ^ (5 * 31) ^ (7 * 17)`, directly matching the state's own `X=3, Y=5,
  ScrollWheelValue=7` constructor arguments — i.e. it hardcodes and asserts the *exact* numeric
  hash value of a specific state via an independently-written formula (`X XOR Y*31 XOR
  ScrollWheel*17`), not merely re-deriving it from a sub-call as `GamePadStateTests.cpp`'s
  analogous test does. This is precisely the pattern this audit's directive flagged as a concern:
  per the already-recorded production-code (LOW-severity) finding, `MouseState`'s real FNA
  `GetHashCode()` is `base.GetHashCode()` (a plain inherited/reference-style hash with no
  X/Y/ScrollWheel-based composition at all), so this specific `X ^ Y*31 ^ ScrollWheel*17` formula
  is a CNA-invented composition, not something ported from FNA. This test's name (`...MatchesFormula`)
  and lack of an FNA-fidelity comment mean it does not itself assert "this matches FNA" in so many
  words, but it does pin the invented formula down as canonical/expected behavior with a
  hardcoded-literal exact-value assertion — the clearest instance in this shard of the exact pattern
  the directive asked to check for. Severity stays LOW (consistent with the underlying production
  finding) because `GetHashCode()`'s only real contractual requirement — internal consistency
  (equal objects produce equal hashes) — is separately and correctly covered by
  `GetHashCodeIsConsistentForEqualStates`, so no functional defect follows from this; the concern is
  purely that a future reader could mistake this hardcoded formula for verified FNA parity.

## Cross-File Observations
Compare directly with `GamePadStateTests.cpp`'s `GetHashCodeMatchesButtonsHashXorPacketFormula`:
that test derives its expected value by calling back into the implementation's own
`getButtonsProperty().GetHashCode()`, which is a weaker but less presumptive pin; this file's
`GetHashCodeMatchesFormula` instead hardcodes a fully independent arithmetic literal, making it the
stronger and more directive-relevant example of the two.

## Missing or Weak Tests
None beyond the nuance noted above.

## Positive Findings
The letterbox-offset test's explicit "if wx≈50 the offset was ignored (scale-only, wrong)" assertion
comment is an excellent practice — documenting the specific wrong value a regression would produce,
not just the correct one.

## Final Assessment
No MEDIUM+ findings. One LOW, directive-specific documentation nuance confirmed: this file's
`GetHashCodeMatchesFormula` test is the clearest instance in the `tests-xna-input` shard of a test
hardcoding an exact numeric hash value for a formula that (per the already-recorded production-code
finding) is not actually FNA's real `GetHashCode()` behavior — worth a documentation
clarification (e.g. a comment noting the formula is CNA's own internally-consistent scheme, not a
ported FNA algorithm) but not a functional defect.
