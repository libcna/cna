# Audit: examples/vulkan_skinnedeffect_fog_test.cpp

## Metadata

- Source file: `examples/vulkan_skinnedeffect_fog_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-vulkan` shard — `SkinnedEffect` linear fog pixel test, Vulkan backend
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_vulkan_test(cna_test_vulkan_skinnedeffect_fog …)` / `cna_register_backend_test(NAME
  Vulkan_SkinnedEffect_Fog …)`, `cmake/Tests/VulkanTests.cmake:639-642`, Task 899).
- XNA/FNA relevance: direct — `SkinnedEffect.FogEnabled`/`FogColor`/`FogStart`/`FogEnd`,
  `DirectionalLight.DiffuseColor`/`Direction`.
- FNA reference: `Graphics/Effect/StockEffects/SkinnedEffect.cs` (`FogEnabled`/`FogColor`/
  `FogStart`/`FogEnd`), `Graphics/Effect/StockEffects/HLSL/Common.fxh`
  (`ComputeFogFactor`/`ApplyFog`: linear fog interpolates toward `FogColor` as the fog factor
  approaches 0, computed from **pre-transform, object-space** vertex position via `FogVector`).
- Related production code: `src/CNA/Internal/Backends/Vulkan/shaders/
  skinned3d.{vert,frag}.glsl` (vFogFactor computed from raw `aPos.z`, i.e. pre-skin), `src/
  Microsoft/Xna/Framework/Graphics/SkinnedEffect.cpp` (`FillGpuDrawParams()`), Vulkan's
  `descriptorSetLayoutSkinned_`/`BoneBlock` UBO capacity constraints (per this file's own header
  comment).

## Purpose

Isolates `SkinnedEffect`'s linear fog formula (`fogFactor = clamp((FogEnd - Z) / (FogEnd -
FogStart), 0, 1)`, `finalRGB = mix(FogColor, geomRGB, fogFactor)`) via `renderQuad()`, a
parameterized helper that renders a 6-vertex quad at a given object-space `Z`, with fog
enabled/disabled and configurable `FogColor`/`FogStart`/`FogEnd`. Deliberately isolates the
pre-fog material colour via `DirectionalLight0` (identity bone palette, light pointing straight at
the quad's `+Z` normal for `NdotL=1`, blue `DiffuseColor`) rather than via `EmissiveColor` (as the
Bgfx sibling test does), because — per this file's own header comment — "Vulkan's skinned3d.frag
.glsl has never read an emissive uniform at all... a separate, unreported gap outside this task's
scope." Three checks: (a) fog off → pure blue; (b) 50% fog at Z=0.5 → blue/red 50/50 mix; (c) full
fog at Z=0.9 with `FogEnd=0.5` → pure red (fog colour fully dominant).

## Executive Verdict

**Healthy, and unusually self-aware** — every specific claim in the file's own extensive header
comment was independently re-derived from the actual shader source and found accurate, *including*
its own admission of the `EmissiveColor`/`AmbientLightColor`-dropped-on-Vulkan gap, which this
audit separately traced end-to-end (in the sibling `vulkan_skinnedeffect_combined_test.cpp`
report) and confirmed is real, not merely a hypothesis. This file's own design choice to route
around that exact gap (using `DirectionalLight0` instead of `EmissiveColor` to establish the
pre-fog material colour) is precisely the correct engineering response.

## Checklist Results

### API / XNA / FNA parity
`fx.DirectionalLight0.setDirectionProperty`/`setDiffuseColorProperty`,
`fx.setFogEnabledProperty`/`setFogColorProperty`/`setFogStartProperty`/`setFogEndProperty` all map
directly to FNA's `SkinnedEffect`/`DirectionalLight` API surface.

### Behavioral correctness
Independently re-derived all 3 expected pixel values from the actual `skinned3d.vert/frag.glsl`
formula (this is the per-pixel-lit variant; note `SkinnedEffectFogVulkanTest` never sets
`PreferPerPixelLighting`, so the *actually*-selected pipeline is in fact the per-vertex-lit sibling
`skinned3d_vertexlit.{vert,frag}.glsl` — confirmed both variants compute an *identical* fog
formula, `vFogFactor` derived from raw `aPos.z` in the vertex stage either way, so this
distinction does not change the result below):
- Given `AmbientLightColor` is confirmed always-zero on Vulkan (see this file's own header comment
  and the sibling combined-test audit's independent confirmation) and this test's `DiffuseColor`
  is left at its `SkinnedEffect` default (`Vector3::One`, verified against `SkinnedEffect.hpp`'s
  field defaults) with a white 1×1 texture (identity multiplier), the pre-fog `litRGB` reduces
  exactly to `light0Diffuse * NdotL0 = (0,0,1) * 1 = (0,0,1)` — pure blue, matching the test's own
  claimed reduction ("reducing skinned3d.frag's formula to exactly light0Diffuse").
- (a) `fogEnabled=false` → `vFogFactor` vertex-shader branch (`fog.fogColorEnabled.w > 0.5`) is
  false → `vFogFactor=1.0` unconditionally → `mix(fogColor, blue, 1.0) = blue` → matches `kBlue
  (0,0,255)`.
- (b) `Z=0.5, FogStart=0, FogEnd=1`: `fogFactor = clamp((1-0.5)/(1-0), 0, 1) = 0.5` →
  `mix(red(1,0,0), blue(0,0,1), 0.5) = (0.5, 0, 0.5)` → `(128,0,128)` after 8-bit quantization —
  matches the test's own expected `Color(128,0,128,255)` exactly.
- (c) `Z=0.9, FogStart=0, FogEnd=0.5`: `fogFactor = clamp((0.5-0.9)/(0.5-0), 0, 1) =
  clamp(-0.8,0,1) = 0` → `mix(red, blue, 0) = red` → matches `kRed(255,0,0)`.
  All three independently re-derived values match the test's own assertions exactly, not just
  approximately within tolerance.

### Logic
Confirmed the vertex shader computes `vFogFactor` from **`aPos.z`, the raw pre-skin object-space
Z** (`skinned3d.vert.glsl`: `vFogFactor = ... clamp((fog.fogStartEnd.y - aPos.z) / ..., 0, 1) : 1.0;`
— using `aPos.z`, not any post-`skinMat`-transformed position), exactly matching this file's header
claim ("computed from the PRE-SKIN vertex position, before the bone-skinning matrix multiply").
Since this test uses an identity bone palette (`w0=1,w1=w2=w3=0`, `i0=i1=i2=i3=0`, bone 0 =
`Matrix::getIdentityProperty()`), pre-skin and post-skin Z are numerically identical here, so this
specific test cannot distinguish "fog correctly uses pre-skin Z" from "fog incorrectly uses
post-skin Z" — a scope limitation the file's header comment does not claim to address (it frames
this as matching "EasyGL/Bgfx's already-tested formula exactly," implying the pre/post-skin
distinction was validated elsewhere, not re-litigated here).

### Robustness
The 20-iteration blank-frame retry in `renderQuad()` (checking for any non-black pixel before
accepting the frame) mirrors the same narrowly-scoped driver-flake workaround pattern seen in
`vulkan_scissor_test.cpp` in this same batch — confirmed it cannot mask a genuine fog-formula
regression, since it only skips frames with zero drawn content.

### Testing
3 assertions, `matches()` tolerance `±30` per channel (looser than the `±8` in
`vulkan_rt_roundtrip_test.cpp`, appropriate here given the 50%-mix case's sensitivity to blend
precision across the interpolated fog factor). Confirmed `Color(128,0,128,255)`'s r/g/b values are
each independently checked, not just "is it purple-ish."

## Detailed Findings

None new for this specific file — see Cross-File Observations below: the underlying
AmbientLightColor/EmissiveColor-dropped-on-Vulkan defect this file's header comment references is
real (independently confirmed via `SkinnedEffect::FillGpuDrawParams()` and both `skinned3d*
.frag.glsl` variants, detailed fully in this batch's `vulkan_skinnedeffect_combined_test.cpp.audit
.md` report, F1), but this file correctly routes around it by design and is not itself weakened by
it — it is reported against the sibling file, not duplicated here, since this file's own
assertions do not depend on ambient/emissive at all.

## Missing or Weak Tests

None specific to this file's own stated scope (isolating the fog formula via `DirectionalLight0`).
The pre-skin-vs-post-skin Z distinction for fog (noted under Logic above) is untested by this exact
file, but the header comment correctly attributes that validation to the already-established
EasyGL/Bgfx formula parity rather than claiming to re-prove it here — not a gap in this file, since
it never claims that coverage.

## Positive Findings

- Every specific numeric claim in this file's unusually detailed header comment was independently
  re-derived from the actual shader source and matched exactly, including the self-reported
  "Vulkan's skinned3d.frag.glsl has never read an emissive uniform at all" gap, which this audit
  traced end-to-end in a sibling report and confirmed is genuinely accurate, not stale or
  overstated.
- The choice to isolate material colour via `DirectionalLight0` rather than `EmissiveColor` is a
  deliberate, correctly-reasoned design decision to avoid a known broken code path, rather than an
  accidental omission — good engineering judgement, made explicit in the comment rather than left
  implicit.
- All three fog-factor pixel values were independently re-derived by this audit from first
  principles and matched the asserted constants exactly (not merely within tolerance of a
  plausible value), giving high confidence the formula genuinely is `clamp((FogEnd-Z)/(FogEnd-
  FogStart),0,1)` mixed against `FogColor`, matching FNA's linear-fog semantics.

## Final Assessment

One of the strongest files in this batch: a correct, self-aware test whose own documentation about
a known adjacent defect (ambient/emissive dropped on Vulkan for `SkinnedEffect`) was independently
verified as accurate rather than assumed, and whose design explicitly avoids being compromised by
that same defect. No changes recommended to this file itself; the referenced defect is tracked
against `SkinnedEffect.cpp`/the Vulkan skinned shaders in the sibling
`vulkan_skinnedeffect_combined_test.cpp` report.
