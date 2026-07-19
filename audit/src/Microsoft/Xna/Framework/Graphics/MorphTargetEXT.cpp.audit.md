# Audit: src/Microsoft/Xna/Framework/Graphics/MorphTargetEXT.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/Graphics/MorphTargetEXT.cpp`
- Audit status: AUDITED (full read, 165 lines)
- Subsystem: `xna-graphics` shard
- File type: C++ implementation
- XNA/FNA relevance: NOXNA extension, no FNA equivalent
- Main related tests: not independently located in this pass

## Purpose
Implements `BlendMorphTargetsEXT` (per-vertex weighted position/normal delta blending),
`SetMorphWeightsEXT` (blend + re-upload), and `EvaluateMorphWeightsEXT` (LINEAR/STEP/CUBICSPLINE
weight-track sampling).

## Executive Verdict
Correct. Independently re-derived and verified the cubic Hermite basis functions used for
CUBICSPLINE weight-track evaluation (`h00 = 2s³-3s²+1`, `h10 = s³-2s²+s`, `h01 = -2s³+3s²`,
`h11 = s³-s²`) — these are the standard Hermite basis, matching the well-known glTF CUBICSPLINE
interpolation formula, applied correctly component-wise per morph-target weight.

## Checklist Results
- `BlendMorphTargetsEXT`'s per-vertex loop correctly skips a zero-weight morph target
  (`if (w == 0.0f) continue;`) as a pure optimization (mathematically a no-op either way, since
  `0 * delta = 0`), not a correctness-affecting shortcut.
- Normal-delta renormalization (lines 70-76) correctly guards against a near-zero-length result
  (`lenSq > 1e-12f`) before computing `1/sqrt`, avoiding a divide-by-zero/NaN for a degenerate
  all-canceling weight combination.
- `EvaluateMorphWeightsEXT` correctly clamps at both track ends before searching for a bracketing
  keyframe pair, mirroring `SkinnedModelEXT.cpp`'s own `SampleTrack` clamping convention.

## Detailed Findings

### LOW — `BlendMorphTargetsEXT`/`SetMorphWeightsEXT` throw raw `std::runtime_error` instead of
`System::ArgumentException`
Both functions' validation throws (`weights.size() != morph.PositionDeltas.size()` at line 21-26,
and the missing-`MorphTargetDataEXT`-tag check at line 86-91) use `std::runtime_error` rather than
this project's own `System::ArgumentException`. Since this file is a pure NOXNA extension with no
FNA equivalent to match, there's no FNA-parity concern here (unlike the `Model.cpp` finding in this
same batch) — this is purely the recurring project-wide exception-type convention, rated LOW since
it's a self-contained internal API with no cross-hierarchy catch-compatibility expectation from
ported XNA game code.

## Cross-File Observations
`EvaluateMorphWeightsEXT`'s CUBICSPLINE evaluation explicitly cross-references
`GltfImportCore::HermiteEvaluate` (the bone-channel equivalent, outside this shard) as sharing the
identical formula — consistent, reused math across the content-import and runtime-playback sides.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
The Hermite basis math is correct, and the normal-renormalization edge case (near-zero-length
result) is handled safely rather than left to produce a NaN.

## Final Assessment
One LOW finding: two raw-`std::`-exception throw sites, consistent with this audit's recurring
exception-type pattern (no FNA-parity implication here, since this is a pure NOXNA extension).
