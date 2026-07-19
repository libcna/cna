# Audit: include/Microsoft/Xna/Framework/Input/GamePadDPad.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Input/GamePadDPad.hpp`
- Audit status: AUDITED (full read, 98 lines)
- Subsystem: `xna-input` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Input/GamePadDPad.cs` (read in full)
- Main related tests: not independently located in this pass

## Purpose
Represents the state of the four directional-pad buttons.

## Executive Verdict
Correct. Constructor shape, `FromButtonArray()`, `Equals`/`GetHashCode`/operators all match FNA
exactly (verified in the paired `.cpp`, including the exact `1/2/4/8`-weighted `GetHashCode()`
formula, which FNA genuinely has as a real, portable formula — not an opaque `base.GetHashCode()`
case like a few sibling types elsewhere in this shard).

## Checklist Results
No issues found.

## Detailed Findings
None.

## Cross-File Observations
`FromButtonArray()` is called by `GamePadState`'s public 5-arg constructor and by
`GamePad::GetState()` (both audited separately), matching FNA's own "used by GamePadState public
constructor, DO NOT USE" internal-use comment.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Exact match to FNA including the hash formula.

## Final Assessment
No findings.
