# Audit: src/Microsoft/Xna/Framework/Input/KeyboardState.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/Input/KeyboardState.cpp`
- Audit status: AUDITED (full read, 128 lines)
- Subsystem: `xna-input` shard
- File type: C++ implementation
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Input/KeyboardState.cs` (`AddPressedKey`/
  `InternalGetKey` lines 195-234, `GetHashCode` lines 264-267 — verified matching)
- Main related tests: not independently located in this pass

## Purpose
Implements key-pressed storage (`std::unordered_set<Keys>`), `GetPressedKeys()`'s FNA-matching
ascending-order sort, and the 8x32-bit-bitfield-reconstructing `GetHashCode()`.

## Executive Verdict
Correct, and a strong example of reasoning precisely about an out-of-range edge case. FNA's real
`AddPressedKey`/`InternalGetKey` address an 8-word x 32-bit (256-slot) bitfield via `((int)key)>>5`/
`((int)key)&0x1f` with a `switch` covering only cases 0..7 — meaning any `Keys` value outside
0..255 (including a negative value, which wraps to a huge unsigned once cast) is silently dropped
and can never become "pressed." This port reproduces that exact drop behavior
(`IsWithinKeyBitfieldRange()`), applied consistently across both constructors and `GetHashCode()`.
`GetPressedKeys()` correctly re-sorts its `unordered_set`-backed storage into ascending numeric
order to match FNA's bitfield-scan-order output, since the underlying container's iteration order
would otherwise be unspecified.

## Checklist Results
No issues found.

## Detailed Findings
None.

## Cross-File Observations
None beyond the paired `.hpp` report.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
The out-of-range-key silent-drop behavior and the ascending-order `GetPressedKeys()` re-sort are
both non-obvious FNA behaviors correctly identified and preserved.

## Final Assessment
No findings.
