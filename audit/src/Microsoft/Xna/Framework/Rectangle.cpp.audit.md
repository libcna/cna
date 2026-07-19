# Audit: src/Microsoft/Xna/Framework/Rectangle.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/Rectangle.cpp`
- Audit status: AUDITED (full read, 225 lines)
- Subsystem: `xna-framework-core` shard
- File type: C++ implementation
- XNA/FNA relevance: matches real XNA `Rectangle`'s exact containment/intersection semantics
- Main related tests: not independently located in this pass

## Purpose
Implements all `Rectangle` methods.

## Executive Verdict
Healthy -- independently verified correct against known XNA `Rectangle` semantics.

## Checklist Results

### `Contains`/`Intersects`: correct half-open-interval semantics
`Contains` uses `X <= x < X+Width` (half-open interval, matching real XNA -- a point exactly on the right/
bottom edge is NOT contained); `Intersects` uses strict `<` on all four edge comparisons, matching XNA's own
"touching edges don't count as intersecting" semantics exactly.

### `Intersect`/`Union`: correct
`Intersect` correctly short-circuits to `Rectangle(0,0,0,0)` when the rectangles don't actually intersect
(computed via `Intersects()` first, avoiding a would-be-negative-width/height result from the raw min/max
edge math); `Union` correctly computes the smallest enclosing rectangle via min(left/top)/max(right/bottom).

### `GetHashCode()`: XOR-combined, unaffected by the sibling UB pattern
`X ^ Y ^ Width ^ Height` -- no overflow possible, consistent with `Point`'s own safe pattern.

## Detailed Findings
None.

## Cross-File Observations
N/A.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Correct half-open-interval containment/intersection semantics, matching a real and easy-to-get-wrong XNA
detail (edge-touching not counting as intersection/containment).

## Final Assessment
No issues found.
