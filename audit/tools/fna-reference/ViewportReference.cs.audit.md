# Audit: tools/fna-reference/ViewportReference.cs

## Metadata
- Source file: `tools/fna-reference/ViewportReference.cs` (75 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tools-fna-reference` shard
- File type: C# tool (Task 476, part of the `FnaReference` console app)
- XNA/FNA relevance: generates authoritative FNA reference data for `Viewport.Project`/`Unproject`
  using the real compiled FNA.dll
- Main related tests: complements Phase 40's Viewport/DisplayMode/adapter coverage; consumed by
  `tools/cna-reference/` + `scripts/compare-fna-reference.py`

## Purpose
Dumps `Viewport.Project`/`Unproject` reference values for 5 cases (3 identity-matrix cases, 2
real-camera cases), each verified via a `Project`-then-`Unproject` round-trip self-consistency
check.

## Executive Verdict
Correct. Calls real FNA's `Viewport.Project`/`Unproject` directly (no reimplementation) — the
reference data this generates is authentic FNA ground truth. The round-trip self-consistency check
(`Unproject(Project(source))` should recover `source`) is a genuinely useful design: it doesn't
need any externally-known-good expected value, just `Project`/`Unproject` being real inverses of
one another, which is exactly XNA's own documented contract for the pair.

## Checklist Results
- The 3 identity-matrix cases (`identity_origin`, `identity_topRight`, `identity_bottomLeft`) and
  2 real-camera cases (`camera_worldOrigin`, `camera_offAxis`, using
  `CreatePerspectiveFieldOfView`/`CreateLookAt`) together cover both the degenerate (matrix =
  identity, pure viewport-rect remap) and general cases.
- `roundTripError` (Euclidean distance between `source` and the round-tripped point) is computed
  and included in the JSON output for every case — lets a consumer set an appropriate
  floating-point tolerance rather than requiring an exact match.
- This file's own top comment (lines 12-17) explicitly notes the identity-matrix formula was
  "verified by hand against the formula in `Viewport.cs`'s own `Project()` body before trusting the
  numbers" — a good discipline (don't blindly trust the harness's own first output without an
  independent sanity check).

## Detailed Findings
None.

## Cross-File Observations
Directly corroborates this session's own `xna-graphics` shard audit finding that CNA's
`Viewport::Project`/`Unproject` "match FNA term-for-term" — this file is (part of) the actual
reference-generation mechanism that finding's cross-check would have used.

## Missing or Weak Tests
Not independently located in this pass; the 5 cases are non-exhaustive (no off-screen/negative-
viewport-coordinate case, no non-square aspect-ratio variation beyond the one camera setup used)
but reasonable for a reference-generation tool rather than an exhaustive test suite.

## Positive Findings
The round-trip self-consistency design is a strong methodological choice — it validates
`Project`/`Unproject` against their own documented mathematical relationship rather than requiring
a separately-derived expected value that could itself be wrong.

## Final Assessment
No findings.
