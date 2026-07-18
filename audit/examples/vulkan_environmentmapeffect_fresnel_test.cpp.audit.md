# Audit: examples/vulkan_environmentmapeffect_fresnel_test.cpp

## Metadata

- Source file: `examples/vulkan_environmentmapeffect_fresnel_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-vulkan` shard — `EnvironmentMapEffect` Fresnel-weighting pixel test
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_vulkan_test(cna_test_vulkan_environmentmapeffect_fresnel …)` /
  `cna_register_backend_test(NAME Vulkan_EnvironmentMapEffect_Fresnel …)`, `cmake/Tests/VulkanTests.cmake:295-297`).
- XNA/FNA relevance: direct — `EnvironmentMapEffect.FresnelFactor`/`EnvironmentMapAmount` interplay
  (`ComputeFresnelFactor` in `HLSL/EnvironmentMapEffect.fx`).
- FNA reference: `HLSL/EnvironmentMapEffect.fx` lines 61-66
  (`ComputeFresnelFactor`: `pow(max(1-abs(dot(eyeVector,worldNormal)),0),FresnelFactor)*EnvironmentMapAmount`),
  `EnvironmentMapEffect.cs` (`FresnelFactor` setter: default `1`, "Fresnel only affects the environment map
  RGB... The alpha contribution (controlled by `EnvironmentMapSpecular`) is not affected").
- Related production code: `src/Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.cpp`
  (`setFresnelFactorProperty()` lines 272-284, `FillGpuDrawParams()` line 415), `src/CNA/Internal/Backends/
  Vulkan/shaders/env_map3d.frag.glsl` (`blendFactor` computation, lines 45-48).

## Purpose

Two-check pixel test: (a) camera/view set to `Matrix::Identity` (a near-degenerate configuration where the
interpolated eye vector at the sampled centre pixel is nearly perpendicular to the surface normal, giving
`viewAngle≈0`) → the Fresnel term reduces to the same value a flat `EnvironmentMapAmount` blend would give,
explicitly called out by the file's own label as "not discriminating"; (b) a real `CreateLookAt`/
`CreatePerspectiveFieldOfView` camera placed head-on to the quad (`viewAngle≈1`) → Fresnel should suppress the
cube-map contribution almost entirely, which a flat-`Amount` implementation would *not* do. The header comment
states this test found and fixed a real historical gap: CNA previously had **no Fresnel uniform in any of the 3
backends**, always using the flat `EnvironmentMapAmount` regardless of view angle.

## Executive Verdict

**Healthy** — both checks were independently re-derived by this audit against `ComputeFresnelFactor`'s exact
formula and match the file's expected constants precisely; check (b) genuinely discriminates real Fresnel
behavior from a flat-amount fallback, and the file's own comment on check (a) not discriminating is accurate
self-disclosure rather than an overclaim.

## Checklist Results

### API / XNA / FNA parity
`setFresnelFactorProperty` (not explicitly called here — relies on the constructor default `FresnelFactor=1`)
matches FNA's own default of `1` (`EnvironmentMapEffect.cs` constructor: `FresnelFactor = 1;`), confirmed by
reading `EnvironmentMapEffect.hpp`'s `fresnelFactor_ = 1.0f` and `fresnelEnabled_ = true` field defaults plus
the constructor's own `setFresnelFactorProperty(1.0f)` call — this test relies on that default rather than
setting it explicitly, which is a faithful match to FNA's own default-Fresnel-on behavior.

### Behavioral correctness
Re-derived both checks:
- `EmissiveColor=(0.5,0.5,0.5)`, `DiffuseColor` default `(1,1,1)`, texture `(200,100,50,255)`, `EnvironmentMap
  Amount=1`, `EnvironmentMapSpecular=0`. `litRGB=(0.5,0.5,0.5)`; `baseColor=litRGB*texColor=(0.392,0.196,0.098)`
  ≈`(100,50,25)` in 0-255.
- (a) Identity view/proj places `EyePosition=(0,0,0)` (inverse-identity translation) coincident in Z with the
  quad plane (`z=0`); the per-vertex `vEyeDir` values at the 4 corners sum to zero, so the barycentric
  interpolation at (or extremely near) the sampled centre pixel gives a near-zero eye vector whose normalized
  direction is roughly perpendicular to `N=(0,0,1)` — `viewAngle=dot(E,N)≈0` → Fresnel term
  `pow(max(1-0,0),1)*1=1`, identical to the flat-`Amount=1` case. `mix(baseColor, envSample*combinedAlpha, 1)
  =envSample=(128,128,128)` (the gray cube's own color) — matches the file's expected `Color(128,128,128,255)`
  exactly, and matches the label's own "not discriminating" claim (both formulas give `blendFactor=1` here).
- (b) `CreateLookAt(Vector3(0,0,3), Vector3.Zero, up)` + `CreatePerspectiveFieldOfView` places the camera
  directly in front of the quad along its normal, giving `viewAngle≈1` (eye vector ≈ parallel to `N`). Fresnel
  term `=pow(max(1-abs(1),0),1)*1=pow(0,1)=0` → `mix(baseColor, envSample, 0)=baseColor=(100,50,25)` — matches
  the file's expected `Color(100,50,25,255)` exactly. Since a flat-`Amount=1` implementation would instead give
  `blendFactor=1` (full cube-map contribution, `(128,128,128)`), this check genuinely distinguishes real Fresnel
  from the historical flat-amount bug the header comment describes.

### Logic
`dev.setRasterizerStateProperty(RasterizerState::CullNone)` (line 119) is applied unconditionally before both
sub-checks, correctly matching this quad's winding needing the override (per the Task 896 comment), consistent
with the sibling `multilight`/`specular`/`worldtransform` tests in this batch that need the identical override
for the same quad-construction pattern.

### C++ correctness
No lifetime or cast concerns; `grayCube`/`tex` outlive both `renderWith()` calls (declared before use, in
`Draw()`'s local scope).

### Robustness
`colourMatch()`'s tolerance of `20` (line 64) is tight enough that it would not mask a materially wrong Fresnel
exponent or a missing `EnvironmentMapAmount` multiplication, while being loose enough to absorb ordinary
GPU-interpolation noise — appropriately scoped for the magnitude of difference being tested (128 vs 100/50/25 is
a much larger gap than the tolerance).

### Testing
Both checks are real, evidence-backed pixel assertions; check (a)'s self-admitted "not discriminating" label
is an honest disclosure, not a defect — it exists to establish a baseline reading before check (b) demonstrates
the actual Fresnel-dependent behavior.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings. One LOW/INFO observation below.

### F1 — Check (a)'s eye vector is near-degenerate (interpolated value close to the zero vector), relying on pixel-center sub-pixel offset rather than a deliberately-chosen non-degenerate direction

- Severity: LOW
- Confidence: MEDIUM (the geometry is confirmed to produce a near-zero interpolated eye vector at the screen
  centre; whether the actual rendered pixel is close enough to exact center to hit true `(0,0,0)` — which would
  make `normalize()` return NaN/undefined rather than a well-defined near-perpendicular direction — depends on
  the rasterizer's exact pixel-center sampling convention, not verified by running the backend in this pass)
- Category: robustness / test-design-fragility
- Location/symbol: `renderWith()` (lines 92-109), quad vertices (lines 129-136), `view=Matrix::getIdentityProperty()`
  branch of check (a) (line 139)
- Evidence: with `View=Identity`, `EyePosition=(0,0,0)`; quad corners at `z=0` give `vEyeDir` values
  `(1,-1,0),(1,1,0),(-1,1,0),(-1,-1,0)` at the 4 corners (in the order used), which sum to exactly zero — so
  the exact geometric center of the quad interpolates to `vEyeDir=(0,0,0)`, and `normalize(vec3(0))` is
  undefined (0/0) in GLSL. The actually-sampled pixel is offset from true center by the rasterizer's
  half-pixel convention (for a 64×64 target, pixel (32,32)'s center is at NDC `≈(0.0156,0.0156)`, not exactly
  `(0,0)`), so in practice the interpolated eye vector at the sampled pixel is small-but-nonzero and
  well-defined — this audit did not find evidence of an actual NaN/garbage read, and the checked value (128,
  128, 128) matching exactly is itself evidence the shader produced a coherent result, not a NaN-propagated one.
- Why it matters: this is a fragile-by-construction test setup — a future change to viewport size, pixel-center
  convention, or `readCenter()`'s sampled coordinate could shift the sampled pixel closer to the true degenerate
  center and introduce flaky `NaN`/undefined-direction sampling. It happens to work today, but the design relies
  on an unstated coincidence (half-pixel offset) rather than a deliberately non-degenerate eye position.
- FNA/XNA comparison: N/A — this is a test-construction concern, not an XNA/FNA behavioral question.
- Suggested future action (not implemented by this audit): use a camera position with a small but definite
  offset (e.g. `Vector3(0, 0, 0.001f)` translation via `View`) instead of true `Identity`, to guarantee a
  well-defined non-degenerate eye vector at every corner without changing the intended "near-zero viewAngle"
  semantics of check (a).

## Cross-File Observations

- This file and `vulkan_environmentmapeffect_specular_test.cpp` share the same `renderWith()`/`makeSolidCube()`
  helper structure and the same `EmissiveColor=(0.5,0.5,0.5)`/tex-color `(200,100,50,255)` baseline scene setup —
  their `baseColor=(100,50,25)` intermediate is reused identically across both files' derivations (independently
  confirmed by this audit in both reports).
- `FillGpuDrawParams()`'s `p.fresnelEnabled = fresnelEnabled_;` (`EnvironmentMapEffect.cpp` line 415) and the
  frag shader's `ep.light0Diff_fresnelEn.w` gate (`env_map3d.frag.glsl` line 46) together correctly implement
  FNA's documented behavior that Fresnel is a global on/off toggle keyed by `FresnelFactor != 0`, not merely a
  continuous multiplier that happens to equal 1 when disabled — consistent with `FresnelFactor`'s C# setter
  semantics ("Setting this property to 0 disables Fresnel entirely").

## Missing or Weak Tests

- No check in this file explicitly sets `FresnelFactor=0` to verify the "disabled" branch collapses to the flat
  `EnvironmentMapAmount` behavior directly (as opposed to relying on check (a)'s incidental `viewAngle≈0`
  coincidence, which produces the same numeric result but for a different reason). A dedicated
  `setFresnelFactorProperty(0.0f)` check with a genuinely oblique camera would close this gap.

## Positive Findings

- Both checks' expected constants were independently re-derived by this audit from the real `ComputeFresnelFactor`
  formula and FNA's documented default (`FresnelFactor=1`), and match exactly, not merely within tolerance.
- Check (b) is a well-chosen, genuinely discriminating scene (head-on camera) that directly falsifies the
  historical flat-`EnvironmentMapAmount` bug the header comment describes, rather than merely re-confirming a
  trivial case.
- The header comment's claim of finding "NO Fresnel uniform ... in any of the 3 backends" is a serious,
  specific historical-bug claim; this test's structure (mirrored across EasyGL/Bgfx/Vulkan per the comment)
  is consistent with a genuine cross-backend fix rather than a Vulkan-only patch.

## Final Assessment

A solid, correctly-derived two-check Fresnel test whose only weakness is a mildly fragile near-degenerate eye
vector in check (a) (works today via incidental pixel-center offset) and the absence of an explicit
`FresnelFactor=0` regression check — neither rises above LOW severity, and the core Fresnel formula this file
verifies is confirmed correct against both the FNA reference and the current Vulkan shader.
