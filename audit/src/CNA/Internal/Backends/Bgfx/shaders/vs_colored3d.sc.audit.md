# Audit: src/CNA/Internal/Backends/Bgfx/shaders/vs_colored3d.sc

## Metadata

- Source file: `src/CNA/Internal/Backends/Bgfx/shaders/vs_colored3d.sc`
- Audit status: AUDITED
- Subsystem: `backend-bgfx` shard
- File type: BGFX shading language (.sc) source
- XNA/FNA relevance: BasicEffect vertex stage, VertexPositionColor (untextured, unlit)
- Graphics backend relevance: compiled by `compile_shaders.py` into `bgfx_shaders.hpp`, consumed by
  `BgfxGraphicsBackend`
- Main related tests: `examples-tests-bgfx` (98 files, already audited via mechanical batch this session)

## Purpose

Transforms position, mixes vertex color with DiffuseColor when VertexColorEnabled, applies a DepthBias emulation.

## Executive Verdict

**Needs attention — correct core logic; shares the cross-cutting fog-formula bug.**

## Checklist Results

### API / XNA parity
Correct MVP transform and VertexColorEnabled gating.

### Architecture
**Positive finding**: this file's own comment is the canonical explanation (referenced by every sibling shader) of bgfx's `RasterizerState.DepthBias` emulation via a per-draw vertex-shader Z offset (`gl_Position.z += u_depthBias.x * gl_Position.w`) — bgfx has no native polygon-offset mechanism, so this is a genuine, functional workaround, independently confirmed consumed by `ApplyRasterizerState()`'s own `depthBias_` member in the main backend file.

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

Canonical DepthBias-emulation comment referenced by every other 3D vertex shader in this shard.

## Missing or Weak Tests

No dedicated fog test found for this exact formula on this backend (though the cross-cutting fog-formula bug was already independently confirmed via 3 `examples-tests-bgfx` files).

## Positive Findings

Clear, reusable documentation of the DepthBias emulation technique.

## Final Assessment

One MEDIUM finding (shared fog-formula bug); otherwise correct.
