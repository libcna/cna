# Audit: examples/webgpu_skinnedpbr3d_test.cpp

## Metadata

- Source file: `examples/webgpu_skinnedpbr3d_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-webgpu` shard — `SkinnedPbrEffect` test (PBR + skinning combo, plans/plan_cnj.md
  Phase 14J WebGPU counterpart, closes the remaining half of this backend's "no skinning shader at all" gap
  alongside `webgpu_skinned3d_test.cpp`)
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_webgpu_test(cna_test_webgpu_skinnedpbr3d examples/webgpu_skinnedpbr3d_test.cpp)` /
  `cna_register_backend_test(NAME WebGPU_SkinnedPbr3D …)`, `cmake/Tests/WebGpuTests.cmake:114-115`).
- XNA/FNA relevance: `SkinnedPbrEffect` is **NOXNA** (glTF PBR + skinning combination, postdates XNA 4.0 —
  same NOXNA basis as the unskinned `PbrEffect` audited alongside this file). FNA reference: N/A directly;
  the BRDF and skinning composition are each independently checkable against the glTF 2.0 spec and
  `HLSL/Skinning.fxh`'s conventions respectively.
- Related production code: `src/CNA/Internal/Backends/WebGPU/WebGPUGraphicsBackend.cpp`
  (`CreateSkinnedPbrResources()` lines 8292-8497, vertex shader lines 8382-8395,
  `GetOrCreatePipelineSkinnedPbr3D()` lines 8499+, `QueueSkinnedPbrDraw()` lines 8594+), EasyGL's
  `EnsurePbrSkinnedProgram()` (explicitly cited port origin).

## Purpose

Four-check test proving the WebGPU backend's `SkinnedPbrEffect` shader (`skinned_pbr3d.wgsl`,
`GetOrCreatePipelineSkinnedPbr3D()`/`QueueSkinnedPbrDraw()`/`DrawPrimitivesEx()` dispatch for stride-68
`VertexPositionNormalTangentTextureSkinned` draws): (A) one identity bone, ambient-only renders white
(proves the stride-68 dispatch reaches a real pipeline, not a silent fallback to either `colored3d.wgsl`'s
stride-16 layout or the unskinned `pbr3d.wgsl`); (B)/(C) a facing vs. back-facing light produces
non-black/black; (D) bone-palette translation + `WeightsPerVertex` gating, independently re-derived for
*this* shader (not assumed identical to plain `SkinnedEffect`'s, since it is a genuinely separate WGSL
module) using the same NDC-shift-after-perspective-divide technique as `webgpu_skinned3d_test.cpp`'s own
Check D.

## Executive Verdict

**Needs attention** — smaller in scope than `webgpu_skinned3d_test.cpp`'s finding but the same defect class:
`skinned_pbr3d.wgsl`'s vertex shader **does** compose the object's World rotation into the skinned normal
(`worldMat3 * (skinMat3 * input.normal)`), unlike the plain `SkinnedEffect` shader audited alongside this
file — but it uses the **raw** World 3x3 rotation, not the correct `WorldInverseTranspose`, a
self-documented, intentional simplification carried over from the EasyGL reference. This is only wrong for a
non-uniform-scale/sheared World transform (mathematically correct for pure rotation, since a rotation
matrix's inverse-transpose equals itself); this file's `World=Identity` convention across all 4 checks means
neither the correct nor the incorrect case is ever exercised — see F1.

## Checklist Results

### API / XNA / FNA parity

`SetBoneTransforms()`, `setWeightsPerVertexProperty()`, `setRoughnessFactorProperty()`,
`setMetallicFactorProperty()`, `DirectionalLight0/1/2` all map onto real `SkinnedPbrEffect` members.
`ConfigureCommon()`'s helper correctly centralises `World/View/Projection/Texture/Roughness/Metallic/
WeightsPerVertex` across all 4 checks, reducing duplication versus `webgpu_skinned3d_test.cpp`'s more
verbose per-check repetition.

### Behavioral correctness

- Checks A/B/C were not independently re-derived numerically here (this audit already performed the full
  GGX/Smith-Schlick-GGX/Schlick-Fresnel numeric re-derivation for the *unskinned* `pbr3d.wgsl` shader in
  `webgpu_pbr3d_test.cpp`'s own report, confirming the shared `pbrLight()` helper's formula is correct;
  `skinned_pbr3d.wgsl`'s `fs_main`/`pbrLight()` at lines 8397-8452 is textually identical to the unskinned
  shader's, differing only in the vertex stage's skinning/normal-composition, so that re-derivation carries
  over directly).
- Check D's bone math was independently re-traced against this shader's own `skinMatrix()` (lines
  8370-8380, identical structure to `webgpu_skinned3d_test.cpp`'s), confirming the same
  `Identity`→`Identity+Translate(2,0,0)` NDC-shift-by-`+1` derivation applies here too — verified separately
  as the header comment itself states ("Verified independently for THIS shader (not assumed identical to
  plain SkinnedEffect's)"), and this audit's independent trace agrees.
- **F1** (see Detailed Findings): the normal/tangent transform composes `worldMat3` (raw World rotation)
  with `skinMat3` (bone rotation), but `worldMat3` is derived directly from `lp.world`'s upper-left 3x3
  (line 8388: `let worldMat3 = mat3x3f(lp.world[0].xyz, lp.world[1].xyz, lp.world[2].xyz);`), not from the
  precomputed `normalMatrixCol0/1/2` inverse-transpose fields this same `LitLightParams` struct already
  declares (present at lines 8329-8331 but never read in this shader's vertex-stage body) — this is
  mathematically correct only when World has no non-uniform scale/shear (a rotation matrix's own
  inverse-transpose equals itself), and wrong otherwise.

### Logic

Check D's two-pass `wpv` loop structure and two-bone palette are identical in shape to
`webgpu_skinned3d_test.cpp`'s own Check D — appropriate reuse of a proven technique rather than inventing a
new one for a structurally similar shader.

### C++ correctness

`SkinnedPbrGpuVertex` carries a `static_assert(sizeof(...) == 68, ...)` — good layout-drift guard.

### Memory/resource lifetime

No leak/UAF risk (stack/member values with ordinary lifetime).

### Performance

N/A — one-shot test.

### Architecture

Correctly scoped to the PBR+skinning combination only, complementing (not duplicating)
`webgpu_skinned3d_test.cpp` and `webgpu_pbr3d_test.cpp`. `ConfigureCommon()`'s shared-setup helper is a
reasonable, appropriately-scoped reduction of duplication given all 4 checks share most of the same effect
configuration.

### Maintainability

Uses a self-tallying `passCount_`/`checkCount_` pair (lines 130-139), the more drift-resistant pattern noted
as absent in `webgpu_msaa_test.cpp`/`webgpu_pbr3d_test.cpp`/`webgpu_rendertarget2d_test.cpp` in this batch.

### Portability

N/A.

### Robustness

N/A — test file.

### Testing

**F1 is a testing gap as much as a (narrower) production defect**: no check in this file varies `World` away
from identity, so neither the shader's actual correctness under pure rotation (which this audit's analysis
shows would in fact be correct, since raw-rotation and inverse-transpose coincide for orthogonal matrices)
nor its incorrectness under non-uniform scale is ever exercised.

## Detailed Findings

### F1 — `SkinnedPbrEffect`'s WGSL shader composes the skinned normal with the raw World 3x3 rotation instead of the precomputed inverse-transpose normal matrix, a self-documented simplification only wrong for non-uniform-scale/sheared World transforms, and untested in either direction by this file

- Severity: MEDIUM
- Confidence: HIGH (confirmed by direct source reading; the deviation is also self-documented in the
  backend's own header, independently corroborating the same conclusion)
- Category: correctness / FNA-adjacent parity (this effect is NOXNA, but the underlying lighting-math
  convention it is meant to match is still FNA's own `Lighting.fxh`/glTF normal-transform convention)
- Location/symbol: `WebGPUGraphicsBackend::CreateSkinnedPbrResources()` vertex shader, lines 8382-8395:
  `let skinMat3 = mat3x3f(skinMat[0].xyz, skinMat[1].xyz, skinMat[2].xyz); let worldMat3 = mat3x3f(lp.
  world[0].xyz, lp.world[1].xyz, lp.world[2].xyz); output.worldNormal = normalize(worldMat3 * (skinMat3 *
  input.normal)); output.worldTangent = worldMat3 * (skinMat3 * input.tangent.xyz);` — `worldMat3` is the
  *raw* World rotation/scale block, not `WorldInverseTranspose`.
- Evidence: this is **self-documented** in the backend's own comment immediately preceding the shader
  source (lines 8302-8306): "the normal/tangent transform here uses the raw World rotation directly
  (`mat3(uWorld)*(skinMat3*normal)`), NOT the precomputed inverse-transpose normal matrix `pbr3d.wgsl`'s own
  unskinned vertex shader uses — an intentional difference from unskinned PbrEffect, replicated here for
  cross-backend consistency rather than 'fixed'." The `LitLightParams` struct in this very shader still
  declares `normalMatrixCol0/1/2` (lines 8329-8331), populated by the same `FillLitLightUniforms()`/
  `ComputeNormalMatrix3x3()` CPU-side path the *unskinned* `pbr3d.wgsl` correctly reads — but this shader's
  vertex-stage body never reads them.
- Why it matters: for a World transform that is pure translation/rotation/uniform-scale, raw-rotation and
  inverse-transpose coincide (an orthogonal matrix is its own inverse-transpose; uniform scale cancels in
  the normalize() step), so this is silently correct for the common case. It only diverges for a
  non-uniformly-scaled or sheared World matrix — a real but narrower failure surface than the plain
  `SkinnedEffect` shader's defect (`webgpu_skinned3d_test.cpp`'s F1), which composes no World component at
  all and is therefore wrong under plain rotation too. Classified MEDIUM rather than HIGH specifically
  because of this narrower, self-disclosed scope and because non-uniform-scale skinned models are a less
  common authoring case than simply moving/rotating one.
- FNA/XNA comparison: N/A directly (NOXNA effect), but the underlying convention it deviates from is the
  same `WorldInverseTranspose` normal-matrix approach FNA's own `Lighting.fxh`/this same backend's unskinned
  `pbr3d.wgsl` correctly use.
- Related files: `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp`
  (`EnsurePbrSkinnedProgram()`, the explicitly-cited origin of this same intentional simplification);
  `webgpu_pbr3d_test.cpp`'s report (this same batch) documents the *correct* inverse-transpose approach the
  unskinned sibling shader uses, making this file's own shader the direct contrast case.
- Suggested future action (not implemented by this audit): if/when this simplification is revisited, swap
  `worldMat3` for the already-declared-but-unused `normalMatrixCol0/1/2` fields (mirroring the unskinned
  shader exactly), and add a non-uniform-scale-World regression check to prove the fix; until then, this
  finding is best tracked as a documented, intentional scope limitation rather than an unknown bug — which
  it already mostly is, at the source-comment level, just not at the per-file-audit/test level.

## Cross-File Observations

- Directly contrasts with `webgpu_skinned3d_test.cpp`'s F1 (plain `SkinnedEffect`, same batch): that shader
  composes **no** World component into the skinned normal at all (wrong even under pure rotation), whereas
  this shader composes the raw World rotation (correct under pure rotation, wrong only under non-uniform
  scale) — two different severities of the same root defect class, both traceable to the same "ported
  line-for-line from EasyGL" origin, and both invisible to their respective test files' `World=Identity`
  convention.
- Reuses `webgpu_pbr3d_test.cpp`'s `pbrLight()`/`fs_main` BRDF unchanged (textually identical fragment
  shader) — this audit's independent GGX/Fresnel re-derivation for the unskinned test therefore also
  validates this file's fragment-stage correctness by direct code-identity, without needing a second
  from-scratch derivation.

## Missing or Weak Tests

F1: no non-uniform-scale-World (nor even pure-rotation-World) skinned-PBR lighting test exists in this file
or elsewhere in the shard — the concrete gap that leaves this self-documented simplification's actual
correctness boundary (fine under rotation, wrong under non-uniform scale) unverified by any executable test.

## Positive Findings

- The shader's own header comment is a genuinely rare example of a **known, intentional** simplification
  disclosed at the point of definition, in enough detail (naming the exact alternative convention it
  deviates from and why) that this audit could evaluate its actual correctness boundary (rotation: fine;
  non-uniform scale: wrong) rather than treating it as an unknown risk — a positive engineering practice
  worth contrasting with the plain `SkinnedEffect` shader's *undocumented* instance of the same defect class.
- Check D's independent re-verification note ("not assumed identical to plain SkinnedEffect's... since it is
  a genuinely separate WGSL module") reflects good testing discipline — not assuming code-sharing that
  doesn't actually exist between the two skinning shader families.

## Final Assessment

A correctly-scoped, well-tested `SkinnedPbrEffect` suite carrying one self-documented, MEDIUM-severity
normal-transform simplification (raw World rotation instead of inverse-transpose) that is silently correct
for the common rotation/translation case and wrong only for non-uniformly-scaled World transforms — real,
but narrower in practical impact than its plain-`SkinnedEffect` sibling's undocumented, always-wrong
equivalent (`webgpu_skinned3d_test.cpp`'s F1), and, like that finding, currently untested in either
direction by this file's `World=Identity` convention.
