# Audit: include/Microsoft/Xna/Framework/Input/MouseState.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Input/MouseState.hpp`
- Audit status: AUDITED (full read, 161 lines)
- Subsystem: `xna-input` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Input/MouseState.cs`
- Main related tests: not independently located in this pass

## Purpose
Represents cursor position, 5 button states, and cumulative scroll wheel value; adds a NOXNA
horizontal-scroll-wheel extension field.

## Executive Verdict
Needs the same minor documentation-accuracy note already recorded for `GamePadState` (not a
functional defect — see that report for the full explanation). `getHorizontalScrollWheelValueEXTProperty()`'s
doc comment (lines 65-73) is a good example of a correctly-scoped extension: explicitly excluded
from `Equals`/`GetHashCode` "so those stay byte-identical to FNA," which is verified true in the
paired `.cpp`.

## Checklist Results

### LOW (documentation accuracy only, not functional): `GetHashCode()`'s comment implies a preserved FNA formula that doesn't exist
Same situation as `GamePadState` (audited separately, full explanation there): FNA's real
`MouseState.GetHashCode()` is `base.GetHashCode()`, not a portable formula; the custom
`x_ ^ (y_*31) ^ (scrollWheelValue_*17)` formula here is a necessary CNA invention, correctly
satisfying `GetHashCode()`'s actual contract (Equals-consistency) but not literally "preserving" an
FNA original the way the comment's framing implies.

## Detailed Findings
1. **[LOW, documentation-only] `GetHashCode()`'s comment framing implies FNA has a portable formula
   being faithfully preserved, when FNA's real implementation is an opaque `base.GetHashCode()`** —
   implementation in the paired `.cpp`; cf. FNA `MouseState.cs` (`GetHashCode` returns
   `base.GetHashCode()`).

## Cross-File Observations
Same pattern as `GamePadState.hpp` (audited separately) — see that report's Cross-File Observations
for the contrast against the shard's several genuinely-portable-formula types.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
The horizontal-scroll-wheel EXT field's exclusion from `Equals`/`GetHashCode`/(implicitly)
`ToString` to stay FNA-frozen is correctly designed and verified.

## Final Assessment
One LOW, documentation-accuracy-only finding with no functional consequence (same pattern as
`GamePadState.hpp`).
