# Audit: examples/vulkan_basiceffect_specular_test.cpp

## Metadata

- Source file: `examples/vulkan_basiceffect_specular_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-vulkan` shard — Vulkan backend `BasicEffect` specular-highlight pixel test
  (Task 886)
- File type: standalone `Game`-subclass executable, CTest-registered (`cna_test_vulkan_basiceffect_specular` /
  `Vulkan_BasicEffect_Specular`, `cmake/Tests/VulkanTests.cmake:527-529`)
- XNA/FNA relevance: direct — `BasicEffect.SpecularColor`/`SpecularPower`/`DirectionalLight0.SpecularColor`,
  `EyePosition` derivation (`IEffectLights`), `PreferPerPixelLighting`
- FNA reference: `HLSL/Lighting.fxh` (half-vector Blinn-Phong `ComputeLights`), `HLSL/Common.fxh`'s
  `AddSpecular` macro (specular added after texture×diffuse, scaled by resulting alpha)
- Related production code: `src/Microsoft/Xna/Framework/Graphics/BasicEffect.cpp::FillGpuDrawParams()`
  (lines 109-131, specular/emissive/eye-position forwarding, lit-path-only gating),
  `src/CNA/Internal/Backends/Vulkan/shaders/lit_textured3d_vertexlit.vert.glsl`/`.frag.glsl` (Task 1103,
  Vulkan's real default `PreferPerPixelLighting=false` per-vertex-lit path — this test never sets the flag, so
  it exercises this pipeline, not the per-pixel one).

## Purpose

