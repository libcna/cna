# Audit: examples/sdlgpu_envmap_test.cpp

## Metadata

- Source file: `examples/sdlgpu_envmap_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlgpu` shard — `EnvironmentMapEffect` cube-map reflection proof
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_sdlgpu_test(cna_test_sdlgpu_envmap …)` / `cna_register_backend_test(NAME SdlGpu_EnvMap
  …)`, `cmake/Tests/SdlGpuTests.cmake:71-73`, `TIMEOUT 60`).
- XNA/FNA relevance: direct — `EnvironmentMapEffect` (`EnvironmentMapAmount`, `EmissiveColor`,
  `EnvironmentMapSpecular`, `DiffuseColor` (not set here, left at its FNA default), `TextureCube`).
- FNA reference: `HLSL/EnvironmentMapEffect.fx` (`PSEnvMap`/`PSEnvMapSpecular`),
  `HLSL/Lighting.fxh` (`ComputeLights`: `result.Diffuse = mul(diffuse, lightDiffuse) *
  DiffuseColor.rgb + EmissiveColor;`), `EffectHelpers.cs`'s `SetMaterialColor` (the
  ambient/emissive-precombination convention this effect's C++ port also uses).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.cpp`
  (`FillGpuDrawParams()`), `src/CNA/Internal/Backends/SdlGpu/shaders/env_map3d.{vert,frag}.glsl`,
  `SdlGpuGraphicsBackend.cpp` (`FillEnvMapUniforms`/`FillEnvMapParams`).

## Purpose

Three-check pixel-readback proof (via `RenderTarget2D::GetData()`, not a screenshot) that
`EnvironmentMapEffect`'s cube-map reflection genuinely samples and blends, mirroring this
project's existing `vulkan_environmentmapeffect_amount_one_test.cpp`: since the test cubemap is a
single, uniformly-colored solid on all 6 faces, the specific reflection direction cannot matter —
only whether the sample is genuinely read and blended at all. Check A:
`EnvironmentMapAmount=1` + a solid **white** cube — FNA's real lerp *fully replaces* the
lit/textured color, so the readback should be white. Check B: the same scene with a solid
**gray** cube — readback should be gray, not white and not the diffuse texture's own color
(the real discriminator that the sampled value is genuinely coming from the cubemap, not a
hardcoded/leftover value). Check C: `EnvironmentMapAmount=0` — the env-map contribution must be
fully suppressed (not close to the cubemap color), proving the blend amount is real.

## Executive Verdict

**Significant correctness risk** — the fragment shader this file exercises re-multiplies the
pre-combined `EmissiveColor` term by `DiffuseColor` a second time, a formula bug that diverges
from FNA's real `Lighting.fxh` convention (F1). This is the same class of defect
`AUDIT_CROSS_CUTTING_FINDINGS.md` already documents as confirmed in Bgfx's and (per that
document) suspected in Vulkan's `EnvironmentMapEffect` shaders — this audit independently traces
it as a fourth, distinct instance in this SDL_GPU backend's own `env_map3d.frag.glsl`. It is
completely invisible to this specific test file because `DiffuseColor` is left at its FNA
default of `(1,1,1)`, under which the bug's extra multiply is a no-op — exactly the masking
pattern the cross-cutting document already flagged for the other backends' equivalent test
families.

## Checklist Results

### API / XNA / FNA parity
`setEmissiveColorProperty`/`setEnvironmentMapAmountProperty`/`setEnvironmentMapSpecularProperty`/
`setTextureProperty`/`setEnvironmentMapProperty` all match FNA's `EnvironmentMapEffect` property
surface. `DiffuseColor` is *not* set by this test (left at its FNA-matching default
`Vector3(1,1,1)`, confirmed at `EnvironmentMapEffect.hpp:387`) — this is the specific choice that
masks F1; see below.

### Behavioral correctness
Re-derived FNA's real formula chain by hand, cross-referencing three independent sources that all
agree:
1. `EffectHelpers.cs`'s `SetMaterialColor` (`Lighting.fxh`'s calling convention, used by every
   FNA stock lighting effect including `EnvironmentMapEffect`): when lighting is enabled (always
   true for this effect), it precomputes `emissive = (EmissiveColor + AmbientLightColor *
   DiffuseColor) * alpha` on the CPU specifically so *"the shader no longer needs to bother adding
   the ambient contribution, simplifying its computation to: `(sum(diffuse directional light) *
   DiffuseColor) + EmissiveColor`"* (comment quoted verbatim from FNA's own source, lines 202-206).
