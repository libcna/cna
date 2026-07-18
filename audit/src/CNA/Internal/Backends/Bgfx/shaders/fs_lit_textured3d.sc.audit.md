# Audit: src/CNA/Internal/Backends/Bgfx/shaders/fs_lit_textured3d.sc

## Metadata

- Source file: `src/CNA/Internal/Backends/Bgfx/shaders/fs_lit_textured3d.sc`
- Audit status: AUDITED
- Subsystem: `backend-bgfx` shard
- File type: BGFX shading language (.sc) source
- XNA/FNA relevance: BasicEffect fragment stage, per-pixel-lit path
- Graphics backend relevance: compiled by `compile_shaders.py` into `bgfx_shaders.hpp`, consumed by
  `BgfxGraphicsBackend`
- Main related tests: `examples-tests-bgfx` (98 files, already audited via mechanical batch this session)

## Purpose

Full Blinn-Phong lighting: ambient+diffuse+specular with correct light-facing gating, emissive added unscaled, specular added after texture*diffuse scaled by resulting alpha.

## Executive Verdict

**Healthy — correct, FNA-accurate lighting formula.**

## Checklist Results

### API / XNA parity
**Independently verified correct** against FNA's `Lighting.fxh ComputeLights()`: ambient+diffuse summed via a branchless `mix(vec3(1,1,1), lightSum, u_lightingEnabled.x)` lighting-enabled toggle (a nice, GPU-friendly alternative to an `if`/`else` branch), with `u_emissiveColor` added **unscaled** after the tint multiply (line 49) — correctly matching FNA's convention, the exact place `fs_env_map3d.sc`'s sibling gets wrong. Specular correctly added after the texture*diffuse multiply, scaled by resulting alpha.

### C++ correctness / Logic
No issues found.

## Detailed Findings

None.

## Cross-File Observations

Part of the control group proving `fs_env_map3d.sc`'s emissive-remultiply bug is an isolated `EnvironmentMapEffect`-specific mistake, not a directory-wide misunderstanding.

## Missing or Weak Tests

No dedicated per-pixel-lighting specular/emissive test found for this backend specifically.

## Positive Findings

Textbook-correct FNA lighting formula; a nice branchless `mix()`-based lighting-enabled toggle.

## Final Assessment

No defects found; inherits the vertex-shader fog-formula bug as a pass-through consumer only.
