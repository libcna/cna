# Audit: examples/vulkan_environmentmapeffect_specular_test.cpp

## Metadata

- Source file: `examples/vulkan_environmentmapeffect_specular_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-vulkan` shard — `EnvironmentMapEffect.EnvironmentMapSpecular` pixel test
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_vulkan_test(cna_test_vulkan_environmentmapeffect_specular …)` /
  `cna_register_backend_test(NAME Vulkan_EnvironmentMapEffect_Specular …)`, `cmake/Tests/VulkanTests.cmake:281-283`).
- XNA/FNA relevance: direct — `EnvironmentMapEffect.EnvironmentMapSpecular` (cube-map-alpha-driven additive
  specular term).
- FNA reference: `HLSL/EnvironmentMapEffect.fx` `PSEnvMapSpecular`
  (`color.rgb += EnvironmentMapSpecular * envmap.a;` where `envmap = SAMPLE_CUBEMAP(...) * color.a`).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.cpp`
  (`setEnvironmentMapSpecularProperty()` lines 258-270), `src/CNA/Internal/Backends/Vulkan/shaders/
  env_map3d.frag.glsl` (line 52, the additive specular term).

## Purpose

Two-check pixel test proving `EnvironmentMapSpecular`'s additive contribution is scaled by the cube map's own
alpha channel (times the combined texture/diffuse alpha), not added as a flat constant. The header comment
states this test found and fixed a real bug present in **all 3 backends**: `EnvironmentMapSpecular` was being
added as a flat constant rather than `EnvironmentMapSpecular * envmap.a`, scaled further by combined alpha.
Check (a) uses a fully-opaque cube (alpha=255) as a non-discriminating baseline; check (b) uses a
half-translucent cube (alpha=128) whose scaled contribution should be roughly halved.

## Executive Verdict

**Healthy** — both checks were independently re-derived by this audit against the exact current shader formula
and match the file's expected constants to the pixel, confirming the Task 395 fix (`envSample.a * combinedAlpha`
scaling) is correctly implemented and correctly tested.

## Checklist Results

### API / XNA / FNA parity
`setEnvironmentMapSpecularProperty(Vector3(0.4f,0.4f,0.4f))` (line 100) maps directly to FNA's
`EnvironmentMapSpecular` property; its C++ setter (`EnvironmentMapEffect.cpp` lines 258-270) correctly derives
`specularEnabled_` from a non-zero check, matching FNA's own `bool enabled = (value != Vector3.Zero);` gating in
`EnvironmentMapEffect.cs`.

### Behavioral correctness
Re-derived both checks (with `EnvironmentMapAmount=0` isolating out the lerp term entirely, so only the additive
specular term matters):
- `EmissiveColor=(0.5,0.5,0.5)`, `DiffuseColor` default `(1,1,1)`, texture `kTex=(200,100,50,255)`:
  `litRGB=(0.5,0.5,0.5)`; `baseColor=litRGB*texColor=(0.392,0.196,0.098)≈(100,50,25)` in 0-255.
  `combinedAlpha=diffuseColor.a(1)*texColor.a(1)=1`.
- With `EnvironmentMapAmount=0`, `blendFactor=0` regardless of Fresnel state, so `mix(baseColor,
  envSample*combinedAlpha, 0)=baseColor` — the lerp term drops out entirely, isolating the additive term.
- (a) opaque cube, `a=255→1.0`: additive term `=EnvironmentMapSpecular(0.4)*envSample.a(1.0)*combinedAlpha(1.0)
  =0.4=102/255`. Total `=(100+102,50+102,25+102)=(202,152,127)` — matches the file's expected
  `Color(202,152,127,255)` exactly.
- (b) translucent cube, `a=128→0.502`: additive term `=0.4*0.502*1=0.2008≈51/255`. Total
  `=(100+51,50+51,25+51)=(151,101,76)` — matches the file's expected `Color(151,101,76,255)` exactly.
- Confirms the shader's `ep.envMapSpec_fresnelF.xyz * envSample.a * combinedAlpha` (`env_map3d.frag.glsl` line
  52) exactly reproduces FNA's `EnvironmentMapSpecular * envmap.a` where `envmap.a` (in FNA) is itself already
  `cubemapAlpha * color.a` — i.e. `envSample.a` (raw cubemap alpha, unscaled in the Vulkan shader) times
  `combinedAlpha` (explicit in the Vulkan shader) is mathematically identical to FNA's single `envmap.a` term.

