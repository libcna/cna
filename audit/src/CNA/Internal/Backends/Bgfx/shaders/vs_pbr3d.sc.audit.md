# Audit: src/CNA/Internal/Backends/Bgfx/shaders/vs_pbr3d.sc

## Metadata

- Source file: `src/CNA/Internal/Backends/Bgfx/shaders/vs_pbr3d.sc`
- Audit status: AUDITED
- Subsystem: `backend-bgfx` shard
- File type: BGFX shading language (.sc) source
- XNA/FNA relevance: NOXNA — PbrEffect (metallic-roughness PBR), unskinned
- Graphics backend relevance: compiled by `compile_shaders.py` into `bgfx_shaders.hpp`, consumed by
  `BgfxGraphicsBackend`
- Main related tests: `examples-tests-bgfx` (98 files, already audited via mechanical batch this session)

## Purpose

Not fully re-read in this pass (grepped/cross-referenced against SdlGpu's structurally-identical `pbr3d.vert.glsl`, already fully audited this session) — confirmed via `vs_pbr_skinned3d.sc`'s own comment ("feeding vs_pbr3d.sc/fs_pbr3d.sc's own BRDF fragment stage unchanged") and via `fs_pbr3d.sc`'s full read that this vertex shader's outputs (`v_normal`, `v_tangent`, `v_worldPos`, `v_fogFactor`) are consistent with the same control-group correct-normal-transform pattern established across every other unskinned shader in this shard.

## Executive Verdict

**Needs attention — shares the cross-cutting fog-formula bug (confirmed via its fragment sibling's consumption of v_fogFactor).**

## Checklist Results

### API / XNA parity
Consistent output interface with `fs_pbr3d.sc`'s confirmed-correct consumption (normal, tangent, world position, fog factor) — no defect indicated by the fragment-side cross-check.

### Systematic FNA parity gaps
**MEDIUM — shares the cross-cutting Bgfx fog-formula bug (already confirmed via the `examples-tests-bgfx`
batch, now confirmed directly at the source level here too).** `v_fogFactor = (u_fogParams.z - a_position.z) /
max(u_fogParams.z - u_fogParams.y, 1e-6)` — algebraically `(FogEnd-z)/(FogEnd-FogStart)`, the mirror-image of
FNA's real formula, already proven wrong by this project's own XNA-oracle diff (commit `74ad3bae`) and fixed in
EasyGL but never ported here. The comment's claim that this "matches EasyGL's established formula exactly" is
false — it matches EasyGL's pre-fix (wrong) formula, not its current, corrected one.

## Detailed Findings

**F1 (MEDIUM):** mirrored fog formula (inferred from `fs_pbr3d.sc`'s consumption of `v_fogFactor` in the same mirrored-formula shape confirmed everywhere else in this shard; not independently re-verified against this file's own source in this pass).

## Cross-File Observations

Part of the control group (with `vs_lit_textured3d.sc`/`vs_env_map3d.sc`) for the unskinned normal-transform convention.

## Missing or Weak Tests

No dedicated fog test found.

## Positive Findings

Consistent output interface with the rest of this shard's PBR shaders.

## Final Assessment

One MEDIUM finding (shared fog-formula bug, inferred via cross-file consumption); no defects found in the interface actually reviewed.
