# Audit: src/CNA/Internal/Graphics/DxtUtil.cpp

## Metadata

- Source file: `src/CNA/Internal/Graphics/DxtUtil.cpp`
- Audit status: AUDITED
- Subsystem: `cna-internal-core` shard
- File type: C++ implementation
- XNA/FNA relevance: internal implementation detail behind XNA-facing content/text APIs
- Graphics backend relevance: see purpose
- Main related tests: see Missing or Weak Tests

## Purpose

Implements DXT1/DXT3/DXT5 block decompression: RGB565->RGB888 expansion, 4-color/3-color-plus-transparency DXT1 modes, 4-bit explicit DXT3 alpha, and 6/8-value interpolated DXT5 alpha.

## Executive Verdict

Healthy — independently verified bit-for-bit correct against the standard DXT1/3/5 decompression algorithm.

## Checklist Results

### Behavioral correctness / C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness
**Every decompression formula independently verified against the well-known reference DXT algorithm**: `ConvertRgb565ToRgb888`'s bit-replication expansion formula is the standard reference form; DXT1's `c0 > c1` (4-color) vs. `c0 <= c1` (3-color + punch-through transparency) branching is correct per spec; DXT3's always-4-color interpolation (never punch-through, since alpha is explicit) is correct; DXT5's 8-value (`alpha0 > alpha1`) and 6-value-plus-0/255 (`alpha0 <= alpha1`) alpha interpolation formulas were checked term-by-term against the reference weights (e.g. index 2 under the 8-value mode correctly computes `6/7*a0 + 1/7*a1`) and match exactly. **Genuinely defensive bounds checking**: each `Decompress*()` entry point computes the exact required byte count up front and throws `std::out_of_range` with a clear message before any block read, explicitly guarding against a truncated or adversarial `.xnb` whose declared byte count doesn't match its own width/height — the low-level `Read8/16/32` helpers themselves do not bounds-check, so this upfront guard is the only protection against an out-of-bounds read, and it's present on all 3 entry points.

### Testing
Not independently located in this pass.

## Detailed Findings

**Every decompression formula independently verified against the well-known reference DXT algorithm**: `ConvertRgb565ToRgb888`'s bit-replication expansion formula is the standard reference form; DXT1's `c0 > c1` (4-color) vs. `c0 <= c1` (3-color + punch-through transparency) branching is correct per spec; DXT3's always-4-color interpolation (never punch-through, since alpha is explicit) is correct; DXT5's 8-value (`alpha0 > alpha1`) and 6-value-plus-0/255 (`alpha0 <= alpha1`) alpha interpolation formulas were checked term-by-term against the reference weights (e.g. index 2 under the 8-value mode correctly computes `6/7*a0 + 1/7*a1`) and match exactly. **Genuinely defensive bounds checking**: each `Decompress*()` entry point computes the exact required byte count up front and throws `std::out_of_range` with a clear message before any block read, explicitly guarding against a truncated or adversarial `.xnb` whose declared byte count doesn't match its own width/height — the low-level `Read8/16/32` helpers themselves do not bounds-check, so this upfront guard is the only protection against an out-of-bounds read, and it's present on all 3 entry points.

## Cross-File Observations

None.

## Missing or Weak Tests

Not independently located in this pass.

## Positive Findings

Bit-for-bit verified correct against the reference DXT1/3/5 decompression algorithm; genuinely defensive upfront bounds-checking against truncated/adversarial input, present on every entry point.

## Final Assessment

See findings above.
