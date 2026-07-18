# Audit: examples/easygl_skinnedeffect_preferperpixellighting_test.cpp

## Metadata

- Source file: `examples/easygl_skinnedeffect_preferperpixellighting_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — `SkinnedEffect.PreferPerPixelLighting` dispatch test
- File type: hand-rolled `Game`-derived executable (not `PixelTestGame`), CTest-registered
  (`cna_easygl_test(cna_test_easygl_skinnedeffect_preferperpixellighting …)` /
  `cna_register_backend_test(NAME EasyGL_SkinnedEffect_PreferPerPixelLighting …)`,
  `cmake/Tests/EasyGLTests.cmake:642-645`).
- XNA/FNA relevance: direct — `SkinnedEffect.PreferPerPixelLighting`
  (`Microsoft::Xna::Framework::Graphics::SkinnedEffect`), real XNA 4.0 API. FNA source:
  `Graphics/Effect/StockEffects/SkinnedEffect.cs`, `HLSL/SkinnedEffect.fx` (`VSSkinnedVertexLighting*`
  vs. per-pixel `PSSkinnedVertexLighting*` families) plus the shared `Lighting.fxh` `ComputeLights()`.
- Production code exercised: `SkinnedEffect::setPreferPerPixelLightingProperty` /
  `FillGpuDrawParams` (`src/Microsoft/Xna/Framework/Graphics/SkinnedEffect.cpp`),
  `EasyGLGraphicsBackend::SelectProgram` (dispatch), `EnsureSkinnedProgram()` (per-pixel family),
  `EnsureSkinnedVertexLitProgram()` (per-vertex/Gouraud family) — all in
  `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp`.

## Purpose

Task 1102b's `SkinnedEffect` counterpart to Task 1102's `BasicEffect.PreferPerPixelLighting` test:
proves the EasyGL backend genuinely dispatches to two different shader programs (Gouraud-interpolated
per-vertex lighting vs. per-fragment lighting) depending on this one boolean, rather than always
evaluating lighting in one stage regardless of the flag's value. Uses a single Identity bone at 100%
weight so skinning itself is a mathematical no-op, isolating the lighting-mode dispatch under test —
correctly reuses the exact scene (geometry, light, material, camera) from
`easygl_skinnedeffect_specular_test.cpp`'s own "(a) eye straight on" case so the two files' expected
values are mutually corroborating rather than independently asserted magic numbers.

## Executive Verdict

**Healthy.** Traced the test's 3-way assertion structure against the real production dispatch code in
`SelectProgram()` (lines 3929-3941) and confirmed it matches exactly: `params.skinned &&
params.lightingEnabled && !params.preferPerPixelLighting` routes to
`EnsureSkinnedVertexLitProgram()` (Gouraud), the negation routes to `EnsureSkinnedProgram()`
(per-pixel) — precisely what checks (a)/(b)/(c) assert. One shared, real production defect was found
while tracing this code path (missing world-space normal transform in both skinned shader families,
detailed below) that this test's identity-World scene structurally cannot detect.

## Checklist Results

### API / XNA / FNA parity
`setPreferPerPixelLightingProperty`/`getPreferPerPixelLightingProperty` (SkinnedEffect.hpp lines
164-171) match FNA's `SkinnedEffect.PreferPerPixelLighting` property name and semantics exactly
(default `false`, real XNA's own default per `SkinnedEffect.cs`). `fx.SetBoneTransforms(std::vector<Matrix>{...})`
matches FNA's `SetBoneTransforms(Matrix[])` (mapped per this project's `std::vector<T>` ↔ `T[]`
convention).

### Behavioral correctness
The test's own header comment (lines 20-28) analytically re-derives both the vertex-lit Gouraud
average (~128, rendered 127) and per-pixel value (~155) from the *exact same* Blinn-Phong formula and
scene as `easygl_skinnedeffect_specular_test.cpp`'s own already-cross-checked "eye straight on" case,
rather than inventing a fresh, unverifiable number — a real strength (see Cross-File Observations).
Verified this against the shader itself: `EnsureSkinnedVertexLitProgram()`'s vertex stage computes
`vLitRGB`/`vSpecularRGB` once per vertex and Gouraud-interpolates via `out` varyings (lines 3510-3524),
while `EnsureSkinnedProgram()`'s fragment stage recomputes the identical Blinn-Phong terms per pixel
(lines 3362-3384) — genuinely different evaluation points, not a decorative flag.

Check (c) (`!matches(vertexLit, pixelLit)`) is a good practice: it converts a possible
"both values happen to be within tolerance of each other" false-pass into an explicit assertion that
the two paths produce genuinely different results, closing exactly the gap a lazy/no-op flag
implementation would otherwise slip through.

### Logic
`renderWith()`'s up-to-20-frame retry loop (lines 178-188), skipping frames whose center pixel reads
all-black, is a pragmatic guard against a swapchain/first-frame black flash rather than a a fixed
single-`Draw()` assumption — reasonable given `dev.Clear(Color(0,0,0,255))` on every iteration would
otherwise make a genuinely-black expected result indistinguishable from "not yet rendered," but here
harmless since none of the expected values are actually black.

### Testing
Covers only the `false`/`true` dispatch boundary for `SkinnedEffect`, correctly scoped as this task's
own goal. Does **not** cover: `PreferPerPixelLighting` combined with `WeightsPerVertex` != 1, a
non-Identity bone, VertexColorEnabled, or fog — all reasonably deferred to their own sibling test files
in this same shard (`easygl_skinnedeffect_weightspervertex_test.cpp`,
`easygl_skinnedeffect_vertexcolor_test.cpp`, etc.), not a gap unique to this file.

## Detailed Findings

### F1 — EasyGL's skinned lighting shaders never transform the normal into world space (missing `WorldInverseTranspose`-equivalent step); invisible to this test's Identity-World scene

- Severity: HIGH
- Confidence: HIGH
- Category: correctness (production shader), test-coverage gap
- Location/symbol: `EasyGLGraphicsBackend::EnsureSkinnedProgram()` vertex stage
  (`EasyGLGraphicsBackend.cpp` lines 3300-3318, specifically
  `vec3 skinnedNormal=mat3(skinMat)*aNormal; ... vNormal=...skinnedNormal/skinnedNormalLen...`) and
  `EnsureSkinnedVertexLitProgram()` vertex stage (lines 3489-3512, `vec3 N=...`); both used by this
  test's `renderWith()` via `SelectProgram()`. Test file: `fx.setWorldProperty(Matrix::getIdentityProperty())`
  (line 161).
- Evidence: `EnsureSkinnedProgram()`/`EnsureSkinnedVertexLitProgram()` compute the lit normal purely
  as `mat3(skinMat)*aNormal` — the bone-palette skin transform's rotational part only, in
  bind/object space — and never multiply by `uWorld` (or its inverse-transpose) at all, even though
  the *position* in the very same shader is correctly world-transformed
  (`vWorldPos=(uWorld*skinnedPos).xyz;`, line 3320/3506) and even though the sibling BasicEffect
  vertex-lit shader `EnsureLit3DVertexLitProgram()` does apply `uNormalMatrix`
  (`vec3 N=normalize(uNormalMatrix*aNormal);`, line 2953) — the correct, transpose-of-inverse World
  normal matrix computed in `BindDrawParams()` (lines 3997-4010). This is confirmed against FNA's
  real behavior: `SkinnedEffect.fx`'s `Skin()` skins the normal in object space
  (`vin.Normal = mul(vin.Normal, (float3x3)skinning);`, matching CNA's `mat3(skinMat)*aNormal` step),
  but `Common.fxh`'s `ComputeCommonVSOutputWithLighting()` (used by every
  `VSSkinnedVertexLighting*`/`VSSkinnedOneLight*` entry point) then does
  `worldNormal = normalize(mul(normal, WorldInverseTranspose))` before ever calling `ComputeLights()`
  — a mandatory second step CNA's EasyGL skinned shaders skip entirely.
- Why it matters: for any `World` matrix other than Identity (i.e. virtually every real game use of
  `SkinnedEffect` — a skinned character is placed and rotated in the scene via `World`, not left at
  the origin), the normal used for lighting stays in bind/object space while the eye vector, light
  directions, and world position it is dotted against are all genuinely in world space — an
  inconsistent-space lighting computation that silently produces wrong pixels (dimmer/brighter/
  wrongly-directed lighting and specular highlights) whenever the model is rotated. This is a common
  path, not an edge case.
- FNA/XNA comparison: see Evidence — confirmed divergence from `Common.fxh`'s
  `ComputeCommonVSOutputWithLighting()`.
- Why this test cannot catch it: `fx.setWorldProperty(Matrix::getIdentityProperty())` (line 161) means
  `mat3(Identity)` is trivially both "no transform" and its own inverse-transpose — the missing step
  is a no-op for this specific scene, and remains a no-op for every other `SkinnedEffect` EasyGL test
  in this shard (all six use `Matrix::getIdentityProperty()` for World). No file in this batch would
  regress if the missing normal transform were removed entirely, and none would newly pass if it were
  added — this entire test family has a structural blind spot for World-space lighting correctness.
- Related files: `EnsurePbrSkinnedProgram()` has a related but distinct issue (uses `mat3(uWorld)`
  directly instead of an inverse-transpose normal matrix) — see the audit report for
  `easygl_skinnedpbreffect_golden_test.cpp`. Not verified whether this also affects the Vulkan/Bgfx/
  D3D9/D3D11/D3D12/SdlGpu/WebGPU skinned pipelines (out of this shard's scope) — flagged for those
  backends' own audits.
- Suggested future action (not implemented by this audit): add a `uNormalMatrix`/`uWorld`-based
  second transform after skinning in both `EnsureSkinnedProgram()` and `EnsureSkinnedVertexLitProgram()`,
  mirroring `EnsureLit3DProgram()`'s existing correct pattern; add at least one EasyGL `SkinnedEffect`
  pixel test with a non-Identity (rotated) `World` matrix and a directional light, so this class of
  regression is actually observable.

## Cross-File Observations

- This file, `easygl_skinnedeffect_specular_test.cpp`, and (by the header comment's own citation)
  `easygl_basiceffect_preferperpixellighting_test.cpp` form a deliberate cross-checking chain: each
  new file's expected numeric values are derived from, and required to exactly match, a sibling file's
  independently-observed live-render value rather than a fresh hand derivation. This is efficient and
  reduces duplicated arithmetic-error risk, but it also means a single wrong number anywhere in the
  chain (e.g. if `easygl_skinnedeffect_specular_test.cpp`'s own "(a)" value were ever wrong) would
  propagate silently into every test that cites it as its own oracle — worth a periodic independent
  re-derivation audit of the root of this chain rather than assuming it forever, though not something
  this audit found evidence of actually being wrong.
- Confirms `SelectProgram()`'s dispatch condition (`params.lightingEnabled && !params.preferPerPixelLighting`)
  is identical in shape to the already-audited `BasicEffect` stride-32 case (lines 3964-3978) —
  consistent architecture across both stock effects.

## Missing or Weak Tests

- No test in this file (or, per the check above, anywhere in this shard) exercises `SkinnedEffect`
  with a non-Identity `World` matrix while lighting is enabled — see F1. This is the single most
  valuable missing test case for this effect's EasyGL backend.
- No test combines `PreferPerPixelLighting=true` with `WeightsPerVertex=2`/`4` or a non-Identity bone
  (this file deliberately isolates skinning as a no-op, which is reasonable for *this* file's own
  narrow goal, but no other file in the shard fills that combined gap either).

## Positive Findings

- The 3-check structure (assert value A, assert value B, assert A != B) is a genuinely good pattern
  for proving a dispatch flag is live rather than decorative — stronger than most single-value pixel
  assertions elsewhere in this codebase.
- Analytical re-derivation is shown in the header comment with actual intermediate numbers
  (`specular(TL)=0.5798`, `specular(BR)=0.0531`, Gouraud average `0.3165`), not just a final expected
  color — genuinely auditable, not a black-box magic constant.

## Final Assessment

A well-constructed, correctly-passing dispatch test for a real XNA property, whose main limitation is
shared with its entire test family: an Identity-only `World` matrix convention that structurally
cannot detect the missing world-space normal transform (F1) in the exact shader programs this file
exercises.
