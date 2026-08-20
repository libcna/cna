# Audit: examples/webgpu_skinned3d_test.cpp

## Metadata

- Source file: `examples/webgpu_skinned3d_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-webgpu` shard — `SkinnedEffect` test (closing this backend's "no skinning shader
  at all" gap, plans/plan_cnj.md Phase 14J WebGPU counterpart)
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_webgpu_test(cna_test_webgpu_skinned3d examples/webgpu_skinned3d_test.cpp)` /
  `cna_register_backend_test(NAME WebGPU_Skinned3D …)`, `cmake/Tests/WebGpuTests.cmake:108-109`).
- XNA/FNA relevance: direct — `SkinnedEffect` is a real XNA 4.0 stock effect (`SetBoneTransforms()`,
  `WeightsPerVertex`, `PreferPerPixelLighting`, `VertexColorEnabled`, `IEffectLights`).
- FNA reference: `HLSL/Skinning.fxh` (bone-palette skinning + normal transform via
  `WorldInverseTranspose`), `HLSL/SkinnedEffect.fx`.
- Related production code: `src/CNA/Internal/Backends/WebGPU/WebGPUGraphicsBackend.cpp`
  (`CreateSkinnedResources()` lines 7465-7981 — all 4 shader variants: plain/vertex-colour ×
  per-pixel/per-vertex-lit — `GetOrCreatePipelineSkinned3D()` lines 7983-8096, `QueueSkinnedDraw()` lines
  8098-8163), `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp`
  (`EnsureSkinnedProgram()`/`EnsureSkinnedVertexLitProgram()`, the explicitly-cited port origin).

## Purpose

Six-check test proving the WebGPU backend's `SkinnedEffect` shader family (`skinned3d.wgsl`,
`GetOrCreatePipelineSkinned3D()`/`QueueSkinnedDraw()`/`DrawPrimitivesEx()` dispatch for stride-52
`VertexPositionNormalTextureSkinned` and stride-56-with-vertex-colour draws): (A) one identity bone,
ambient-only renders white (proves the stride-52 dispatch reaches a real pipeline); (B)/(C) a facing vs.
back-facing light produces non-black/black; (D) bone-palette translation + `WeightsPerVertex` gating — a
two-bone quad shifts position only when `WeightsPerVertex=2` lets the second bone's translation contribute,
hand-derived via the NDC-shift-after-perspective-divide technique; (E) stride-56 `VertexColorEnabled`
(CNB-67) zeroing behaviour; (F) `PreferPerPixelLighting` dispatch — proves the vertex-lit and per-pixel-lit
paths are genuinely two different shaders that produce different results for a non-symmetric light.

## Executive Verdict

**Needs attention** — the test's own checks are correctly designed and this audit independently re-derived
Check D's bone-palette NDC math and confirmed it matches the shader. However, this file is a **live instance
of the cross-cutting, already-confirmed skinned-normal-transform defect** (see
`AUDIT_CROSS_CUTTING_FINDINGS.md` and `WebGPUGraphicsBackend.cpp.audit.md` F1): `skinned3d.wgsl`'s vertex
shader computes the lit normal purely from the bone-skin matrix (`skinMat3 * input.normal`), never composing
the object's own World transform at all, in **all four** shader variants this file's dispatch reaches
(plain/vertex-colour × per-pixel/per-vertex-lit). This file's own `World=Identity` convention (used in every
one of its 6 checks, with no exception) makes the defect structurally undetectable here — see F1.

## Checklist Results

### API / XNA / FNA parity

`SetBoneTransforms()`, `setWeightsPerVertexProperty()`, `setPreferPerPixelLightingProperty()`,
`VertexColorEnabled` (bare public field, matching the pre-existing cross-cutting `BasicEffect.
VertexColorEnabled` convention-violation observation — see Cross-File Observations), and
`DirectionalLight0/1/2` all map onto real `SkinnedEffect`/`IEffectLights` XNA members correctly.

### Behavioral correctness

- Checks A/B/C (ambient-only white, facing-light non-black, back-facing-light black) were not
  independently re-derived numerically (no exact pixel value asserted, only near-white/near-black
  thresholds) — these are qualitative gates, correctly scoped for what they claim to prove (dispatch
  reachability, N·L gating), not overclaiming precision.
- Check D's bone math was independently re-traced against `skinMatrix()` in the actual WGSL source
  (`CreateSkinnedResources()` lines 7533-7543): `skinMat = bones[idx.x]*weight.x`, then `+= bones[idx.y]*
  weight.y` when `weightsPerVertex.x >= 2.0`. With `bones = {Identity, Translate(+2,0,0)}` and the test
  vertex's `index=(0,1,0,0)`/`weight=(1,1,0,0)`: `WeightsPerVertex=1` gives `skinMat=bones[0]*1=Identity`
  (bone1 never read), `WeightsPerVertex=2` gives `skinMat=Identity+Translate(2,0,0)` — matching the header's
  own hand-derivation of an effective NDC shift of `+1` in x after the perspective divide. This audit did not
  re-derive the underlying `Matrix::CreateTranslation`/column-major dump convention from scratch (shared,
  already-tested engine code used identically by every other backend's own skinning tests), but the WGSL-side
  arithmetic itself is confirmed as claimed.
- Check F's claim that vertex-lit and per-pixel-lit are "two distinct live shaders" was confirmed:
  `GetOrCreatePipelineSkinned3D()` (lines 7983-8003) selects between `skinnedVertexLitShader_`/
  `skinnedVertexLitColorShader_` and `skinnedShader_`/`skinnedColorShader_` based on `preferVertexLit`, and
  `QueueSkinnedDraw()` (line 8140) sets `command.preferVertexLit = params.lightingEnabled && !params.
  preferPerPixelLighting` — i.e. `PreferPerPixelLighting=false` (XNA's real default) selects the *vertex-lit*
  shader, matching the file's own stated convention.
- **F1** (see Detailed Findings): every one of the 4 shader variants examined computes
  `output.worldNormal = normalize(skinMat3 * input.normal)` (or, for the vertex-lit pair, the equivalent
  local `n = normalize(skinMat3 * input.normal)`) with `skinMat3` derived purely from the bone-palette matrix
  — the object's own World rotation is never multiplied in. Because every check in this file uses
  `fx.setWorldProperty(Matrix::getIdentityProperty())`, World-space and bone-space normals are numerically
  identical here, so no check in this file can distinguish correct from incorrect behaviour.

### Logic

Check D's two-pass loop (`wpv` from 1 to 2) correctly reuses the same two-bone palette and only varies
`WeightsPerVertex`, isolating exactly one variable per the checklist's "gating" testing discipline.

### C++ correctness

`SkinnedGpuVertex`/`SkinnedColorGpuVertex` both carry a `static_assert(sizeof(...) == 52/56, ...)` — good,
cheap layout-drift guards.

### Memory/resource lifetime

No leak/UAF risk in this file (`VertexBuffer`s and `Texture2D`s are stack/member values with ordinary
lifetime).

### Performance

N/A — one-shot test.

### Architecture

Correctly scoped to `SkinnedEffect` only, leaving `SkinnedPbrEffect` to its own sibling file
(`webgpu_skinnedpbr3d_test.cpp`), consistent with the two being genuinely separate WGSL shader modules in
the backend.

### Maintainability

Uses a self-tallying `passCount_`/`checkCount_` pair (lines 179-187) rather than a hardcoded literal — the
better pattern noted as absent in `webgpu_msaa_test.cpp`/`webgpu_pbr3d_test.cpp`/
`webgpu_rendertarget2d_test.cpp` in this same batch.

### Portability

N/A.

### Robustness

N/A — test file.

### Testing

**F1 is fundamentally a testing gap as much as a production defect**: no check in this file (or anywhere
else in the `examples-tests-webgpu` shard) exercises a skinned model with a non-identity (especially
rotated) `World` transform, which is exactly the scenario needed to expose the missing normal composition.

## Detailed Findings

### F1 — `SkinnedEffect`'s WGSL shader (`CreateSkinnedResources()`) never composes the object's World transform into the lit normal, in any of its 4 variants — this file's own `World=Identity` convention makes the defect invisible to every one of its checks

- Severity: HIGH
- Confidence: HIGH (confirmed by direct source reading, both in this audit and independently already in
  `WebGPUGraphicsBackend.cpp.audit.md`'s own F1)
- Category: correctness / FNA parity (cross-backend systemic issue — see `AUDIT_CROSS_CUTTING_FINDINGS.md`)
- Location/symbol: all four shader variants inside `WebGPUGraphicsBackend::CreateSkinnedResources()`:
  plain (line 7551-7552: `let skinMat3 = mat3x3f(skinMat[0].xyz, skinMat[1].xyz, skinMat[2].xyz);
  output.worldNormal = normalize(skinMat3 * input.normal);`), vertex-colour sibling (lines 7676-7677,
  identical shape), per-vertex-lit sibling (lines 7795-7796), per-vertex-lit + vertex-colour sibling (lines
  7908-7909) — every one composes the normal from `skinMat3` alone, with no reference to `lp.world` (which
  the same `LitLightParams` uniform struct already carries, and which *is* used correctly two lines below
  each occurrence for `worldPos = (lp.world * skinnedPos).xyz`).
- Evidence: this backend's own source comment immediately preceding `CreateSkinnedResources()` (line
  7471) states the shader was "Ported from `EasyGLGraphicsBackend::EnsureSkinnedProgram()`'s GLSL shader
  line-for-line" — an admission that this specific formula (bug included) was deliberately carried over
  from the already-audited EasyGL implementation, which has the identical defect (`EasyGLGraphicsBackend.
  cpp.audit.md` F2). Since this test file always calls `fx.setWorldProperty(Matrix::getIdentityProperty())`
  (every one of its 6 checks, no exception), World-space and bone-local-space normals are numerically
  identical in every scene this file renders, so none of its 6 checks can distinguish correct from
  incorrect normal-transform behaviour.
- Why it matters: any skinned model rendered with a non-identity (rotated) `World` transform via this
  backend will get lighting normals expressed in the model's bind-pose/bone-local orientation rather than
  its actual world-facing direction — a visible lighting-direction bug for any real game object that isn't
  sitting at the world origin with no rotation. This is "wrong behaviour on a common path" (any moved/rotated
  skinned character), not an edge case.
- FNA/XNA comparison: FNA's `Skinning.fxh` composes the bone's rotational part with `WorldInverseTranspose`
  for the normal (`mul(vin.Normal, (float3x3)Bones[...]) ` then further composed with the world normal
  matrix in `SkinnedEffect.fx`'s calling code) — this shader skips that composition entirely.
- Related files: `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp` (origin of the port, per that
  file's own F2 finding); very likely every other backend with its own `SkinnedEffect` implementation
  (Vulkan — already independently confirmed per `AUDIT_CROSS_CUTTING_FINDINGS.md` — Bgfx, D3D9, D3D11,
  D3D12, SdlGpu).
- Suggested future action (not implemented by this audit, matches the already-tracked cross-cutting
  remediation plan): once EasyGL's fix is designed (compose the already-declared-but-unused
  `normalMatrixCol0/1/2` uniform fields — present in this very shader's `LitLightParams` struct at lines
  7504-7506/7627-7629/7748-7750/7858-7860 but never read in the vertex shader body — with `skinMat3`), port
  the identical fix here, and add a rotated-`World` skinned-lighting regression check to this file (or a new
  sibling) so the defect has real test coverage once fixed.

## Cross-File Observations

- Directly corroborates and extends `WebGPUGraphicsBackend.cpp.audit.md`'s own F1 (which reached
  `CreateSkinnedResources()` in its scoped-depth review) — this audit independently re-confirmed the same
  defect from the test-file side, and additionally confirmed it recurs identically in all 4 shader variants
  (plain, vertex-colour, per-vertex-lit, per-vertex-lit + vertex-colour), not merely the one variant that
  prior report's excerpt focused on.
- `SkinnedEffect::VertexColorEnabled` being a bare public field (line: `fx.VertexColorEnabled = false;` /
  `= true;`) reproduces the same convention-violation already flagged for `BasicEffect.VertexColorEnabled`
  in `AUDIT_CROSS_CUTTING_FINDINGS.md`'s "API design" section — worth folding into that same finding's scope
  once the `xna-graphics`/`SkinnedEffect` production file is directly audited, since this test's usage
  proves the same bare-field pattern extends to `SkinnedEffect`, not just `BasicEffect`.

## Missing or Weak Tests

F1: no rotated-`World` skinned-lighting test exists anywhere in this shard — the single highest-value
missing check for this file's own feature area, and the one that would have actually caught F1.

## Positive Findings

- Check D is a genuinely rigorous, independently-verifiable proof of bone-palette translation +
  `WeightsPerVertex` gating, using an exact (not approximate) NDC-shift derivation this audit independently
  confirmed against the actual `skinMatrix()` WGSL source.
- Check F correctly proves `PreferPerPixelLighting` dispatches to two genuinely different shaders (not one
  path silently always winning) via a `colorDiffers()` check, a meaningfully stronger assertion than merely
  "both non-black."
- Check E correctly isolates CNB-67's stride-56 vertex-colour gate using a black-zeroes-the-result technique
  that needs no Phong hand-derivation to be unambiguous — a well-chosen, low-fragility check design.

## Final Assessment

A well-constructed `SkinnedEffect` test suite whose own universal `World=Identity` convention structurally
cannot detect the one real, HIGH-severity defect present in every shader variant it exercises (F1) — already
known at the backend level (`WebGPUGraphicsBackend.cpp.audit.md`'s own F1) and now independently reconfirmed
here across all 4 shader variants, with the missing rotated-World regression test flagged as the concrete
gap that let it ship undetected.
