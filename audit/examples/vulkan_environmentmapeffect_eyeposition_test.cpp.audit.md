# Audit: examples/vulkan_environmentmapeffect_eyeposition_test.cpp

## Metadata

- Source file: `examples/vulkan_environmentmapeffect_eyeposition_test.cpp` (174 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-vulkan` shard — `EnvironmentMapEffect` `EyePosition`-driven
  reflection-vector verification, Vulkan backend (Task 397)
- File type: standalone `Game`-subclass executable
  (`VulkanEnvironmentMapEyePositionTest`), CTest-registered
- XNA/FNA relevance: direct — `EnvironmentMapEffect`'s internally-derived `EyePosition` (from
  `Invert(View)`'s translation) drives the reflection vector used to sample the environment cube
- FNA reference: `HLSL/EnvironmentMapEffect.fx` (`ComputeEnvMapVSOutput`:
  `eyeVector = normalize(EyePosition - pos_ws.xyz); ... vout.EnvCoord = reflect(-eyeVector,
  worldNormal);`)
- Related production code: `src/Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.cpp`
  (`OnApply()` lines 347-355: `EyePosition` derived from `Matrix::Invert(view_)`'s translation),
  `src/CNA/Internal/Backends/Vulkan/shaders/env_map3d.vert.glsl` /
  `env_map3d.frag.glsl` (`reflDir = reflect(-E, N)`), `include/Microsoft/Xna/Framework/Graphics/
  CubeMapFace.hpp` (face-index ordering: `PositiveX=0, NegativeX=1, PositiveY=2, NegativeY=3,
  PositiveZ=4, NegativeZ=5`)

## Purpose

Two-check test proving the reflection vector genuinely depends on the camera's `EyePosition`
(derived from `View`), not a hardcoded/hemisphere-independent value: a cube map with a distinct
color per face is rendered once from a straight-on camera `(0,0,3)` (expecting the `PositiveZ`
face, blue) and once from an off-axis camera `(5,0,0.5)` (expecting the `NegativeX` face, cyan) —
proving both that the reflection genuinely changes with eye position and that the specific face
selected is geometrically correct, not just "different from before."

## Executive Verdict

**Needs attention** — both checks' expected face selections were independently re-derived via the
actual `reflect()` formula and the project's real `CubeMapFace` layer ordering, and are correct.
As with its siblings in this batch, the test's center-pixel-only sampling cannot detect the
separately-found `env_map3d.vert.glsl` Y-flip defect (F1) — ironic for the one file in this
family whose entire purpose is to probe an asymmetric-camera scenario.

## Checklist Results

### API / XNA / FNA parity
The test never calls `setEyePositionProperty` directly — correctly, since `EnvironmentMapEffect`
in both FNA and CNA derives `EyePosition` internally from `Invert(View)`'s translation
(`EnvironmentMapEffect.cpp:347-355`), matching FNA's own `EffectHelpers.SetLightingMatrices`
convention; the test drives it indirectly via `setViewProperty`, which is the only
XNA-API-correct way to influence it.

### Behavioral correctness
Re-derived both checks against the live shader and the project's actual `CubeMapFace` layer
order (`PositiveX=0,...,NegativeZ=5`, confirmed at `CubeMapFace.hpp:7-21`, matching the standard
Vulkan/D3D cube-face-to-array-layer convention, so no face-remap bug to account for):
- (a) `View = CreateLookAt((0,0,3), origin, (0,1,0))`, `World=Identity`. Eye `(0,0,3)`,
  `worldPos≈(0,0,0)` at the sampled center pixel (camera looks directly at the origin, which maps
  to the exact viewport center under a symmetric FOV and 1:1 aspect ratio). `E =
  normalize((0,0,3)) = (0,0,1)`. `N=(0,0,1)`. `reflDir = reflect(-E,N) = reflect((0,0,-1),(0,0,1))
  = (0,0,-1) - 2*(-1)*(0,0,1) = (0,0,1)` → dominant `+Z` → `PositiveZ` face → `posZ=(0,0,255,255)`
  blue. Matches the test's expectation `Color(0,0,255,255)` (line 144).
- (b) `View = CreateLookAt((5,0,0.5), origin, (0,1,0))`. `E = normalize((5,0,0.5)) ≈
  (0.99504,0,0.09950)`. `reflDir = reflect(-E,N)`: `dot(-E,N) = -0.09950`; `reflDir = -E -
  2*(-0.09950)*N = (-0.99504,0,-0.09950)+(0,0,0.19901) = (-0.99504,0,0.09950)`. Dominant axis is
  `-X` (magnitude `0.995` vs. `Z`'s `0.0995`) → `NegativeX` face → `negX=(0,255,255,255)` cyan.
  Matches the test's expectation `Color(0,255,255,255)` (line 150).
- Both derivations independently confirm the test's own comments ("PositiveZ face (blue)",
  "NegativeX face (cyan)") rather than merely restating them.

### Logic
`FresnelFactor` is explicitly set to `0.0f` (line 105) in this test — correctly disabling the
Fresnel edge-weighting term specifically so `blendFactor` reduces to a constant
`EnvironmentMapAmount=1` regardless of `viewAngle`, isolating the reflection-*direction* question
(which face is sampled) from the separate Fresnel-*blend-strength* question (already covered by
the sibling `amount_one`/`amount_zero`/`combined` tests in this batch).

### Cross-file consistency
`CubeMapFace`'s enum order was independently checked against the Vulkan `TextureCube` backend's
`SetData(int face, ...)` (uses the raw enum integer as the array layer index, confirmed at
`VulkanGraphicsBackend.cpp:8386`) and against standard Vulkan/D3D-style cube face-to-major-axis
hardware sampling — no face-remap bug found, so the test's premise that "face N corresponds to
major axis N" is sound.

## Detailed Findings

### F1 — Shares the `env_map3d.vert.glsl` missing-Y-flip defect (see `vulkan_env_map_test.cpp.audit.md` F1 for full analysis)

- Severity: HIGH
- Confidence: HIGH
- Category: correctness (production shader) / test-coverage (structural blind spot)
- Location/symbol: `src/CNA/Internal/Backends/Vulkan/shaders/env_map3d.vert.glsl:35`
- Evidence: identical to the full analysis in `vulkan_env_map_test.cpp.audit.md`'s F1.
- Why it matters for this file specifically: this is the one file in the batch whose entire
  purpose is to probe a genuinely asymmetric camera position (`(5,0,0.5)`, no `Y` component but
  clearly off the `Z`-axis) — exactly the kind of scenario where a rendering-orientation bug
  would normally be most likely to surface. It still does not, because `readCenter()`
  (lines 68-74) samples only the exact center pixel of a viewport-filling quad, and — as
  established in the other reports in this batch — negating clip-space `Y` is an involution that
  fixes the exact center point regardless of how asymmetric the *camera* is, since the quad
  itself is symmetric and fills the whole screen. A genuinely off-center sample point (or a
  non-full-screen/non-viewport-centered quad) would be needed to expose this defect via this kind
  of test.
- FNA/XNA comparison: N/A.
- Related files: see `vulkan_env_map_test.cpp.audit.md` for full cross-shader evidence; also
  affects `vulkan_environmentmapeffect_amount_one_test.cpp`,
  `vulkan_environmentmapeffect_amount_zero_test.cpp`, and
  `vulkan_environmentmapeffect_combined_test.cpp`.
- Suggested future action (not implemented by this audit): add the missing Y-flip to
  `env_map3d.vert.glsl`. Once fixed, this specific file would be a natural place to also add a
  third check with an eye position that has a nonzero `Y` component and an off-center sample
  point, which would make this test family capable of catching a regression of this exact class
  in the future.

## Cross-File Observations

- `git log` corroborates Task 397 as a real, closed test: `5dc7a436 test(Task 397): verify
  EnvironmentMapEffect EyePosition drives reflection vector`.
- This file's `makeDistinctCube()` (assigning a different, easily distinguishable color per face)
  is a more rigorous verification technique than the solid-color cubes used by the `amount_one/
  zero/combined` siblings in this batch — appropriate, since this file's specific job is to prove
  *which* face gets sampled, not just whether the cube contributes at all.

## Missing or Weak Tests

Beyond F1 (which is really a production-code gap, not this test's own design flaw): a third
check varying eye position along `Y` (e.g. eye above the object) would round out the coverage
of all three axes' worth of reflection-direction sensitivity; currently only `X`/`Z` variation is
exercised (`(0,0,3)` vs. `(5,0,0.5)`).

## Positive Findings

- Both expected face selections were independently re-derived from the actual `reflect()`
  formula and the project's real cube-face layer ordering, not merely restated from the test's
  own comments.
- Setting `FresnelFactor=0` to cleanly separate "which face" from "how strongly blended" is good
  test design that keeps this check orthogonal to the sibling `amount_one`/`amount_zero`/
  `combined` tests in this same batch.
- The distinct-color-per-face cube technique is the most rigorous verification of correct
  cube-face/reflection-direction wiring in this batch.

## Final Assessment

A well-designed, independently-verified reflection-direction test; ironically the one file in
this family best positioned by intent (asymmetric camera) to catch the batch-wide Y-flip defect
(F1), yet still structurally blind to it due to center-pixel-only, full-viewport-quad sampling.