4-check pixel test proving `BasicEffect`'s half-vector Blinn-Phong specular model: (a) baseline eye position;
(b) a different `EyePosition` (via `View`) producing a different specular value, proving `EyePosition`-dependence;
(c) `SpecularColor=(0,0,0)` → pure diffuse+ambient, proving the material gate; (d)
`DirectionalLight0.Enabled=false` → ambient-only, proving the light's `Enabled` gate zeroes specular too, not
just diffuse. The header (lines 66-79) documents a Task 1103 correction: since Vulkan's real default is
`PreferPerPixelLighting=false`, this scene (sampled at the exact diagonal seam of the quad's two triangles) reads
a Gouraud-interpolated average of the two shared vertices' own specular terms, not a fresh per-fragment
evaluation — matching the identical Task-1102 correction already audited on the EasyGL sibling.

## Executive Verdict

**Needs attention** — checks (a), (c), (d) are independently re-derived and confirmed correct against both FNA's
half-vector formula and the actual `lit_textured3d_vertexlit.vert.glsl` shader math. Check (b), however, carries
over the *exact same* stale expected constant already found and documented in this audit's prior-batch report
for `examples/easygl_basiceffect_specular_test.cpp` — `kExpectedOffAxisEye=(68,68,68)` is the historical,
pre-Task-1102-correction per-pixel value, not the current per-vertex-lit value (~61), and only passes because it
falls inside the check's own `±10` tolerance.

## Checklist Results

### API / XNA / FNA parity
`setSpecularColorProperty`/`setSpecularPowerProperty`/`DirectionalLight0.setSpecularColorProperty` map correctly
to FNA's `IEffectLights` surface. `EyePosition` is (correctly) not set directly — `BasicEffect::
FillGpuDrawParams()` derives it from `View` via `Matrix::Invert(View).getTranslationProperty()`
(`BasicEffect.cpp:126-127`), matching FNA's own internal derivation exactly, and this test supplies `View` via
`Matrix::CreateLookAt` for both eye positions (lines 142, 181).

### Behavioral correctness
Re-derived the half-vector formula by hand for case (a) (`kEyeStraightOn=(0,0,3)`,
`kLightDirRaw=(0.5,0,-1)` normalized, `kNormal=(0,0,1)`, `SpecularPower=32`), using
`lit_textured3d_vertexlit.vert.glsl`'s actual per-vertex formula (lines 64-86: half-vector `h=normalize(E-nL)`,
`spec=pow(max(dot(h,N),0)*zeroL, power)`, summed across enabled lights, `*specularColorPower.xyz`, Gouraud-
interpolated to the fragment along with `fragLitRGB`):
- Diffuse: `dot(-lightDir,N)=0.894427`; `lit=(ambient+lightDiffuse*0.894427)*materialDiffuse
  =(0.02+0.4472)*0.4≈0.18685` per channel.
- Per-vertex specular at `TL=(-1,1,0)`: `eyeVector≈(0.3015,-0.3015,0.9045)`; `h≈normalize(eyeVector-lightDir)
  ≈(-0.0796,-0.1648,0.9830)`; `dot(h,N)=0.983`; `spec=pow(0.983,32)≈0.579`.
- Per-vertex specular at `BR=(1,-1,0)`: `h≈(-0.7487,0.3015,1.7990)` normalized→`dot(h,N)≈0.912`;
  `spec=pow(0.912,32)≈0.053`.
- Gouraud average ≈`0.316`; total ≈`0.1869+0.316=0.503`→≈`128`, rendered `127` (1-unit gap is ordinary GPU
  floating-point/interpolation rounding) — **matches `kExpectedStraightOn(127,127,127)` exactly**, and matches
  the independently-confirmed value already established for the physically-identical scene in the EasyGL
  sibling's own audit.
- Checks (c)/(d): `SpecularColor=(0,0,0)` zeroes `specularColorPower.xyz` in the UBO
  (`BasicEffect.cpp:121-123`→`lp.specularColorPower.xyz`), leaving pure `diffuse+ambient`
  (`kExpectedNoSpecular(48,48,48)` — matches `(ambient+NdotL*lightDiffuse)*materialDiffuse=(0.02+0.4*0.8944)*0.4
  ≈0.1878`… actually more precisely `(0.02+0.5*0.8944)*0.4≈0.1868`, ×255≈48 — matches). `DirectionalLight0.
  Enabled=false` is gated by `BasicEffect::FillGpuDrawParams()` lines 85-87 (`ld`/`ls` both forced to
  `Vector3::Zero` when `!light0On`), zeroing *both* diffuse and specular contributions — giving the ambient-only
  `kExpectedLightDisabled(2,2,2)` (`0.02*0.4=0.008`, ×255≈2 — matches).

### Logic
Check (b)'s own numeric derivation is **not** independently re-verified against the current per-vertex-lit
formula the way check (a) was — this is the seam that reproduces the EasyGL sibling's already-documented F1.

### C++ correctness
`matches(...,10)`'s tolerance (lines 105-110) is the mechanism that (accidentally) makes check (b) pass despite
its stale constant, identical to the EasyGL sibling's own F1 mechanism.

### Robustness
Checks (c)/(d) correctly isolate two independently-failable hypotheses (material gate vs. light-enabled gate) —
both covered, both confirmed correct against the actual UBO-population code in `BasicEffect.cpp`.

### Testing
Three of four checks are strong, evidence-backed, independently re-derived pixel assertions. The fourth (check
b) exists mainly to prove `EyePosition`-dependence via `!matches(b,a)` (line 184), which does hold (`68` clearly
differs from `127`) — so the file's *discriminating* goal for check (b) is not defeated, but the specific
`matches(b, kExpectedOffAxisEye)` assertion (lines 182-183) verifies the wrong number for the wrong (historical)
reason.

## Detailed Findings

### F1 — Check (b)'s expected constant (`kExpectedOffAxisEye=68`) is the stale, pre-Task-1102/1103 per-pixel value, carried over unchanged from the already-audited EasyGL sibling; passes only by tolerance overlap

- Severity: MEDIUM
- Confidence: HIGH (this exact constant, tolerance, and scene were independently confirmed stale in this audit's
  own prior-batch report for `examples/easygl_basiceffect_specular_test.cpp`, which re-derived the true current
  per-vertex-lit value for the physically identical `kEyeOffAxis=(3,0,1)` scene as ~61, not 68; the Vulkan
  shader's per-vertex Blinn-Phong formula is byte-for-byte the same shape as EasyGL's, confirmed by direct
  reading of `lit_textured3d_vertexlit.vert.glsl` above, so the same ~61 real value applies here too)
- Category: test-coverage / correctness-of-test
- Location/symbol: `kExpectedOffAxisEye(68, 68, 68, 255)` (line 77); check `(b)` (lines 181-184)
- Evidence: this file's own header (lines 66-76) explicitly documents that `127` (case a) is "the new, correct
  value... confirmed against the identical scene's own independently-derived value on EasyGL... Task 1102
  update" — but does not carry the same scrutiny to case (b)'s constant, which is numerically identical to the
  value the EasyGL audit found to be the *superseded* per-pixel constant there. Given the identical geometry,
  identical light/eye parameters, and a confirmed-identical per-vertex Blinn-Phong shader formula, the current,
  correct per-vertex-lit rendered value for this scene should likewise be ~61, not 68.
- Why it matters: this specific assertion does not actually verify the current per-vertex-lit formula's output
  at the off-axis eye position — it verifies that the output is *within 10* of a value that is very likely the
  historical, pre-correction per-pixel value for this exact scene. A regression that shifted the true rendered
  value from ~61 to, say, 65 would pass unnoticed for the wrong reason; a regression to 79 would fail for the
  wrong reason.
- FNA/XNA comparison: N/A (test-authoring issue; the underlying `BasicEffect` behavior itself is independently
  confirmed correct via case (a)'s exact re-derivation above).
- Related files: `examples/easygl_basiceffect_specular_test.cpp` (identical finding, already documented in this
  audit's prior batch).
- Suggested future action (not implemented by this audit): re-derive and update `kExpectedOffAxisEye` to the
  currently-correct per-vertex-lit value for this scene (precisely computed, not the "~61" approximation), the
  same remediation already suggested for the EasyGL sibling.

## Cross-File Observations

- `vulkan_basiceffect_preferperpixellighting_test.cpp` (audited alongside this file) reuses this file's own case
  (a) scene verbatim and independently confirms both the vertex-lit (`127`) and pixel-lit (`155`) values by hand
  — see that report; both values check out exactly.
- This file, `vulkan_basiceffect_one_light_test.cpp`, and `vulkan_basiceffect_multilight_emissive_test.cpp` all
  exercise the shared `lit_textured3d`/`lit_textured3d_vertexlit` Blinn-Phong implementation; F1 here only
  affects this file's check (b), not the shared production code (independently confirmed correct via case (a)'s
  exact re-derivation and via `preferperpixellighting_test.cpp`'s cross-check of both dispatch paths).
- `RasterizerState::CullNone` (line 159, "Task 896"/"Task 908" comment) is accurate and current — confirmed via
  `RasterizerState.cpp`'s default `CullCounterClockwiseFace` (matching FNA) and the Vulkan pipeline's
  cull-mode mapping, and via `git log` showing the referenced Task 909 commit
  (`078f879d fix(Task 909): add missing RasterizerState::CullNone to 2 Vulkan BasicEffect tests`) actually
  touches this file.
- `SpecularColor` default (`Vector3{1,1,1}`, `BasicEffect.hpp:363`) and per-light `SpecularColor` default
  (`Vector3::Zero`, `DirectionalLight.hpp`, default-constructed) were checked in the course of this audit's
  cross-file work on the sibling `multilight_emissive`/`one_light` files, confirming those files' omission of
  explicit `SpecularColor` setup is safe (each light's own specular defaults to zero) — not directly relevant to
  this file, which does set `DirectionalLight0.SpecularColor` explicitly.

## Missing or Weak Tests

- See F1 — check (b)'s own expected value should be refreshed to match the currently-computed per-vertex-lit
  result rather than the historical per-pixel one, exactly as already flagged for the EasyGL sibling.
- No case combines specular with `FogEnabled=true` — reasonable scope limit, but worth noting the fog-formula
  defect found in this batch's `vulkan_basiceffect_fog_test.cpp`/`textured3d_fog_test.cpp` reports would also
  affect a specular+fog scene on this pipeline (`lit_textured3d`/`lit_textured3d_vertexlit` share the same fog
  term), untested here.

## Positive Findings

- Checks (a)/(c)/(d) are precise, correctly-scoped, and their numeric derivations were independently confirmed
  by this audit against both FNA's `Lighting.fxh`/`Common.fxh` formula shape and the live Vulkan shader source.
- `DirectionalLight0.Enabled=false` zeroing *both* diffuse and specular (check d) is a genuinely easy-to-miss XNA
  behavior and this test explicitly isolates it, matching production code exactly
  (`BasicEffect.cpp:85-87`).
- The header's own account of Task 897/886/898's UBO-infrastructure history (DirectionalLight1/2+Emissive UBO
  extended with world/eyePos/specular data) is corroborated by direct inspection of `VulkanGraphicsBackend.cpp`'s
  `litUboData` population code (indices 0-63 map exactly to the shader's `LitLightParams` struct layout).

## Final Assessment

A strong specular test overall — three of four checks independently re-derived and confirmed correct — let down
by one stale numeric constant (check b) that reproduces, unchanged, the exact same historical-value oversight
this audit already found and documented in the EasyGL sibling test for the identical physical scene.
