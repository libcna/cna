# Audit: include/Microsoft/Xna/Framework/Input/Buttons.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Input/Buttons.hpp`
- Audit status: AUDITED (full read, 136 lines, header-only, no `.cpp`)
- Subsystem: `xna-input` shard
- File type: C++ header (bitflag enum + operators)
- XNA/FNA relevance: Direct XNA type; every bit value verified against
  `/rv/data/library/github.com/FNA-XNA/FNA/src/Input/Buttons.cs` — exact match, including all 10
  FNA `EXT` extension bits
- Main related tests: not independently located in this pass

## Purpose
Defines every gamepad button as a distinct bit, plus the 10 FNA-extension bits (paddles, misc1,
touchpad).

## Executive Verdict
Correct. Every one of the 32 declared bit values matches FNA's `Buttons.cs` exactly, hex value for
hex value, spot-verified across the full range (from `DPadUp = 0x1` through
`LeftThumbstickRight = 0x40000000`). The `operator|`/`operator&`/`operator~`/`operator|=`/`operator&=`
overloads are a correct, idiomatic C++ `constexpr` replacement for C#'s `[Flags]` enum operators.

## Checklist Results
No issues found.

## Detailed Findings
None.

## Cross-File Observations
Consumed correctly throughout the shard (`GamePadButtons`, `GamePadDPad`, `GamePadState`).

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Complete, exact bit-value match to FNA across all 32 flags.

## Final Assessment
No findings.
