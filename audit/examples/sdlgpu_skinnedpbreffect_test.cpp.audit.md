# Audit: examples/sdlgpu_skinnedpbreffect_test.cpp

## Metadata

- Source file: `examples/sdlgpu_skinnedpbreffect_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlgpu` shard — `SkinnedPbrEffect` (PBR + skinning combo) proof for
  the SDL_GPU backend
- File type: standalone `Game`-subclass executable, CTest-registered (`SdlGpu_SkinnedPbrEffect`,
  `cmake/Tests/SdlGpuTests.cmake:88-90`, `TIMEOUT 60 LABELS "SdlGpu"`)
- XNA/FNA relevance: indirect — `SkinnedPbrEffect`/PBR metallic-roughness is a CNA/glTF-derived
  extension with no FNA/XNA 4.0 equivalent (real XNA has no physically-based-rendering effect
  class); the underlying `SkinnedEffect`-style bone API (`SetBoneTransforms`,
  `setWeightsPerVertexProperty`) it reuses is XNA-shaped.
- FNA reference: N/A for the PBR/BRDF math itself (glTF 2.0 spec, not XNA); FNA's
  `Effect.DirectionalLight0` shape is mirrored for the light-setup API surface used here.
- Related production code: `src/CNA/Internal/Backends/SdlGpu/shaders/pbr_skinned3d.vert.glsl`,
  `pbr3d.vert.glsl` (its own non-skinned sibling), `pbr3d.frag.glsl` (shared, reused unchanged),
  `src/CNA/Internal/Backends/SdlGpu/SdlGpuGraphicsBackend.cpp` (`QueuePbrDraw`'s `skinned` branch,
  lines 3038-3090; `GetOrCreatePipelinePbr3D`, lines ~2770-2810).

## Purpose

