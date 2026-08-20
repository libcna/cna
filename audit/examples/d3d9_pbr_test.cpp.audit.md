# Audit: examples/d3d9_pbr_test.cpp

## Metadata

- Source file: `examples/d3d9_pbr_test.cpp` (262 lines)
- Audit status: AUDITED (static/source-reading only — see Environment Note below)
- Subsystem: `examples-tests-d3d9` shard — `PbrEffect`/`SkinnedPbrEffect` (NOXNA) dispatch
- File type: standalone `Game`-subclass executable, CTest-registered as `D3D9_Pbr`
  (`cna_test_d3d9_pbr`, `cmake/Tests/D3D9Tests.cmake:74-77`, `TIMEOUT 60 LABELS "D3D9"`).
- XNA/FNA relevance: NOXNA — XNA 4.0 has no PBR effect at all; `PbrEffect`/`SkinnedPbrEffect` are
  CNA's own extensions (`plans/plan_cnj.md` CNB-56..79), ported to D3D9 via CNA's own custom vs_3_0/ps_3_0
  `Pbr3D.hlsl`/`PbrSkinned3D.hlsl` shaders — **not** vendored Microsoft Stock Effect files, and
  therefore fully in-scope for correctness review (unlike `shaders/xna/**`, which is exempt per D-5).
- Related production code: `src/CNA/Internal/Backends/D3D9/D3D9PbrDraw.cpp` (292 lines, the entire
  feature), `shaders/cna/Pbr3D.hlsl`, `shaders/cna/PbrSkinned3D.hlsl`, `D3D9EffectDraw.cpp`
  (`DrawPrimitivesExImpl`'s dispatch cascade, lines 545-606).
- The file's own header comment (lines 31-33) discloses: *"this file is written and cross-compile-
  checked (mingw) but has NOT been run"* — a rare, appreciated instance of a test file being explicit
  about its own verification status rather than implying more confidence than warranted.

### Environment Note (per D-P4)

D3D9 is Windows-only and cannot be built or run in this Linux sandbox. This report is static/
source-reading only, and additionally corroborates the file's own disclosed "written but never run"
status: every pixel-value claim below was checked by reading `D3D9PbrDraw.cpp` and both `.hlsl`
shaders line-by-line and hand-evaluating the BRDF formula for the specific "ambient-only" scene this
file constructs, not by executing anything.

## Purpose

4-check proof of PBR dispatch on D3D9: (A) unskinned `Pbr3D` renders exact `texture*DiffuseColor`
when all 3 lights' diffuse is deliberately zeroed (collapsing the BRDF's `Lo` term to exactly zero,
sidestepping the need to hand-derive a half-vector-dependent specular term — the same technique
`d3d9_drawex_test.cpp` uses for `BasicEffect.SpecularColor=0`); (B) the skinned `PbrSkinned3D`
variant, with a single Identity bone at 100% weight (a deliberate skinning no-op), produces the exact
same readback, proving the `Bones[72]` upload doesn't corrupt output even in the trivial case; (C)
`params.pbr=true` together with `params.dualTexture=true` still renders via the PBR path, proving
`pbr` is checked before every Stock-Effect flag in the dispatch cascade; (D) an unsupported vertex
stride for `PbrEffect` throws a named error rather than silently drawing garbage.

## Executive Verdict

**Needs attention** — Checks A-D are all internally correct and their expected values were
independently re-derived by this audit from `Pbr3D.hlsl`'s/`PbrSkinned3D.hlsl`'s own pixel-shader
formula and confirmed exact matches. However, direct reading of `PbrSkinned3D.hlsl` found a real,
previously-undocumented-as-a-bug defect (F1): the skinned normal/tangent transform composes only the
raw `World` matrix (not its inverse-transpose) after skinning — the same shape of defect this audit's
own cross-cutting findings have already confirmed in 3 other backends' skinned-effect shaders. Check
B cannot detect it because it deliberately uses `World=Identity` (for `Identity` a plain and an
inverse-transpose matrix are equal), which is exactly the masking pattern the cross-cutting findings
describe for the other backends.

## Checklist Results

### Behavioral correctness

