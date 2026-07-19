# Audit: src/Microsoft/Xna/Framework/Input/MouseState.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/Input/MouseState.cpp`
- Audit status: AUDITED (full read, 107 lines)
- Subsystem: `xna-input` shard
- File type: C++ implementation
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Input/MouseState.cs` — `ToString()`
  format verified character-for-character identical (`"[MouseState X={0}, Y={1}, Buttons={2},
  Wheel={3}]"` with the same button-name-append ordering: Left, Right, Middle, XButton1, XButton2)
- Main related tests: not independently located in this pass

## Purpose
Implements both constructors, `Equals`, `GetHashCode` (see the paired `.hpp`'s documentation-accuracy
note), and `ToString()`.

## Executive Verdict
Correct. `ToString()`'s output format was verified character-for-character against FNA's real
implementation, including the exact button-name append order and the `"None"` fallback when no
button is pressed.

## Checklist Results
No new issues beyond the one already recorded against the paired header.

## Detailed Findings
None beyond the one already recorded in `MouseState.hpp.audit.md`.

## Cross-File Observations
None beyond what's noted in the paired `.hpp` report.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
`ToString()`'s exact format match to FNA, verified character-for-character.

## Final Assessment
No new findings in this file; see `include/Microsoft/Xna/Framework/Input/MouseState.hpp.audit.md`
for the one LOW documentation-accuracy note whose implementation lives here.
