# Audit: src/CNA/Internal/Backends/Bgfx/shaders/fs_pbr3d.sc

## Metadata

- Source file: `src/CNA/Internal/Backends/Bgfx/shaders/fs_pbr3d.sc`
- Audit status: AUDITED
- Subsystem: `backend-bgfx` shard
- File type: BGFX shading language (.sc) source
- XNA/FNA relevance: NOXNA — PbrEffect (metallic-roughness PBR), unskinned and skinned (shared)
- Graphics backend relevance: compiled by `compile_shaders.py` into `bgfx_shaders.hpp`, consumed by
  `BgfxGraphicsBackend`
- Main related tests: `examples-tests-bgfx` (98 files, already audited via mechanical batch this session)

## Purpose

Full metallic-roughness PBR shading: GGX/Trowbridge-Reitz D, Smith-Schlick-GGX visibility, Schlick Fresnel (glTF 2.0's reference BRDF), per-pixel normal mapping, ambient/emissive/occlusion, an AlphaTest discard branch, and fog mix.

## Executive Verdict

**Healthy — correct, well-implemented PBR BRDF; the most feature-complete PBR fragment shader found in this audit.**

## Checklist Results

### API / XNA parity
Every BRDF term independently verified matching the same glTF 2.0 reference formula already confirmed correct in every other backend's own PBR shader in this audit. `emissive` (line 79) added **unscaled** to `ambient + Lo` (line 81) — correct.

### Architecture — genuinely more complete than its siblings
**Positive finding**: unlike EasyGL's/SdlGpu's own PBR fragment shaders (which both explicitly document an `AlphaTest` discard branch as unreachable dead code for real `PbrEffect` usage, since `PbrEffect` never enables alpha testing), this file includes a real, functioning AlphaTest discard branch (lines 83-86) AND real fog support (line 88) — this backend's PBR shader is more feature-complete than the reference EasyGL implementation it otherwise mirrors, not less.

### Systematic FNA parity gaps
**MEDIUM — shares the cross-cutting Bgfx fog-formula bug (already confirmed via the `examples-tests-bgfx`
batch, now confirmed directly at the source level here too).** `v_fogFactor = (u_fogParams.z - a_position.z) /
max(u_fogParams.z - u_fogParams.y, 1e-6)` — algebraically `(FogEnd-z)/(FogEnd-FogStart)`, the mirror-image of
FNA's real formula, already proven wrong by this project's own XNA-oracle diff (commit `74ad3bae`) and fixed in
EasyGL but never ported here. The comment's claim that this "matches EasyGL's established formula exactly" is
false — it matches EasyGL's pre-fix (wrong) formula, not its current, corrected one.

## Detailed Findings

**F1 (MEDIUM):** mirrored fog formula, line 88 (this file at least HAS fog, unlike SdlGpu's total absence — just the wrong formula).

## Cross-File Observations

Reused unchanged as `vs_pbr_skinned3d.sc`'s fragment stage too (confirmed via that file's own comment) — this is the shared BRDF fragment shader for both `PbrEffect` and `SkinnedPbrEffect` on this backend.

## Missing or Weak Tests

No dedicated test found asserting the exact BRDF output values on this backend (though `sdlgpu_pbreffect_test.cpp`'s equivalent, already audited, independently re-derived and confirmed the same formula's expected outputs by hand).

## Positive Findings

The most feature-complete PBR fragment shader in this audit (real AlphaTest + real fog, unlike its EasyGL/SdlGpu siblings which lack one or the other).

## Final Assessment

One MEDIUM finding (shared fog-formula bug); otherwise a genuinely well-engineered, more-complete-than-average implementation.