Independently re-derived Check A's math from `Pbr3D.hlsl`'s `PSPbr3D` (lines 127-166): with
`Light0Diffuse=Light1Diffuse=Light2Diffuse=(0,0,0)`, `PbrLight()`'s return
`(kd*diffuseColor/π + specular) * lightColor * NdotL` is multiplied by a zero `lightColor` for every
light, making `Lo` exactly `(0,0,0)` regardless of roughness/metallic/normal-map/NdotL — confirmed
this holds structurally (the zero multiply is the outermost factor, so no BRDF term needs evaluating
to know the product is exactly zero). With `Lo=0`, `EmissiveColor=(0,0,0)` (so `emissive=0` regardless
of the emissive-map sample), and no `OcclusionMap`/`EmissiveMap` bound (both fall back to the white
1×1 texture via `BindPbrSampler`'s fallback argument, `D3D9PbrDraw.cpp:269-273`, so `occlusion=1`),
the formula collapses to exactly `outColor.rgb = AmbientColor*albedo = (1,1,1)*(texture.rgb*
DiffuseColor.rgb) = texture.rgb*DiffuseColor.rgb` — matching Check A's asserted
`(200,120,40,255)` exactly (the 1×1 texture's own raw RGB, `DiffuseColor=(1,1,1,1)`).
`AlphaTest` defaults to `{0,0,1,1}` (`IGraphicsBackend.hpp:383`, "Always pass"), confirmed this
never clips the fragment. `FogParams.x=0` (not set by `SetAmbientOnlyPbrParams`, so `fogEnabled`
stays at its `GpuDrawParams` default `false`), so `FogFactor=1.0` and the final
`lerp(FogColor, outColor.rgb, 1.0)` is a no-op — confirmed the readback is untouched by fog.

Check B's `PbrSkinned3D.hlsl` (`PSPbrSkinned3D`, lines 149-188) is textually identical to `Pbr3D.hlsl`'s
pixel shader (both files' own comments state this: "this file's own pixel shader is a straight copy
of Pbr3D.hlsl's"), so the same collapse to `texture.rgb*DiffuseColor.rgb` applies; the only
question is whether the `Bones[72]`/skinning vertex-stage math corrupts the *position* enough to move
the sampled UV away from `(28,28)`'s expected value — with a single Identity bone at 100% weight
(`params.weightsPerVertex=1`, `params.boneCount=1`, identity `boneTransforms`), `skinning =
Bones[0]*1.0` is exactly the identity `float4x3`, so `skinnedPos == vin.Position` and
`skinnedNormal/skinnedTangent == vin.Normal/vin.Tangent` unchanged — confirmed this is a genuine
no-op, matching the test's own framing.

Check C: confirmed via `D3D9EffectDraw.cpp:561-565` (`if (params.pbr) { DrawPbrEffectEXT(...); return; }`)
that `params.pbr` is checked and dispatched **before** `needsDualTex`/`needsEnvMap`/`needsSkinned`
are even computed (lines 567-572) — Check C's claim that `params.pbr` takes priority over
`params.dualTexture` is architecturally guaranteed by this ordering, not merely empirically likely.

Check D: `DrawPbrEffectEXT` (`D3D9PbrDraw.cpp:156-174`) throws `std::runtime_error` for
`!skinned && stride != 48` before doing anything else — stride 20 (`VPT`, Check D's buffer) correctly
triggers this.

### Logic

Traced `SetAmbientOnlyPbrParams` (lines 116-128, shared by Checks A/B/C) — zeroes all 3 lights'
diffuse, sets `AmbientColor=(1,1,1)`, `EmissiveColor=(0,0,0)`, `pbrMetallicFactor=0`,
`pbrRoughnessFactor=1` — none of the metallic/roughness values matter given `Lo=0`, so their specific
values are inert for this scene (a reasonable simplification, not an oversight, since the file's own
purpose is proving dispatch/upload correctness, not BRDF shading correctness — the BRDF math itself
is a line-by-line port of `EnsurePbrProgram()`'s GLSL, not re-derived here).

### C++ correctness

`UploadBonesVS` (`D3D9PbrDraw.cpp:100-116`) clamps `boneCount` to `params.boneCount < 72 ? ... : 72`
— Check B's `boneCount=1` is within range, no clamping exercised, but the guard itself is correct
against `GpuDrawParams::boneTransforms[72*16]`'s own fixed size.

### Memory/resource lifetime

`pbrVS_`/`pbrPS_`/`pbrSkinnedVS_`/`pbrSkinnedPS_` are lazily created and cached exactly once per
backend instance (`D3D9PbrDraw.cpp:182-223`), consistent with this backend's established shader
caching idiom (same pattern as `D3D9InstancedDraw.cpp`'s `instancedVS_`/`instancedPS_`).

### Architecture

`DrawPbrEffectEXT` is correctly documented and implemented as "one of [the dispatch cascade's] own
branches (the highest-priority one, checked first)" rather than routed through the Stock-Effect
cascade proper — matches `EasyGLGraphicsBackend::SelectProgram()`'s own precedent per both files'
comments, confirmed via direct reading of `D3D9EffectDraw.cpp:555-565`.

### Robustness

Check D correctly proves the unsupported-stride case is a real, named `runtime_error` (not a silent
wrong-draw or an unreachable code path) — a legitimate defensive-programming test.

### Testing

Checks A-D give solid coverage of dispatch priority, unskinned/skinned rendering parity, and
error handling for the unsupported-stride case. Not covered: any check with `World != Identity`
(which would surface F1), a lit scene with non-zero light diffuse (would exercise the actual BRDF
math, not just the ambient-only collapse), or `boneCount > 1`/`weightsPerVertex` 2 or 4 for the
skinned path.

## Detailed Findings

### F1 — `PbrSkinned3D.hlsl`'s post-skin normal/tangent transform uses the raw `World` matrix, not its inverse-transpose — the same defect shape already confirmed in 3 other backends' skinned shaders

- Severity: MEDIUM
- Confidence: HIGH (confirmed by direct source reading, not inference)
- Category: correctness / cross-backend parity (NOXNA extension — no direct FNA equivalent, but the
  same correctness bar this audit's own cross-cutting findings already hold every other backend's
  `PbrEffect`/`SkinnedPbrEffect` pair to)
- Location/symbol: `PbrSkinned3D.hlsl`'s `VSPbrSkinned3D` (line 85):
  `vout.Normal = normalize(mul(skinnedNormal, (float3x3)World));` — and line 86 applies the identical
  `(float3x3)World` to the tangent. Contrast with the file's own unskinned sibling, `Pbr3D.hlsl` line
  57: `vout.Normal = mul(vin.Normal, NormalMatrix);`, where `NormalMatrix` is a genuine
  `WorldInverseTranspose` uploaded at `D3D9PbrDraw.cpp:242-243`
  (`Matrix::Transpose(Matrix::Invert(world))`) — the unskinned path gets this right; only the skinned
  variant uses the raw matrix.
- Evidence: `PbrSkinned3D.hlsl` declares no `NormalMatrix`/`WorldInverseTranspose` register at all
  (its register block, lines 40-43, has only `WorldViewProj`, `World`, `FogParams`, `Bones[72]`) —
  there is no equivalent constant even *available* to upload correctly; the raw `World` is the only
  matrix the shader has to work with for this transform.
- Why it matters: raw `World` is correct only for rotation/uniform-scale/translation transforms; a
  non-uniform-scale `World` matrix on a skinned+PBR model would skew the transformed normal and
  tangent, producing incorrect BRDF lighting and incorrect tangent-space normal-mapping results
  specifically for non-uniformly-scaled skinned PBR models (e.g. squash/stretch character animation
  applied at the root transform rather than per-bone).
- The file's own header comment (lines 20-26) explicitly *documents* this as a deliberate,
  non-silent port choice: *"after the skin-local rotation, the skinned Normal/Tangent are further
  transformed by (float3x3)World before use — EasyGL's own EnsurePbrSkinnedProgram() does this too
  ... so this is a faithful port, not a deviation."* This confirms the choice was intentional and
  traceable, but the comment frames it purely as "faithful to the reference backend," without noting
  that the EasyGL reference itself has this exact defect — this audit's own
  `EasyGLGraphicsBackend.cpp.audit.md` F3 (`EnsurePbrSkinnedProgram` "uses the raw `uWorld` matrix
  (not the inverse-transpose)") independently confirms the same root cause in the backend this file
  says it faithfully ported from. This is now a **4th confirmed instance** of the broader
  cross-cutting "skinned-effect shaders skip the WorldInverseTranspose normal transform" pattern
  already documented for EasyGL, WebGPU, and Vulkan in `AUDIT_CROSS_CUTTING_FINDINGS.md` — D3D9's own
  `PbrSkinned3D.hlsl` should be added to that list.
- Why Check B cannot catch this: Check B uses `Matrix::getIdentityProperty()` for `world` — for the
  identity matrix, `World` and `WorldInverseTranspose` are numerically equal, so this defect is
  invisible to any test using an identity or pure-rotation `World`, exactly the masking pattern
  already identified for the other 3 backends.
- FNA/XNA comparison: N/A directly (PBR effects are NOXNA), but the correctness bar is the same
  real-time-graphics standard (inverse-transpose for normals under non-uniform scale) `Pbr3D.hlsl`'s
  own unskinned sibling already applies correctly two lines away in the same directory.
- Related files: `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp` (`EnsurePbrSkinnedProgram`,
  the same bug's origin per this file's own comment), `AUDIT_CROSS_CUTTING_FINDINGS.md`'s "skinned-
  effect shaders skip the WorldInverseTranspose normal transform" entry.
- Suggested future action (not implemented by this audit): add a `NormalMatrix`/`WorldInverseTranspose`
  register to `PbrSkinned3D.hlsl` (mirroring `Pbr3D.hlsl`'s own `c8` register), upload it from
  `D3D9PbrDraw.cpp`'s skinned branch the same way the unskinned branch already does at line 243, and
  compose it with `skinNormalMat` instead of the raw `(float3x3)World`.

## Cross-File Observations

- `Pbr3D.hlsl`'s fog formula (line 67:
  `saturate((vin.Position.z + FogParams.z) / (FogParams.z - FogParams.y))`) and `PbrSkinned3D.hlsl`'s
  identical copy (line 92) both match the **correct**, already-fixed EasyGL fog formula
  (`AUDIT_CROSS_CUTTING_FINDINGS.md`'s "CONFIRMED IN 3+ BACKENDS" entry cites EasyGL's
  `vFogFactor=(aPos.z+uFogEnd)/(uFogEnd-uFogStart)` as correct, vs. Bgfx/Vulkan's mirrored, wrong
  formula) — worth recording as a **positive** cross-check: D3D9's own PBR shaders did **not**
  inherit the fog-formula bug that propagated to Bgfx/Vulkan, even though they did inherit the
  unrelated skinned-normal-transform bug from the same EasyGL reference (F1). This is useful evidence
  that the porting process is not uniformly buggy — some formulas were ported correctly, others
  weren't, on a per-formula basis.
- `git log --oneline -- examples/d3d9_pbr_test.cpp` shows one commit
  (`2b2004aa feat(D3D9 backend): port PBR (PbrEffect/SkinnedPbrEffect) and skinned vertex color to
  D3D9`), consistent with the file's own single-task scope and its own "not run" disclosure.
- `D3D9PbrDraw.cpp`'s constant-upload helpers (`UploadMatrixConstantVS`, `UploadBonesVS`) are locally
  duplicated from `D3D9EffectDraw.cpp`'s own identically-named/-shaped helpers, per this file's own
  header comment citing `D3D9VertexDeclarations.cpp`'s "Task 11.10" precedent for accepting this
  small duplication — a documented, intentional trade-off, not an oversight.

## Missing or Weak Tests

- See F1 — no check in this file uses a non-identity `World` (rotation or non-uniform scale) for the
  skinned path, so the normal-transform defect is untestable by this file as written.
- No check exercises a lit scene (non-zero light diffuse) to validate the actual BRDF math itself,
  only the ambient-collapse special case.

## Positive Findings

- The file's own disclosure that it has "NOT been run" (only cross-compiled) is a valuable, honest
  signal this audit corroborates rather than second-guesses — the underlying math is sound as
  written, but this genuinely has never been observed executing.
- Check A/B/C/D's expected values were all independently re-derivable from the shader source with
  no ambiguity — the "zero every light's diffuse to force `Lo=0`" technique is a clean, low-risk way
  to test dispatch/upload plumbing without needing to hand-verify the BRDF itself.
- The fog formula in both PBR shaders correctly matches the already-fixed EasyGL reference, not the
  buggy Bgfx/Vulkan mirror-image formula (see Cross-File Observations).

## Final Assessment

The test itself is correctly reasoned and its 4 checks would pass exactly as designed. The
significant outcome of this audit is F1: direct reading of the actual shader source (in scope, since
`shaders/cna/**` is not vendored) found a real, documented-but-undisclosed-as-a-bug defect in
`PbrSkinned3D.hlsl`'s normal transform — a 4th confirmed instance of this codebase's recurring
skinned-effect World-inverse-transpose omission, invisible to Check B specifically because it uses
`World=Identity`. Recommend flagging this file/shader pair in `AUDIT_CROSS_CUTTING_FINDINGS.md`
alongside the existing EasyGL/WebGPU/Vulkan instances.
