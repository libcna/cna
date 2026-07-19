# Audit: tools/xna-oracle/scenes/skinned_quad.scene

## Metadata
- Source file: `tools/xna-oracle/scenes/skinned_quad.scene`
- Audit status: AUDITED (full read)
- Subsystem: `tools-xna-oracle` shard
- File type: documentation/data (declarative scene fixture)
- XNA/FNA relevance: `SkinnedEffect` baseline scene, single-bone case
- Main related tests: consumed by `Oracle.cs`/`CnaOracleRender.cpp`

## Purpose
Exercises `SkinnedEffect`'s bone-weighted vertex transform with a single active bone/weight pair.

## Executive Verdict
Correct as currently written, with real documented history: this scene previously omitted
`weightspervertex`, so it was actually exercising the `FourBones` shader-permutation bucket the
whole time (real XNA's `SkinnedEffect` constructor defaults `WeightsPerVertex=4`), despite its
header comment claiming "single-bone." This was harmless in practice only because the single
`(boneindex, boneweight)` pair the scene provides leaves `weights[1..3]=0`, making the `FourBones`
and `OneBone` shader buckets numerically identical for this specific vertex data. Verified this
scene's current content includes an explicit `weightspervertex=1` line, confirming the documented
fix is present.

## Checklist Results
- `weightspervertex=1` is present and explicit in current content.
- All other keys match `README.md`'s documented table (bone matrices, weight/index values).
- Confirmed pixel-perfect per `README.md`'s status log.

## Detailed Findings
None currently open. (Historical: see Executive Verdict — already fixed.)

## Cross-File Observations
Complements `skinned_twobone_quad.scene` and `skinned_fourbone_quad.scene`, together covering all 3
`WeightsPerVertex` values (1/2/4) `README.md` claims complete.

## Missing or Weak Tests
N/A — data fixture.

## Positive Findings
Same positive transparency pattern as `envmap_quad.scene`'s history: a self-inconsistency was found
and fixed rather than left unnoticed.

## Final Assessment
No open findings. Historical fixture-documentation bug already fixed and verified present in
current content.
