# Audit: examples/bgfx_environmentmapeffect_multilight_test.cpp

## Metadata

- Source file: `examples/bgfx_environmentmapeffect_multilight_test.cpp` (200 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `EnvironmentMapEffect.DirectionalLight1`/
  `DirectionalLight2` forwarding, Bgfx backend, Task 890.
- CTest registration: `cna_bgfx_test(cna_test_bgfx_environmentmapeffect_multilight …)` /
  `cna_register_backend_test(NAME Bgfx_EnvironmentMapEffect_MultiLight …)`
  (`cmake/Tests/BgfxTests.cmake:187-189`).
- XNA/FNA relevance: direct — `IEffectLights.DirectionalLight0/1/2`.
- FNA reference: `HLSL/Lighting.fxh`'s `ComputeLights(eyeVector, worldNormal, numLights=3)`: sums
  all three lights' `step(0,dot(-L,N)) * dot(-L,N) * lightDiffuse`, i.e. a per-light Lambertian
  term clamped at zero, matching `DirectionalLight.Enabled` zeroing a disabled light's GPU-facing
  diffuse (`DirectionalLight.cs`).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.cpp`
  (`FillGpuDrawParams()` lines 439-449, forwarding `DirectionalLight1`/`DirectionalLight2` — the
  Task 890 fix itself), `src/CNA/Internal/Backends/Bgfx/shaders/fs_env_map3d.sc` (`NdotL0/1/2 =
  max(dot(N,-normalize(lightDir)),0.0)`, lines 24-27).

## Purpose

Three-check pixel test proving `EnvironmentMapEffect` forwards all three directional lights'
diffuse contribution to the Bgfx shader, not just `DirectionalLight0` — the real gap Task 890 fixed
(per the header comment, mirroring `easygl_environmentmapeffect_multilight_test.cpp`'s established
derivation). `EnvironmentMapAmount=0`/`EnvironmentMapSpecular=(0,0,0)` (lines 114-115) isolate pure
per-light-diffuse lighting from the cube-map contribution. The chosen normal `kNormal=
(0.8660254, 0, -0.5)` gives `dot(-lightDir,N)=0.5` for all three lights sharing `kLightDir=(0,0,1)`,
and each light is assigned a distinct diffuse channel (`kLight0Diffuse=(0.6,0,0)`,
`kLight1Diffuse=(0,0.6,0)`, `kLight2Diffuse=(0,0,0.6)`) so each light's contribution is
independently observable in a separate RGB channel of the single readback pixel.

## Executive Verdict

**Healthy** — all three checks (all-lights-on, `DirectionalLight2.Enabled=false`, and
`DirectionalLight1` rotated off-axis) were independently re-derived and match the expected
constants exactly.

## Checklist Results

### Behavioral correctness — full independent re-derivation
`kNormal=(0.8660254,0,-0.5)` is a genuine unit vector (`0.866²+0.5²=0.75+0.25=1.0`). With
`kLightDir=(0,0,1)`: `dot(-kLightDir,N)=dot((0,0,-1),(0.866,0,-0.5))=0.5`, matching the file's own
comment (line 42) exactly. `texColor=white=(1,1,1,1)` (line 161-162), `DiffuseColor` left at its
default `(1,1,1,1)`, `EmissiveColor` left at its default `Vector3::Zero` (never set in this file).
- Check 1 (`allLightsGot`, light1Dir=`kLightDir` shared, light2Enabled=true): `lightSum =
  0.5*(kLight0Diffuse+kLight1Diffuse+kLight2Diffuse) = 0.5*(0.6,0.6,0.6) = (0.3,0.3,0.3)`.
  `litRGB=(0+lightSum)*(1,1,1)=(0.3,0.3,0.3)`. `baseColor=litRGB*texColor=(0.3,0.3,0.3)` →
  `blendFactor=0` (`EnvironmentMapAmount=0`) → `rgb=baseColor` → `(76.5,76.5,76.5)≈(77,77,77)`.
  Matches `kExpectedAllLights(77,77,77,255)` (line 46, 165-168) — confirms `EnvironmentMapEffect`
  genuinely sums all 3 lights' diffuse (`FillGpuDrawParams()` lines 439-449), not just light0.
- Check 2 (`light2OffGot`, `light2Enabled=false`): `DirectionalLight2.getEnabledProperty()==false`
  →`FillGpuDrawParams()` forces `ld2=Vector3::Zero` (`.cpp:445-446`) regardless of
  `DiffuseColor` still being set to `kLight2Diffuse` on the C++-side effect object — the blue
  channel's `0.5*0.6=0.3` term drops entirely. `lightSum=(0.3,0.3,0)` → `(77,77,0)`. Matches
  `kExpectedLight2Disabled(77,77,0,255)` (line 48, 170-173) exactly — confirms the `Enabled` gate
  applies per-light, matching FNA's `DirectionalLight.Enabled` C# setter semantics.
- Check 3 (`light1OffAxisGot`, `light1Dir=kLight1DirOffAxis=(1,0,0)`): `NdotL1 =
  max(dot(N,-(1,0,0)),0) = max(dot((0.866,0,-0.5),(-1,0,0)),0) = max(-0.866,0) = 0` — the
  `max(...,0)` clamp (matching FNA's `Lighting.fxh` `step(0,dotL)` zero-clamp exactly, verified
  against the actual FNA source) zeroes green's contribution regardless of `kLight1Diffuse`'s
  magnitude. `lightSum=0.5*(kLight0Diffuse+kLight2Diffuse)=(0.3,0,0.3)` → `(77,0,77)`. Matches
  `kExpectedLight1OffAxis(77,0,77,255)` (line 50, 175-178) exactly — confirms `DirectionalLight1`
  genuinely uses its *own* independent `Direction` field, not a shared/aliased one.

All three checks are exact algebraic derivations and are strong, orthogonal discriminators: check 1
proves multi-light summation exists at all; check 2 proves the `Enabled` gate is wired per-light;
check 3 proves `Direction` is wired per-light (not just diffuse color) — three genuinely distinct
failure modes a naive re-implementation could trip on independently, each isolated to its own RGB
channel in a single readback.

### Robustness
Comment lines 90-94 note a real, documented pre-existing gap worth independently verifying: "Bgfx's
env-map path has no null-EnvironmentMap fallback (unlike EasyGL/Vulkan, which both bind a default
white cube); leaving it unset crashes with a `GL_INVALID_OPERATION`." This test works around it by
binding a real (irrelevant, since `EnvironmentMapAmount=0`) cube (line 163,
`makeCube(dev, Color(0,255,0,255))`) rather than leaving `EnvironmentMap` unset — a defensible,
explicitly-justified test-authoring choice that avoids conflating this task's own regression target
with a separate, already-acknowledged gap. This audit did not independently re-verify the crash
claim (would require deliberately triggering it, out of scope for a static/read-only audit), but the
claim is plausible and internally consistent with the shader's unconditional `SAMPLERCUBE(s_envMap,
1)` + `textureCube(s_envMap, reflDir)` sample with no guard (`fs_env_map3d.sc:6,31`).

## Detailed Findings

### F1 — Same `DiffuseColor`/`EmissiveColor` recombination defect as this batch's other `EnvironmentMapEffect` files; masked here identically (and doubly so, since `EmissiveColor` is also left at zero)

- Severity: HIGH
- Confidence: HIGH
- Category: fna-parity (production code exercised by, but not exposed by, this test)
- Location/symbol: `src/CNA/Internal/Backends/Bgfx/shaders/fs_env_map3d.sc:28`; same defect
  reproduced in `src/CNA/Internal/Backends/Vulkan/shaders/env_map3d.frag.glsl:39`. Full derivation
  in this batch's `bgfx_environmentmapeffect_eyeposition_test.cpp.audit.md` (F1).
- This file's own coverage: `EmissiveColor` is never set (stays `Vector3::Zero` default) and
  `DiffuseColor` is never set (stays `(1,1,1,1)` default), so both the buggy `(emissive+lightSum)*
  diffuseColor` and the correct `lightSum*diffuseColor+emissive` formulas reduce to identically
  `lightSum` in this scene — doubly masked.
- Suggested future action (not implemented by this audit): see the `eyeposition` report's F1 for the
  concrete shader fix.

## Cross-File Observations

- Per Task 896 (comment line 146), the standard quad winding needs `RasterizerState::CullNone` —
  consistent with the `Task 364/884` finding independently re-verified in this batch's
  `eyeposition`/`fresnel` reports against `RasterizerState.cpp:11`/
  `BgfxGraphicsBackend.cpp:1781-1782`. One structural difference from the other five files in this
  batch: this file calls `dev.setRasterizerStateProperty(RasterizerState::CullNone)` *after*
  `fx.Apply()` (line 145-147) rather than before it (as in `eyeposition`/`fresnel`/`specular`/
  `worldtransform`/`fog`); since `RasterizerState` and `Effect.Apply()` set independent GPU state
  (rasterizer fixed-function state vs. shader program + uniforms) with no XNA-defined ordering
  dependency between them within the same draw call, this reordering is harmless — both states are
  in effect by the time `DrawUserPrimitives` executes (line 148).
- Shares the shared-`EnvironmentMapEffect`/`fs_env_map3d.sc` production path with the other five
  files in this batch (F1 above).

## Missing or Weak Tests

- See F1.

## Positive Findings

- All three checks are exact algebraic derivations, each isolating a genuinely distinct multi-light
  failure mode (summation, `Enabled` gating, `Direction` independence) into its own RGB channel of a
  single pixel read — an efficient, well-designed 3-in-1 test.
- The `EnvironmentMap`-null-fallback-gap workaround (lines 90-94) is explicitly justified in-code
  rather than left as an unexplained "just because" fixture choice, keeping this task's own
  regression target cleanly isolated from that separate, acknowledged gap.

## Final Assessment

A precise, well-designed multi-light test whose three checks are all exact re-derivations of the
current production formula and correctly isolate three independent failure modes. This audit's
review of the shared shader surfaced the same untracked `DiffuseColor`/`EmissiveColor`
recombination bug (F1) documented across this batch, doubly masked here by this file's
all-default-emissive/diffuse scene.
