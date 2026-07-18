# Audit: src/CNA/Internal/Backends/Bgfx/shaders/vs_textured3d.sc

## Metadata

- Source file: `src/CNA/Internal/Backends/Bgfx/shaders/vs_textured3d.sc`
- Audit status: AUDITED
- Subsystem: `backend-bgfx` shard
- File type: BGFX shading language (.sc) source
- XNA/FNA relevance: BasicEffect vertex stage, VertexPositionTexture (textured, unlit)
- Graphics backend relevance: compiled by `compile_shaders.py` into `bgfx_shaders.hpp`, consumed by
  `BgfxGraphicsBackend`
- Main related tests: `examples-tests-bgfx` (98 files, already audited via mechanical batch this session)

## Purpose

Transforms position, forwards UV, sets v_color0 = DiffuseColor.

## Executive Verdict

**Needs attention — correct core logic; shares the cross-cutting fog-formula bug.**

## Checklist Results

### API / XNA parity
Correct MVP transform and UV/tint forwarding.

### Systematic FNA parity gaps
**MEDIUM — shares the cross-cutting Bgfx fog-formula bug (already confirmed via the `examples-tests-bgfx`
batch, now confirmed directly at the source level here too).** `v_fogFactor = (u_fogParams.z - a_position.z) /
max(u_fogParams.z - u_fogParams.y, 1e-6)` — algebraically `(FogEnd-z)/(FogEnd-FogStart)`, the mirror-image of
FNA's real formula, already proven wrong by this project's own XNA-oracle diff (commit `74ad3bae`) and fixed in
EasyGL but never ported here. The comment's claim that this "matches EasyGL's established formula exactly" is
false — it matches EasyGL's pre-fix (wrong) formula, not its current, corrected one.

## Detailed Findings

**F1 (MEDIUM):** mirrored fog formula.

## Cross-File Observations

Consistent with `vs_colored3d.sc`'s own conventions.

## Missing or Weak Tests

No dedicated test found.

## Positive Findings

Correct, minimal implementation.

## Final Assessment

One MEDIUM finding (shared fog-formula bug); otherwise correct.