### Logic
The near-degenerate eye vector concern already raised in the sibling Fresnel test's audit (identity
view/projection, quad centered at the origin) applies identically here (same `renderWith()` scene setup), but
is immaterial to *this* file's correctness: because the cube map is a uniform solid color across all 6 faces
(`makeSolidCube`), the reflection direction computed from that eye vector cannot affect the sampled `envSample`
value regardless of its exact (possibly near-degenerate) direction — see F1 in the Fresnel test's report for the
underlying geometry; not re-flagged here since it has zero bearing on this file's specific assertions.

### C++ correctness
No lifetime/cast issues; `opaqueCube`/`translucentCube` both outlive their respective `renderWith()` calls.

### Robustness
`colourMatch()`'s tolerance of `20` (line 63) is appropriately tight relative to the ~50-unit gap between check
(a) and (b)'s expected values — large enough to absorb ordinary interpolation noise, small enough that a
regression which dropped the alpha-scaling entirely (making both checks read `(202,152,127)`) would fail check
(b) outright (`|202-151|=51 > 20`).

### Testing
Both checks are real, evidence-backed, and exactly reproduce the intended formula; check (a) is an honest
"non-discriminating" baseline (the file's own label says so) while check (b) is the actual discriminator, which
this audit confirms genuinely separates "flat additive constant" (would give `(202,152,127)` in both checks)
from "alpha-scaled" (gives materially different values, as observed).

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings.

### F1 — Both checks share the same non-degenerate-in-practice but structurally fragile identity-view eye vector as the sibling Fresnel test

- Severity: LOW
- Confidence: MEDIUM (same underlying geometric setup already documented in this batch's Fresnel-test report;
  not independently re-verified via a live GPU run in this pass)
- Category: robustness / test-design-fragility
- Location/symbol: `renderWith()` (lines 91-107), identical scene geometry to
  `vulkan_environmentmapeffect_fresnel_test.cpp`'s check (a)
- Evidence: see the parallel finding in `vulkan_environmentmapeffect_fresnel_test.cpp.audit.md` (F1) — the same
  `View=Identity` setup produces a near-zero interpolated eye vector at the sampled centre pixel. It does not
  affect this file's correctness (uniform-color cube map makes the reflection direction irrelevant to the
  sampled value), but shares the same latent fragility should the render-target size or pixel-sampling
  convention change.
- Why it matters: low — purely a shared test-construction fragility, not a defect affecting this file's actual
  pass/fail correctness today.
- Suggested future action: none required specifically for this file; if the shared `renderWith()`-style helper
  is ever refactored across the `EnvironmentMapEffect` test family, consider a non-degenerate eye position as
  noted in the Fresnel test's report.

## Cross-File Observations

- Shares its `renderWith()`/`makeSolidCube()` structure and baseline scene (`EmissiveColor=(0.5,0.5,0.5)`,
  `kTex=(200,100,50,255)`) verbatim with `vulkan_environmentmapeffect_fresnel_test.cpp` — the `baseColor=
  (100,50,25)` intermediate this report derives is identical to, and cross-checked against, that sibling file's
  own derivation.
- The Task 395/891 provenance (`git log`: `92b6df5d fix(Task 891): EnvironmentMapEffect's cube-map lerp target
  wasn't alpha-scaled`) is corroborated by real commit history, and the shader comment explicitly distinguishes
  the Task 395 fix (specular term alpha-scaling, verified by this file) from the separate Task 891 fix (the
  lerp target's own alpha-scaling, verified instead by the fog/combined tests in this family) — these are two
  related but historically distinct fixes, both accurately attributed in the shader source comments.

## Missing or Weak Tests

None significant — the two checks together correctly isolate and verify the alpha-scaling behavior this file
exists to test.

## Positive Findings

- Both expected constants were independently re-derived by this audit from the exact current shader formula and
  match to the pixel, not merely within tolerance — strong evidence the Task 395 fix is genuinely correct, not
  just passing by accident.
- `EnvironmentMapAmount=0` is a precise, minimal isolation of the additive specular term from the lerp term,
  making the test's actual discriminating logic easy to verify independently (as done in this audit).

## Final Assessment

A precise, correctly-derived two-check test that accurately verifies `EnvironmentMapSpecular`'s cube-map-alpha
scaling fix, with both expected values confirmed exact by independent re-derivation against the real shader
source and the FNA reference formula.