2. `Lighting.fxh`'s `ComputeLights()`, line 43: `result.Diffuse = mul(diffuse, lightDiffuse) *
   DiffuseColor.rgb + EmissiveColor;` — i.e., the light sum is multiplied by `DiffuseColor`, and
   the (precombined) `EmissiveColor` term is *added afterward, unscaled*.
3. This codebase's own `EnvironmentMapEffect::FillGpuDrawParams()` (confirmed lines 418-426)
   correctly reproduces step 1's CPU-side precombination: `p.emissiveColor[i] = (emissiveColor_.i
   + ambientLightColor_.i * diffuseColor_.i) * alpha_` — this C++ layer is **correct** and matches
   FNA exactly (also independently confirmed correct in this project's `lit_textured3d.frag.glsl`,
   which implements the identical FNA convention for `BasicEffect` with an explicit comment stating
   it: *"EmissiveColor is added after the light-sum*DiffuseColor multiply, not scaled by it …
   `lit = lightSum * fragTint.rgb + lp.emissiveColor_pad.xyz;`"*).

   This backend's own `env_map3d.frag.glsl`, however (confirmed line 53), computes:
   `vec3 litRGB = (pc.emissiveAmount.xyz + lightSum) * fragTint.rgb;`
   — i.e., it adds the precombined emissive term to `lightSum` **first**, then multiplies the
   *sum* by `fragTint.rgb` (which carries `DiffuseColor`). This re-applies `DiffuseColor` to the
   `EmissiveColor` term a second time, directly contradicting both FNA's real formula and this
   same backend's own correct `lit_textured3d.frag.glsl` sibling shader.
- With this test's actual values (`DiffuseColor` left at default `(1,1,1)`, so `fragTint.rgb =
  (1,1,1)*alpha`), the bug's extra multiply-by-`(1,1,1)` is numerically a no-op — this is *why*
  none of the three checks (which only vary `EnvironmentMapAmount` and the cubemap color, never
  `DiffuseColor`) can detect it. A hypothetical fourth check setting `DiffuseColor` to anything
  other than `(1,1,1)` (e.g., `(0.5, 1.0, 1.0)`, a colored material) would read back a visibly
  wrong emissive contribution — the reflected env-map/lit color would be scaled by the diffuse
  tint in the R channel even for the pure-emissive term, where FNA's real behavior keeps
  `EmissiveColor`'s contribution untinted by `DiffuseColor`.

### Logic
Checks A/B/C are each real, well-chosen discriminators for *their own* stated claims
(env-map-replaces-color, cube-sample-is-genuine, amount-is-real) — none of those three specific
claims is what F1 affects; F1 is a formula-correctness gap in a code path (`EmissiveColor`
handling) this file's chosen scene values happen not to exercise discriminatingly.

### C++ correctness
`MakeSolidCube()`'s per-face `SetData(face, &col, 1)` loop and `RenderWith()`'s render-clear-draw-
readback sequence are correct and match this project's established `TextureCube`/`RenderTarget2D`
usage conventions elsewhere in this shard.

### Robustness
`RenderWith()` correctly clears the render target to `(0,0,0,255)` before each draw (distinct
from both expected colors and from the background this scene doesn't otherwise use), so a
"never actually rendered" failure mode would read back black, not accidentally match an expected
value.

### Testing
Strong, well-targeted coverage for the three claims it makes (replace/genuine-sample/amount-real).
Weak/missing coverage for `DiffuseColor` interaction with `EmissiveColor` — see F1 and Missing
Tests.

## Detailed Findings

### F1 — `env_map3d.frag.glsl` re-multiplies the precombined `EmissiveColor` term by `DiffuseColor`, diverging from FNA's real `Lighting.fxh` formula; masked in this test by `DiffuseColor`'s untouched default of `(1,1,1)`

- Severity: HIGH
- Confidence: HIGH (independently re-derived from FNA's real `EffectHelpers.SetMaterialColor`/
  `Lighting.fxh` source and confirmed against this project's own `FillGpuDrawParams()` C++ code
  and its own correctly-implementing `lit_textured3d.frag.glsl` sibling shader — not just a
  pattern match)
- Category: correctness / FNA-parity
- Location/symbol: `env_map3d.frag.glsl` line 53:
  `vec3 litRGB = (pc.emissiveAmount.xyz + lightSum) * fragTint.rgb;`
  (should be `lightSum * fragTint.rgb + pc.emissiveAmount.xyz` to match FNA); contrast with the
  same backend's own correct `lit_textured3d.frag.glsl` line 71:
  `vec3 lit = lightSum * fragTint.rgb + lp.emissiveColor_pad.xyz;`
- Evidence: see the Behavioral correctness section above for the full three-source derivation
  (FNA's `EffectHelpers.cs` comment explaining the CPU precombination's *intent*, `Lighting.fxh`'s
  literal formula, and this codebase's own correct/incorrect sibling shaders). The
  `SdlGpuGraphicsBackend.cpp` comment at the `FillEnvMapUniforms()` call site (lines 370-372)
  states *"EnvironmentMapEffect::FillGpuDrawParams() already pre-sums emissive+ambient*diffuse and
  pre-multiplies diffuseColor/emissiveColor by Alpha, so no extra alpha handling needed"* — this
  claim about *alpha* handling is correct, but does not address (and the shader itself does not
  correctly implement) the *diffuse-color* handling of the already-precombined emissive term.
- Why it matters: for any `EnvironmentMapEffect` scene where `DiffuseColor` is set to something
  other than the default `(1,1,1)` — e.g., a colored/tinted metallic surface, a common real-world
  usage — the emissive contribution would be incorrectly tinted by the material's diffuse color
  instead of remaining independent, producing a visibly wrong (typically darker/color-shifted)
  emissive term compared to real XNA/FNA. This is confirmed as the **fourth backend** (after
  Bgfx, confirmed; Vulkan, per `AUDIT_CROSS_CUTTING_FINDINGS.md`'s "test-file phrasing suggests
  Vulkan shares the same bug (unconfirmed pending a dedicated check)"; and now this SDL_GPU
  backend, independently confirmed via direct shader/source inspection rather than phrasing
  inference) to carry this specific `EnvironmentMapEffect` emissive/diffuse formula defect,
  strengthening the case that it is systemic to how `EnvironmentMapEffect`'s fragment shader was
  authored/ported across this project's backends, rather than an isolated one-off. Notably, this
  same backend's own `BasicEffect` shader (`lit_textured3d.frag.glsl`) implements the *correct*
  formula with an explicit comment describing it correctly — meaning whoever wrote
  `env_map3d.frag.glsl` had the correct reference pattern sitting in a sibling file in the same
  directory and still introduced the divergent formula, suggesting `env_map3d.frag.glsl` was
  ported from a different (already-buggy) reference — most likely Bgfx's or Vulkan's own
  `EnvironmentMapEffect` shader, consistent with this project's documented practice (seen
  elsewhere in `AUDIT_CROSS_CUTTING_FINDINGS.md`, e.g. the WebGPU skinned-normal bug) of porting
  shader logic from one backend's implementation into another's, including its defects.
- FNA/XNA comparison: diverges from FNA's real, verified formula (`Lighting.fxh` line 43 /
  `EffectHelpers.cs` lines 202-226) as detailed above.
- Related files: `src/CNA/Internal/Backends/SdlGpu/shaders/env_map3d.frag.glsl` (the actual bug
  location; out of this batch's file list but necessarily inspected to assess this test file's
  correctness — flagged here for the `backend-sdlgpu` shard's own audit to pick up as its primary
  finding), `src/Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.cpp` (confirmed correct,
  not implicated), `AUDIT_CROSS_CUTTING_FINDINGS.md`'s existing Bgfx/Vulkan
  `EnvironmentMapEffect` emissive*diffuse entry (this finding extends that entry to a fourth
  backend).
- Suggested future action (not implemented by this audit): fix `env_map3d.frag.glsl` line 53 to
  `lightSum * fragTint.rgb + pc.emissiveAmount.xyz`, matching `lit_textured3d.frag.glsl`'s own
  established correct convention in the same file directory; add a fourth check to
  `sdlgpu_envmap_test.cpp` (or a new dedicated file) that sets a non-default, non-white
  `DiffuseColor` (e.g., `(0.5, 1.0, 1.0)`) together with a nonzero `EmissiveColor` and confirms the
  emissive contribution's readback value does not scale with the diffuse tint — this is exactly
  the kind of check that would have caught F1 and would guard against a regression once fixed.

## Cross-File Observations

- `env_map3d.vert.glsl` correctly omits the Vulkan-specific Y-flip
  `AUDIT_CROSS_CUTTING_FINDINGS.md` documents as a *separate* confirmed Vulkan-only defect —
  confirmed this backend's own established convention (per `colored3d.vert.glsl`'s own comment,
  "No Vulkan-style Y-flip here -- confirmed empirically via sprite2d.vert.glsl that SDL_gpu's
  [own NDC convention differs]") correctly applies uniformly across every 3D vertex shader in
  this backend including `env_map3d.vert.glsl`, so that specific cross-cutting concern does
  **not** recur here — worth noting as a clean result, not just a gap.
- This file's CW-winding comment (lines 154-159) explicitly cross-references
  `sdlgpu_renderstate_test.cpp`'s own note on the same winding convention, and independently
  cross-referencing `SdlGpuGraphicsBackend.cpp`'s `ToCullMode()` (confirmed real, dynamic
  `SDL_GPUCullMode` dispatch, not a hardcoded `SDL_GPU_CULLMODE_NONE`) confirms the comment's claim
  that culling is genuinely wired up (not the old "SDLGPU-18/19/20 not yet done" state it
  contrasts itself against) — an accurate, current claim, not stale.
- `plans/plan_sdlgpu.md`'s own `SDLGPU-33` row documents this file's 3 checks as "3/3 checks" verified,
  and describes the correct FNA lerp semantics for the *blend* (`mix(baseColor, envSample*
  combinedAlpha, blendFactor) + envMapSpecular*envSample.a*combinedAlpha`) accurately — but does
  not mention the emissive/diffuse formula at all, confirming F1 is a genuine, currently
  undocumented gap rather than a known-and-accepted limitation.

## Missing or Weak Tests

- No check varies `DiffuseColor` away from its default — see F1; this is the specific gap that
  hides the emissive/diffuse-remultiplication defect.
- No check exercises `EnvironmentMapSpecular` with a nonzero value (this file always sets it to
  `Vector3(0,0,0)`) — the fragment shader's `+ ep.envMapSpecular_pad.xyz * envSample.a *
  combinedAlpha` term (line 71 of `env_map3d.frag.glsl`) is therefore never exercised by this
  file at all, unlike Vulkan's own `vulkan_environmentmapeffect_amount_one_test.cpp` family which
  this file is explicitly modeled on and which does have a dedicated specular-variant test.
- `EnvironmentMapEffect::FresnelEnabled` has no public setter in this codebase (confirmed
  permanently `true` per `plans/plan_sdlgpu.md`'s own `SDLGPU-33` note) — this file's 3 checks therefore
  all exercise the Fresnel-enabled code path by construction; this is a pre-existing, documented,
  cross-backend API-completeness gap (not introduced or worsened by this file) rather than a new
  finding.

## Positive Findings

- Checks A/B/C are individually well-designed, real pixel-readback discriminators for their
  stated claims, an improvement over several sibling files in this batch that only check
  exception-absence.
- The vertex shader correctly omits the Y-flip bug already confirmed on Vulkan for the equivalent
  shader — a clean result worth recording, not merely an absence of a finding.
- Correctly reuses a proven `RenderTarget2D::GetData()` readback path rather than depending on the
  swapchain (which this backend genuinely cannot read, per `SDLGPU-39`).

## Final Assessment

The three checks this file makes are correct and pass for the right reasons — no defect in what
this file specifically asserts. However, auditing "is this test correct" necessarily required
verifying the production code it exercises, and that verification surfaced a real, independently-
confirmed FNA-parity defect (F1) in `env_map3d.frag.glsl`'s emissive-color handling — the fourth
backend now confirmed to share this exact `EnvironmentMapEffect` bug class, invisible to this file
specifically because it never varies `DiffuseColor` away from `(1,1,1)`. This is the single most
significant finding in this batch and should be escalated to `AUDIT_CROSS_CUTTING_FINDINGS.md`'s
existing Bgfx/Vulkan `EnvironmentMapEffect` emissive*diffuse entry.
