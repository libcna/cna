# Audit: examples/vulkan_environmentmapeffect_amount_one_test.cpp

## Metadata

- Source file: `examples/vulkan_environmentmapeffect_amount_one_test.cpp` (169 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-vulkan` shard — `EnvironmentMapEffect.EnvironmentMapAmount=1`
  full-replace verification, Vulkan backend (Task 394)
- File type: standalone `Game`-subclass executable
  (`VulkanEnvironmentMapAmountOneTest`), CTest-registered
- XNA/FNA relevance: direct — `EnvironmentMapEffect.EnvironmentMapAmount`
- FNA reference: `HLSL/EnvironmentMapEffect.fx` (`PSEnvMap`: `color.rgb = lerp(color.rgb,
  envmap.rgb, pin.Specular.rgb);` where `pin.Specular.rgb` carries `EnvironmentMapAmount` — a
  `lerp`/interpolate, i.e. at `Amount=1` the env map *fully replaces* the lit base color, not
  "adds on top of it")
- Related production code: `src/Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.cpp`
  (`FillGpuDrawParams()` lines 398-467), `src/CNA/Internal/Backends/Vulkan/shaders/
  env_map3d.frag.glsl` (line 52: `vec3 rgb = mix(baseColor, envSample.rgb*combinedAlpha,
  blendFactor) + ...`)

## Purpose

Two-check test proving the real bug this task found and fixed (per the file's own header
comment): CNA's env-map fragment shaders across all 3 backends originally computed
`rgb = litRGB*texColor.rgb + envColor*amount + specular` (additive), when FNA's real formula is
`lerp(baseColor, envColor, amount)` (interpolate). At `EnvironmentMapAmount=1` these two formulas
diverge sharply: additive would produce `baseColor + envColor` (potentially blown out/clamped),
while lerp produces exactly `envColor`, fully discarding `baseColor`. Check (a) uses a white
cube (non-discriminating between the two formulas by itself, since both would clamp to white);
check (b) uses a **gray** cube specifically because it *is* discriminating — additive
(`baseColor + gray`) and lerp (`= gray` exactly) produce visibly different results.

## Executive Verdict

**Needs attention** — the test's own math is correct and precisely proves the lerp-vs-additive
distinction it claims to. However, both checks render via `env_map3d.vert.glsl`, which is
missing the Vulkan Y-flip every other core Vulkan 3D vertex shader applies (F1, shared with 4
sibling files in this batch) — a real production defect this test's center-pixel-only sampling
cannot detect.

## Checklist Results

### API / XNA / FNA parity
`setEnvironmentMapAmountProperty(1.0f)` (line 100, the real FNA default, set explicitly here for
clarity) and `setEnvironmentMapSpecularProperty(Vector3(0,0,0))` (disabling the specular add-on
to isolate the lerp term alone) are both correct uses of `EnvironmentMapEffect`'s FNA-facing API.

### Behavioral correctness
Re-derived against the live `env_map3d.frag.glsl`. With `View=Identity` and the quad at
object-space `z=0` (same plane as the eye, since `EyePosition` derives from `Invert(View)`'s
translation, which is `(0,0,0)` under `View=Identity`), the eye vector `eyeDir = eyePos -
worldPos` always has `eyeDir.z = 0` exactly (both terms are `0`), so its normalized form `E`
always lies in the XY plane. Since `N=(0,0,1)`, `viewAngle = dot(E,N) = E.z = 0` for any nonzero
`E` (and the pixel actually sampled — the render target's center texel — is never exactly at the
degenerate `x=y=0` point, due to the standard half-texel pixel-center sampling offset, so `E` is
always well-defined here, not a `normalize(0,0,0)` NaN). With `viewAngle=0`: `blendFactor =
pow(max(1-0,0), FresnelFactor)*EnvironmentMapAmount = 1*Amount = Amount = 1` for both checks,
regardless of whether Fresnel is enabled (it is, by the effect's own real FNA default
`FresnelFactor=1`, never overridden in this test).
- (a) white cube: `mix(baseColor, white, 1) = white = (255,255,255)`. Matches
  `Color(255,255,255,255)` (line 140) — this check is explicitly labeled "not discriminating" by
  the test's own comment (line 141), correctly, since additive would also clamp to white here.
- (b) gray cube (`128,128,128`): `mix(baseColor, gray, 1) = gray = (128,128,128)` exactly —
  matches `Color(128,128,128,255)` (line 147). This is the discriminating check: the old additive
  formula would have produced `baseColor(≈(100,50,25) from tex=(200,100,50)*emissive0.5) + gray
  (128,128,128)` ≈ `(228,178,153)` clamped, visibly different from the correct `(128,128,128)`.

### Logic
Both checks correctly hold `EmissiveColor=(0.5,0.5,0.5)` and `EnvironmentMapSpecular=(0,0,0)`
constant, varying only the cube color between the two `renderWith()` calls — a clean, minimal
difference isolating exactly the variable under test (the lerp target).

### Testing
Genuinely discriminates the historical bug: verified by hand that the pre-fix additive formula
would fail check (b) numerically (`≈(228,178,153)` vs. expected `(128,128,128)`, a difference far
outside the `±20` tolerance), while the fixed lerp formula passes both. This is one of the
stronger, most surgically-targeted tests in this batch.

## Detailed Findings

### F1 — Shares the `env_map3d.vert.glsl` missing-Y-flip defect (see `vulkan_env_map_test.cpp.audit.md` F1 for full analysis)

- Severity: HIGH
- Confidence: HIGH
- Category: correctness (production shader) / test-coverage (structural blind spot)
- Location/symbol: `src/CNA/Internal/Backends/Vulkan/shaders/env_map3d.vert.glsl:35`
- Evidence: identical to the full analysis filed in `vulkan_env_map_test.cpp.audit.md`'s F1 —
  every other core Vulkan 3D vertex shader applies `pos.y = -pos.y;` (Vulkan NDC vs. OpenGL);
  `env_map3d.vert.glsl` does not, and no compensating mechanism (CPU-side matrix flip,
  negative-height viewport) was found.
- Why it matters for this file specifically: both `renderWith()` calls in this test use a
  full-viewport quad with `World=View=Projection=Identity` and read only the exact center pixel.
  A vertical mirror of the render leaves the center pixel unchanged (Y-negation fixes the exact
  center), so this test — despite being one of the most numerically rigorous in the batch —
  cannot detect the missing flip.
- FNA/XNA comparison: N/A (Vulkan clip-space convention bug).
- Related files: see `vulkan_env_map_test.cpp.audit.md` for the full cross-shader evidence table;
  also affects `vulkan_environmentmapeffect_amount_zero_test.cpp`,
  `vulkan_environmentmapeffect_combined_test.cpp`, and
  `vulkan_environmentmapeffect_eyeposition_test.cpp`.
- Suggested future action (not implemented by this audit): add the missing Y-flip to
  `env_map3d.vert.glsl`.

## Cross-File Observations

- `git log` corroborates Task 394 as a real, closed fix: `a5237f55 fix(Task 394):
  EnvironmentMapEffect cube-map blend was additive, not lerp`.
- Shares its `makeSolidCube()`/`readCenter()` helper structure with the other 3
  `vulkan_environmentmapeffect_*` files in this batch — reviewed together for the shared
  eye/normal-geometry derivation, but each earns its own per-file report.

## Missing or Weak Tests

Check (a) is explicitly non-discriminating by the test's own admission — acceptable since it's a
baseline sanity check paired with the genuinely discriminating check (b), not standing alone.

## Positive Findings

- Check (b)'s gray-cube choice is a well-reasoned, minimal way to discriminate lerp from additive
  blending — verified by hand that it would actually have failed under the pre-fix formula.
- The test's own comment (line 141) correctly self-identifies check (a) as non-discriminating
  rather than overclaiming its coverage.

## Final Assessment

A precisely-targeted, correctly-derived regression test for the Task 394 lerp fix; its only
weakness is one it shares structurally with the rest of this Vulkan `EnvironmentMapEffect` test
family — center-pixel-only sampling that cannot see the separately-discovered Y-flip defect (F1).
