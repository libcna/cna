# Audit: src/CNA/Internal/Backends/Bgfx/shaders/fs_skinned3d.sc

## Metadata

- Source file: `src/CNA/Internal/Backends/Bgfx/shaders/fs_skinned3d.sc`
- Audit status: AUDITED
- Subsystem: `backend-bgfx` shard
- File type: BGFX shading language (.sc) source
- XNA/FNA relevance: SkinnedEffect fragment stage
- Graphics backend relevance: compiled by `compile_shaders.py` into `bgfx_shaders.hpp`, consumed by
  `BgfxGraphicsBackend`
- Main related tests: `examples-tests-bgfx` (98 files, already audited via mechanical batch this session)

## Purpose

Full Blinn-Phong lighting (ambient+diffuse via a pre-combined emissive+ambient uniform, specular), per-vertex-color multiply after specular, fog mix.

## Executive Verdict

**Healthy — correct lighting-combination math, and a genuinely valuable self-documented bug-then-fix history confirming Ambient/Emissive ARE correctly forwarded.**

## Checklist Results

### Systematic FNA parity gaps — POSITIVE, self-documented resolution
**Confirmed (see `AUDIT_CROSS_CUTTING_FINDINGS.md`): this file's own comment (lines 47-52) documents a real bug-then-fix history unique in this audit for its transparency**: "Task 899: EmissiveColor was a total GPU no-op on Bgfx (`u_emissiveColor` was never even declared here) — found while writing this task's fog test... C++'s `FillGpuDrawParams()` already pre-combines `AmbientLightColor*DiffuseColor` into `emissiveColor` (`u_ambientColor` is never separately populated for `SkinnedEffect`, always 0), matching EasyGL's already-working formula." **This means Bgfx's `SkinnedEffect` correctly receives both Ambient and Emissive contributions today** (via a single pre-combined wire slot, a different but equally-valid mechanism from SdlGpu's separate-slots-both-forwarded approach) — Bgfx does NOT share the Vulkan-specific "ambient/emissive dropped for skinned models" bug.

### Logic
Vertex-color-modulation-after-specular ordering (line 65) independently verified correct and explicitly, accurately cross-referenced against `EnsureSkinnedProgram()`'s identical discipline.

### Systematic FNA parity gaps
**MEDIUM (inherited from the vertex shader) — shares the cross-cutting Bgfx fog-formula bug (already confirmed via the `examples-tests-bgfx`
batch, now confirmed directly at the source level here too).** `v_fogFactor = (u_fogParams.z - a_position.z) /
max(u_fogParams.z - u_fogParams.y, 1e-6)` — algebraically `(FogEnd-z)/(FogEnd-FogStart)`, the mirror-image of
FNA's real formula, already proven wrong by this project's own XNA-oracle diff (commit `74ad3bae`) and fixed in
EasyGL but never ported here. The comment's claim that this "matches EasyGL's established formula exactly" is
false — it matches EasyGL's pre-fix (wrong) formula, not its current, corrected one.

## Detailed Findings

**F1 (MEDIUM):** inherits the vertex-shader's mirrored fog formula (this file's own fog-mix logic is correct in isolation).

## Cross-File Observations

This file's own Task 899 comment is one of the most valuable, self-documented pieces of bug-history evidence in this entire audit — directly confirming a real historical defect AND its current fixed state in the same breath.

## Missing or Weak Tests

No dedicated test found specifically isolating the Ambient+Emissive-forwarding correctness on this backend (though the comment itself references a fog test that was used to discover the original bug).

## Positive Findings

A genuinely transparent, self-documented bug-then-fix history proving `SkinnedEffect`'s Ambient/Emissive terms are correctly forwarded — a real positive finding distinguishing this backend from Vulkan's confirmed gap in the same area. Correct, verified vertex-color-after-specular ordering.

## Final Assessment

No new defects found; inherits the vertex-shader normal-transform and fog-formula bugs as a pass-through consumer, but independently confirms correct Ambient/EmissiveColor forwarding.
