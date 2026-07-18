# Audit: src/CNA/Internal/Backends/D3D9/D3D9EffectDraw.cpp

## Metadata

- Source file: `src/CNA/Internal/Backends/D3D9/D3D9EffectDraw.cpp`
- Audit status: AUDITED
- Subsystem: `backend-d3d9` shard
- File type: C++ implementation, 1163 lines (fully read)
- XNA/FNA relevance: real draw dispatch for all 6 vendored XNA Stock Effects (BasicEffect, AlphaTestEffect,
  DualTextureEffect, EnvironmentMapEffect, SkinnedEffect, SpriteEffect)
- Graphics backend relevance: the central stock-effect draw-dispatch file for this backend
- Main related tests: `examples-tests-d3d9` (already audited via mechanical batch earlier this session)

## Purpose

Implements `ComputeFogVectorEXT()` (the real FNA `EffectHelpers.SetFogVector` port), `ComputeOneLightEXT()`,
per-effect `ShaderIndex`-to-register-table dispatch (`GetBasicEffectRegisterTablesEXT()`/
`GetSkinnedEffectRegisterTablesEXT()`), and the real constant-upload + draw-call sequence for every vendored
stock effect.

## Executive Verdict

**Needs attention — this file is the origin of a significant, precisely-mechanized cross-cutting finding
(the "object-space-only fog" defect's true nature), and confirms 2 genuine positive architectural results.**

## Checklist Results

### Confirmed correct: `ComputeFogVectorEXT()` is a faithful FNA port
Line 149: builds a per-vertex dot-product fog vector from the combined `World*View` matrix's own Z-row/column
elements (`worldView.M13/M23/M33/M43`, scaled by `1/(fogStart-fogEnd)`) — a materially more sophisticated
and CORRECT mechanism than the simple scalar `(z+FogEnd)/(FogEnd-FogStart)` formula this backend's own
3 CNA-custom shaders use with raw, untransformed local-space Z (see those files' own reports and
`AUDIT_CROSS_CUTTING_FINDINGS.md`'s refined writeup). Correctly handles the `fogStart==fogEnd` degenerate case
(forces 100% fogged, matching FNA's own `SetFogVector`) and the fog-disabled case (returns an all-zero vector,
which — since `dot(position, 0)` is always 0 — already produces "no fog" with no extra branching needed at
any call site).

### Confirmed correct: `GetSkinnedEffectRegisterTablesEXT()` dispatches to the real, vendored SkinnedEffect.fx bytecode
This means the ambient/emissive-dropped-for-skinned-models bug family (confirmed in Vulkan and, more narrowly,
D3D11/D3D12) **cannot exist for this backend's stock `SkinnedEffect` path at all** — the shader itself is
Microsoft's own untouched, byte-identical compiled bytecode, not a CNA reimplementation, so there is no CNA
consumption code that could drop a field. This is the one backend where the bug family is structurally
impossible for the stock effect, a genuine positive result.

### `ComputeOneLightEXT()`
Correctly derives the "can this frame use the cheaper one-light-bucket shader variant" decision purely from
already-existing `GpuDrawParams` fields (a light with both zero diffuse AND zero specular contributes exactly
zero regardless of whether that zero came from `Enabled=false` or an `Enabled=true` light with literal black
colors) — a provably lossless shader-variant-selection optimization, not a behavior change.

### C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness / Testing
No issues found in the areas read (this file was read in full).

## Detailed Findings

None new beyond the already-recorded/refined cross-cutting fog-mechanism finding this file's own
`ComputeFogVectorEXT()` explains.

## Cross-File Observations

This file's `ComputeFogVectorEXT()` is the source of the sharpened cross-cutting understanding that D3D9's
"object-space-only fog" defect in its CNA-custom shaders is not a case of the same formula fed a wrong input —
it is two structurally different fog algorithms coexisting in one backend, with the vendored path getting real
FNA fidelity for free and the CNA-custom shaders using a simpler, less-accurate substitute. Register-table
dispatch (`GetBasicEffectRegisterTablesEXT()`) was cross-checked directly against `D3D9ShaderRegisters.hpp`'s
own generated content with no mismatch found.

## Missing or Weak Tests

No dedicated test found in this audit specifically comparing fog appearance between a vendored-stock-effect
mesh and a CNA-custom-shader mesh (PBR/skinned-vertex-color) in the same D3D9-rendered scene under camera
rotation — the scenario where the two fog algorithms' divergence would become visible.

## Positive Findings

A faithful, correct port of FNA's real `EffectHelpers.SetFogVector`; structural immunity (not just absence) of
the ambient/emissive skinned-effect bug family for the stock `SkinnedEffect` path, since it consumes
Microsoft's own real compiled bytecode rather than a CNA reimplementation.

## Final Assessment

No new defects; this file is the mechanistic explanation behind an already-recorded cross-cutting finding and
contributes a genuine structural-immunity positive result for the stock `SkinnedEffect` path.