Three-check pixel test proving the stride-68 `VertexPositionNormalTangentTextureSkinned` layout,
bone-palette skinning applied to Position/Normal/Tangent, and the shared PBR BRDF fragment stage
(`pbr3d.frag.glsl`) all work end-to-end for `SkinnedPbrEffect`. Reuses
`sdlgpu_pbreffect_test.cpp`'s exact 3-quad scene/camera/light/material setup with a single identity
bind-pose bone — since an identity bind pose is a mathematical no-op for the skin transform, the
non-skinned `PbrEffect` test's own already-derived expected pixel values become this test's oracle
(the file's own header explicitly states this rationale, mirroring
`easygl_skinnedpbreffect_golden_test.cpp`'s identical technique). Correct placement for a combined
PBR+skinning backend feature test.

## Executive Verdict

**Needs attention** — the 3 checks correctly prove end-to-end wiring of the stride-68 vertex
layout and BRDF stage, and this audit independently confirmed the identity-bind-pose-as-oracle
rationale is mathematically sound. However, this audit found a genuine, currently-live defect in
`pbr_skinned3d.vert.glsl`'s normal/tangent transform that this test's identity-`World`,
identity-bone scene structurally cannot detect (F1) — and, notably, this is a *variant* of the
already cross-cutting-confirmed skinned-normal-matrix defect, not the same code shape: unlike the
plain `SkinnedEffect` shaders (which omit the World transform entirely), this PBR-skinned shader
*does* apply a World factor, but uses the raw `World` matrix rather than its correct
inverse-transpose — an internal inconsistency with this same file directory's own non-skinned
`pbr3d.vert.glsl`, which gets this right.

## Checklist Results

### API / XNA / FNA parity
`SkinnedPbrEffect::SetBoneTransforms`/`setWeightsPerVertexProperty` (lines 157-159) mirror
`SkinnedEffect`'s XNA-shaped bone API; `DirectionalLight0.setEnabledProperty`/
`setDirectionProperty`/`setDiffuseColorProperty` (lines 154-156) mirror `IEffectLights`'s shape.
`setRoughnessFactorProperty`/`setMetallicFactorProperty`/`setNormalMapProperty` (lines 166-167,
172, 177) are correctly `NOXNA`-shaded glTF-derived extensions (not FNA members) — appropriately
named/scoped for a PBR effect that has no real XNA analog.

### Behavioral correctness
- Vertex layout: `SkinnedPbrGpuVertex` (lines 52-61, `static_assert(...==68,...)`) —
  pos(12)+normal(12)+tangent(16)+uv(8)+weights(16)+indices(4) = 68 bytes, matches the shader's own
  documented "stride-48 PBR layout + stride-52 skinning suffix, appended" convention
  (`pbr_skinned3d.vert.glsl` lines 3-5) and `QueuePbrDraw`'s `expectedStride = skinned ? 68u : 48u`
  gate (`SdlGpuGraphicsBackend.cpp` line 3045).
- Bone-palette dispatch: `bones={Identity}`, `weightsPerVertex=1` (lines 157, 159) — with a single
  identity bone at weight 1, `skinMat = Identity*1.0 = Identity` (shader lines 61-67), so
  `skinnedPos = vec4(inPos,1.0)` (an exact no-op), matching the file's own stated oracle rationale:
  an identity bind pose makes the skin transform a true no-op, so the rendered result should be
  bit-for-bit identical (modulo BRDF tolerance) to the equivalent non-skinned `PbrEffect` scene.
  This audit independently confirmed the *skin* portion of this reasoning is mathematically sound
  by reading the shader's `skinMat` computation directly (line 61: `bb.bones[inBoneIndices.x] *
  inBoneWeights.x`, with `inBoneIndices.x=0` per `BuildQuad`'s vertex data and `bones[0]=Identity`).
  However, the reasoning that "the rendered pixels must be identical to `PbrEffect`'s own values"
  additionally requires the *normal/tangent* transform to be equivalent between the skinned and
  non-skinned paths for an identity `World` — see F1 for why this happens to hold only because
  `World=Identity`, not because the two shaders compute normals the same way in general.
- Check A/B/C (lines 164-180): reuses the referenced `sdlgpu_pbreffect_test.cpp`'s exact expected
  values (`(79,79,79,255)` dielectric white, `(20,0,0,255)` metallic red, `(0,0,0,255)` tilted
  normal) with a `±10` tolerance (`WithinTolerance`, lines 75-81). This audit did **not**
  independently re-derive these 3 BRDF values from first principles (that would require
  re-verifying `sdlgpu_pbreffect_test.cpp` itself, which is out of this batch's scope) — treated as
  a reasonable, explicitly-declared oracle-reuse technique consistent with the established
  `easygl_skinnedpbreffect_golden_test.cpp` precedent this file cites, but flagged as **not
  independently confirmed** by this specific audit pass (see Missing Tests).

### Logic
Quad C ("tilted normal zeroed", line 179-180) is the most load-bearing check for exercising the
normal-map/TBN path specifically (Quad A/B use `setNormalMapProperty(nullptr)`, which falls back to
the default flat-normal texture per `EnsureDefaultPbrTextures()`, decoding to the unperturbed
geometric normal — so only Quad C's `tiltedNormalTex_` genuinely exercises `pbr3d.frag.glsl`'s
`TBN * sampledNormal` computation, line 98 of that shader) — and it is exactly this check whose
correctness depends on the TBN basis (built from `fragNormal`/`fragTangent`) being built from a
*correctly* transformed normal/tangent pair. Since `World=Identity` for this whole file, F1's defect
cannot be surfaced by Quad C either.

### C++ correctness
`RenderAndSampleCenter` (lines 104-120) correctly re-applies `fx.Apply()` before each draw (line
112) with the same shared `SkinnedPbrEffect fx` instance mutated between quads (texture/metallic/
normal-map properties changed at lines 164-178) — consistent, low-risk reuse pattern already seen
in the sibling `sdlgpu_skinnedeffect_vertexcolor_test.cpp`.

### Testing
3/3 checks are non-redundant and each targets a genuinely distinct material configuration
(dielectric, metallic, normal-mapped) — appropriately scoped given the stated oracle-reuse
rationale, though see F1/Missing Tests for the specific gap this design leaves open.

## Detailed Findings

### F1 — `pbr_skinned3d.vert.glsl` transforms the normal/tangent by the raw `World` matrix (`mat3(lp.world)`) rather than `World`'s inverse-transpose, unlike this same shader directory's own non-skinned `pbr3d.vert.glsl`, which computes the inverse-transpose correctly

- Severity: MEDIUM (confirmed-present via direct code comparison; not observable through this
  specific test's `World=Identity` scene, where `inverse(transpose(Identity)) == Identity`, so the
  bug is silently masked)
- Confidence: HIGH (read and directly compared both shader files' normal-matrix computations)
- Category: correctness / FNA-parity / cross-cutting (related to, but a distinct variant of, the
  systemic defect in `AUDIT_CROSS_CUTTING_FINDINGS.md`: *"`EnsurePbrSkinnedProgram()` uses the raw
  `uWorld` matrix instead of the correct inverse-transpose"* — already confirmed in EasyGL)
- Location/symbol: `pbr_skinned3d.vert.glsl` lines 75-77:
  ```
  mat3 skinNormalMat = mat3(skinMat);
  fragNormal = normalize(mat3(lp.world) * (skinNormalMat * inNormal));
  fragTangent = mat3(lp.world) * (skinNormalMat * inTangent.xyz);
  ```
  compared against this shader's own non-skinned sibling, `pbr3d.vert.glsl` lines 49-52:
  ```
  mat3 normalMatrix = transpose(inverse(mat3(lp.world)));
  fragNormal = normalize(normalMatrix * inNormal);
  ...
  fragTangent = mat3(lp.world) * inTangent.xyz;   // (tangent deliberately NOT inverse-transposed — see below)
  ```
- Evidence: `pbr_skinned3d.vert.glsl`'s own doc comment (lines 69-74) explicitly states it "mirrors
  `EasyGLGraphicsBackend::EnsurePbrSkinnedProgram()`'s own vTangent/vNormal computation
  (`mat3(uWorld)*(skinNormalMat*aNormal/aTangent.xyz)`)" — and `AUDIT_CROSS_CUTTING_FINDINGS.md`
  already independently confirms `EnsurePbrSkinnedProgram()`'s own use of the raw World matrix
  (not inverse-transpose) is a defect in EasyGL. This audit confirms `pbr_skinned3d.vert.glsl`
  faithfully reproduces that same defect (deliberately, per its own comment — "porting for
  rendered-output parity with the reference backend," the same pattern
  `AUDIT_CROSS_CUTTING_FINDINGS.md` already documents for the plain `SkinnedEffect`/WebGPU case).
  **Critically, this is a distinct code shape from the plain `SkinnedEffect` omission** (F1 in
  `sdlgpu_skinned_test.cpp`'s report): that shader applies *no* World factor to the normal at all;
  this shader *does* apply a World factor, just the wrong (non-inverse-transposed) one — for a
  uniform-scale `World`, `mat3(World)` and `transpose(inverse(mat3(World)))` happen to differ only
  by an overall scale factor that `normalize()` removes, so this specific variant's practical impact
  is narrower than the plain omission (it only diverges from correct for **non-uniform-scale**
  `World` matrices — a rotation-only or uniform-scale-only `World` would render identically either
  way, whereas the plain-`SkinnedEffect` omission is wrong for rotation too).
  This audit also confirms the tangent computation (`mat3(World)*tangent`, no inverse-transpose) is
  actually **consistent** between the skinned and non-skinned shaders — `pbr3d.vert.glsl`'s own
  comment (lines 53-55) explains this is a deliberate, documented simplification ("Tangent
  transforms as a plain direction under mat3(World)... correct for uniform-scale World transforms")
  applied identically in both files, so the tangent handling is *not* part of this specific finding
  (it's already a documented, consistent choice) — only the **normal** computation diverges between
  the two sibling shaders.
- Why it matters: a `SkinnedPbrEffect` draw with a non-uniformly-scaled `World` matrix (plausible
  for a stretched/squashed animated character) would compute incorrect normal-mapped lighting
  specifically, diverging from what the equivalent non-skinned `PbrEffect` would compute for the
  same geometry under the same `World` — undermining exactly the "should render identically to
  `PbrEffect`'s own already-verified output" invariant this test's own header relies on as its
  correctness argument, for any scene that isn't this specific `World=Identity`/rotation-only case.
- FNA/XNA comparison: N/A — `SkinnedPbrEffect`/PBR has no FNA/XNA equivalent; this is purely an
  internal-consistency and glTF-BRDF-correctness question, not an FNA parity gap.
- Related files: `pbr3d.vert.glsl` (the correct sibling, for direct comparison), `skinned3d.vert.glsl`
  and `skinned_colored3d.vert.glsl` (the plain-`SkinnedEffect` variant of this same defect family —
  see `sdlgpu_skinned_test.cpp.audit.md`'s F1 and
  `sdlgpu_skinnedeffect_vertexcolor_test.cpp.audit.md`'s F1), and
  `include/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp`'s `EnsurePbrSkinnedProgram()`
  (the origin of the ported defect, per this shader's own comment).
- Suggested future action (not implemented by this audit): change
  `fragNormal = normalize(mat3(lp.world) * (skinNormalMat * inNormal));` to precompute
  `transpose(inverse(mat3(lp.world)))` (matching `pbr3d.vert.glsl`'s own established pattern) and
  apply that to `skinNormalMat * inNormal` instead of the raw `mat3(lp.world)`; add a non-uniform-
  scale-`World` companion test (e.g. `World = CreateScale(2,1,1)`) that would only pass once this is
  fixed, since the current scene structurally cannot distinguish correct from incorrect behavior
  here.

## Cross-File Observations

- This file, together with `sdlgpu_skinned_test.cpp` and `sdlgpu_skinnedeffect_vertexcolor_test.cpp`,
  gives this backend **three** confirmed instances of the same broader "World-space normal
  composition with a bone-skin matrix is not done correctly" defect family, but in **two distinct
  code shapes**: plain `SkinnedEffect` omits the World factor entirely; `SkinnedPbrEffect` applies
  it but without the inverse-transpose. Both shapes trace to the same origin (a deliberate,
  documented "match EasyGL's reference rendered output" porting discipline), and both are masked by
  every existing test in this shard using `World=Identity`.
- The internal inconsistency between `pbr3d.vert.glsl` (correct inverse-transpose) and
  `pbr_skinned3d.vert.glsl` (raw World) within the *same shader directory* is itself worth noting:
  a future maintainer fixing one might reasonably assume the sibling already matches, when it does
  not — this is exactly the kind of "two backends/shaders drift out of sync on the same underlying
  math" risk `AUDIT_CROSS_CUTTING_FINDINGS.md`'s "Duplicated backend logic" section is watching for.
- Git history: `a72bc60b`/`fa3babc0` ("port PbrEffect/SkinnedPbrEffect BRDF and skinned vertex color
  from EasyGL") is the introducing commit for both this file and `pbr_skinned3d.vert.glsl` — no
  later commit revisits the normal-matrix computation, so F1 is a currently-live characteristic of
  the shipped shader, not a stale claim about since-fixed code.
- The fog-formula cross-cutting bug does not apply — this backend's PBR shaders implement no fog
  term (consistent with every other 3D shader in this backend).

## Missing or Weak Tests

- See F1 — no non-uniform-scale (or even rotation-only) `World` variant exists to distinguish
  correct from incorrect normal transform behavior for the skinned-PBR path specifically.
- This audit did not independently re-derive the 3 oracle pixel values
  (`(79,79,79,255)`/`(20,0,0,255)`/`(0,0,0,255)`) from the glTF BRDF formula and this scene's light/
  camera setup — they were checked only for *internal consistency* (reused verbatim from the cited
  `sdlgpu_pbreffect_test.cpp`, out of this batch's scope) and for the *identity-bind-pose-implies-
  same-value* reasoning (which holds for the skin-matrix and tangent parts, but not unconditionally
  for the normal part per F1). A full independent BRDF re-derivation was out of scope for this
  audit pass given the batch's file list, but is flagged here as unverified rather than silently
  assumed correct.
- No test exercises `WeightsPerVertex > 1` for the PBR-skinned path (only a single identity bone,
  weight 1, is used) — the multi-bone-blend interaction with the normal/tangent transform is
  entirely untested for this effect.

## Positive Findings

- The stride-68 vertex layout, bone-palette skin transform for Position, and the shared
  `pbr3d.frag.glsl` BRDF stage were all independently confirmed to be correctly wired for the
  identity-bind-pose case this test exercises.
- The oracle-reuse design (reusing `sdlgpu_pbreffect_test.cpp`'s already-verified expected values) is
  a reasonable, low-duplication testing strategy *when its preconditions genuinely hold* — this
  audit's contribution is identifying the one respect (F1) in which those preconditions are narrower
  than the file's own header implies (holds for uniform-scale/rotation-only `World`, not for
  non-uniform scale).
- The shader-level doc comments throughout this backend's PBR shaders are unusually thorough and
  cross-referential (each explicitly cites its EasyGL origin and its sibling shader), which made
  this specific internal-inconsistency finding straightforward to verify by direct comparison rather
  than requiring speculative reasoning.

## Final Assessment

The 3 checks correctly prove end-to-end stride-68 vertex/BRDF wiring for the specific scene this
file constructs, and the identity-bind-pose oracle-reuse rationale is sound for the skin-transform
and tangent-transform portions of the pipeline. This audit's main contribution is F1: a genuine,
currently-live normal-matrix defect in `pbr_skinned3d.vert.glsl` — a distinct variant of this
backend's already-confirmed skinned-normal-matrix defect family, and an internal inconsistency with
this same shader directory's own correct non-skinned `pbr3d.vert.glsl` — invisible to this test's
`World=Identity` scene by construction.
