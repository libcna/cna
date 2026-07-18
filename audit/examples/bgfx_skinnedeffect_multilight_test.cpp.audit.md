# Audit: examples/bgfx_skinnedeffect_multilight_test.cpp

## Metadata

- Source file: `examples/bgfx_skinnedeffect_multilight_test.cpp` (197 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `SkinnedEffect` `DirectionalLight1`/`DirectionalLight2`
  forwarding pixel test
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_bgfx_test(cna_test_bgfx_skinnedeffect_multilight …)` /
  `cna_register_backend_test(NAME Bgfx_SkinnedEffect_MultiLight …)`, `cmake/Tests/BgfxTests.cmake:449-453`).
- XNA/FNA relevance: direct — `IEffectLights.DirectionalLight1`/`DirectionalLight2` must genuinely
  contribute their own `Direction`/`DiffuseColor`/`Enabled` state, per FNA's shared
  `Lighting.fxh`'s `ComputeLights(..., numLights)` summing all enabled lights, not just light 0.
- FNA reference: `HLSL/Lighting.fxh`'s `ComputeLights()`, `SkinnedEffect.fx`'s
  `VSSkinnedVertexLightingFourBones`/`ComputeCommonVSOutputWithLighting(vin.Position, vin.Normal, 3)`
  (the `3` = number of lights summed).
- Related production code: `SkinnedEffect.cpp::FillGpuDrawParams()` lines 340-360 (per-light
  direction/diffuse forwarding, Task 893 fix), `fs_skinned3d.sc` lines 32-38
  (`nL0`/`nL1`/`nL2`, `NdL0`/`NdL1`/`NdL2`, `lightSum`).

## Purpose

Task 893's regression test: before this fix, `SkinnedEffect::FillGpuDrawParams()` only ever
forwarded `DirectionalLight0`'s direction/diffuse to the GPU, silently dropping
`DirectionalLight1`/`DirectionalLight2` entirely. Three checks use a normal
(`kNx=0.866,kNy=0,kNz=-0.5`) deliberately chosen so `dot(-lightDir, N) = 0.5` when all 3 lights
share the same direction `(0,0,1)`, and per-light diffuse colors on disjoint RGB channels
(`L0=(0.6,0,0)`, `L1=(0,0.6,0)`, `L2=(0,0,0.6)`) so each light's contribution is independently
readable in a single rendered pixel: (a) all 3 lights enabled, sharing direction → expect
`0.5*(L0+L1+L2)=(0.3,0.3,0.3)`; (b) `DirectionalLight2.Enabled=false` → blue channel drops; (c)
`DirectionalLight1`'s own `Direction` rotated off-axis (dot becomes negative, clamped to 0) → green
channel drops, proving `DirectionalLight1` reads its *own* `Direction` field rather than sharing
`DirectionalLight0`'s.

## Executive Verdict

**Healthy** — all three expected color constants were independently re-derived by this audit from
first principles (not merely cross-checked against the file's own commentary) and match exactly,
including the specific choice of `max(dot,0)` clamping in check (c) that makes an off-axis light
contribute exactly zero rather than a negative value.

## Checklist Results

### API / XNA / FNA parity
`fx.DirectionalLight1.setDirectionProperty(...)`/`setDiffuseColorProperty(...)`/
`setEnabledProperty(...)` and `fx.DirectionalLight2.*` map directly onto FNA's `IEffectLights`
surface; `SkinnedEffect`'s own `IEffectLights.LightingEnabled` is hard-wired `true` (matches FNA's
explicit interface implementation throwing on an attempt to disable it) and is not exercised here,
appropriately, since it is covered by the dedicated defaults test suite
(`tests/Microsoft/Xna/Framework/Graphics/SkinnedEffectTests.cpp`).

### Behavioral correctness
Independently re-derived (not copied from the file's own comments):
- `N = (0.8660254, 0, -0.5)`; `-kLightDir = (0,0,-1)`; `dot(-kLightDir, N) = 0*0.866 + 0*0 +
  (-1)*(-0.5) = 0.5` — confirmed exactly, for all 3 lights when sharing `kLightDir`.
- Check (a): `lightSum = 0.5*(0.6,0,0) + 0.5*(0,0.6,0) + 0.5*(0,0,0.6) = (0.3,0.3,0.3)`. Default
  `DiffuseColor=Vector3::One`, `alpha=1`, so `v_color0 = (1,1,1,1)` (the diffuse-color parameter);
  default `AmbientLightColor`/`EmissiveColor = Zero`; `DirectionalLight` default `SpecularColor =
  Vector3::Zero` (confirmed via `DirectionalLight.cpp`'s constructor, so no specular contaminates
  this "diffuse-only" test even though `SkinnedEffect`'s own material `SpecularColor` defaults to
  `One`/`16` — the per-*light* specular is what actually gates the shader's `specularRGB` term, and
  it is zero here since no test in this file sets it). `gl_FragColor.rgb = tex(white=1,1,1) *
  v_color0.rgb(1,1,1) * lightSum + specular(0) = (0.3,0.3,0.3)`. `*255 = 76.5 → 77` — matches
  `kExpectedAllLights(77,77,77)` exactly.
- Check (b): `DirectionalLight2.Enabled=false` zeroes `ld2` in `FillGpuDrawParams()` (line
  356-357: `light2On ? ... : Vector3::Zero`), so blue channel's `0.5*L2=0.3` term is removed:
  `(0.3,0.3,0)*255=(76.5,76.5,0)→(77,77,0)` — matches `kExpectedLight2Disabled(77,77,0)` exactly.
- Check (c): `kLight1DirOffAxis=(1,0,0)`; `-lightDir1=(-1,0,0)`; `dot((-1,0,0),N) = -0.8660254`
  (negative); the shader clamps via `NdL1 = max(dotL1, 0.0) = 0` (`fs_skinned3d.sc` line 36) — so
  light1's green channel term drops entirely (not merely weakened): `(0.3,0,0.3)*255=(76.5,0,76.5)
  →(77,0,77)` — matches `kExpectedLight1OffAxis(77,0,77)` exactly.

All three re-derivations independently confirm the file's own expected constants are correct for
the current shader formula, not merely internally self-consistent.

### Logic
`renderWith()`'s own 20-iteration retry loop (redraw + read each iteration) is the safe Bgfx
readback pattern used consistently across this shard.

### C++ correctness
`SkinnedGpuVertex` (52 bytes) matches production stride-52 layout. Uses a full-screen quad (`x,y
∈[-1,1]`) rather than the half-screen quad used by the identity/translation/two-bone tests, which
is appropriate here since this test samples only the centre pixel, not multiple discriminating
regions.

### Robustness
The ±8-tolerance `matches()` helper is tighter than the ±10 used by the specular/
preferperpixellighting siblings in this batch — appropriate, since this test's expected values are
all exact multiples of `0.5*0.6*255=76.5` with no floating-point-heavy Blinn-Phong `pow()` term
involved (unlike the specular tests), so less numerical slack is needed.

### Testing
Good use of the "disjoint-RGB-channel-per-light" discrimination trick (also used by
`easygl_environmentmapeffect_multilight_test.cpp`/`easygl_basiceffect_multilight_emissive_test.cpp`
per this file's own comment) to isolate 3 independent hypotheses (light forwarding exists at all,
`Enabled` gating is per-light not global, `Direction` is read per-light not shared) in 3 checks
using a single render call each.

## Detailed Findings

None at MEDIUM/HIGH/CRITICAL severity — all math independently re-verified correct.

## Cross-File Observations

- Uses the *default* 72-identity bone palette (no `SetBoneTransforms()` call), like
  `bgfx_skinnedeffect_identity_bones_test.cpp` and unlike the translation/two-bone/
  specular/preferperpixellighting/vertexcolor siblings in this batch — correctly avoids the
  `EffectParameter::SetValue(std::vector<Matrix>)`-truncation subtlety documented in this batch's
  other reports, since it never touches `bonesParam_` at all after construction.
- Does not test `DirectionalLight0.Enabled=false` (only `DirectionalLight2`) or a genuine
  `DirectionalLight0`-vs-`DirectionalLight1` direction-independence check symmetric to check (c)'s
  `DirectionalLight1` case — a minor asymmetry (see Missing or Weak Tests), though `DirectionalLight0`'s
  own `Enabled`/`Direction` gating is independently covered by
  `bgfx_skinnedeffect_specular_test.cpp`'s check (d) in this same batch, so overall shard coverage
  is complete even though this specific file's own coverage is not perfectly symmetric across all
  3 lights.

## Missing or Weak Tests

- No check exercises `DirectionalLight0.Enabled=false` or an off-axis `DirectionalLight0`/
  `DirectionalLight2.Direction` within *this* file (light 0's gating is covered elsewhere in the
  shard, light 2's `Direction`-independence is never directly isolated by any test in this batch —
  only its `Enabled` gate is). A `DirectionalLight2` off-axis-direction check symmetric to check (c)
  would close this small gap and guard against a hypothetical bug where light 2's direction field
  is silently ignored/aliased to light 0's or light 1's.

## Positive Findings

- All three numeric expectations were independently re-derived by this audit from the FNA
  `Lighting.fxh` formula and the actual current shader source, not merely trusted from the file's
  own comments — all three match exactly.
- The `max(dot,0)` clamping in check (c) is a subtle, easy-to-get-wrong detail (a naive
  implementation might use the unclamped dot product, producing a small *negative* contribution
  rather than exactly zero) and this test's tight ±8 tolerance would actually catch that class of
  regression.

## Final Assessment

A precise, independently-verified, well-designed light-forwarding regression test with only a
minor, non-blocking coverage asymmetry (light 0/2 direction-independence not each explicitly
isolated within this specific file).
