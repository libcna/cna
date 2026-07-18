# Audit: examples/webgpu_littextured3d_test.cpp

## Metadata

- Source file: `examples/webgpu_littextured3d_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-webgpu` shard — `BasicEffect` Blinn-Phong lit-textured (stride-32,
  `VertexPositionNormalTexture`) pixel test, WebGPU backend (experimental, per `CLAUDE.md`).
- Test executable: `cna_test_webgpu_littextured3d`, CTest target `WebGPU_LitTextured3D`
  (`cmake/Tests/WebGpuTests.cmake:63-64`).
- XNA/FNA relevance: direct — `BasicEffect.LightingEnabled`/`AmbientLightColor`/`DirectionalLight0`.
- FNA reference: `HLSL/Lighting.fxh` (`ComputeLights()`: `result.Diffuse = mul(diffuse, lightDiffuse) *
  DiffuseColor.rgb + EmissiveColor;`, ambient folded into the directional-light sum before the
  `DiffuseColor` multiply — matching `EffectHelpers.SetMaterialColor()`'s "lighting enabled" branch,
  where ambient is **not** pre-folded into emissive, unlike `EnvironmentMapEffect`'s own convention).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/BasicEffect.cpp`
  (`FillGpuDrawParams()` lines 60-119, notably the comment at lines 66-69 distinguishing the
  unlit-path/lit-path emissive handling), `src/CNA/Internal/Backends/WebGPU/WebGPUGraphicsBackend.cpp`
  (`CreateLitTexturedResources()` lines 2875-3111, per-pixel shader lines 2888-2973, per-vertex-lit
  sibling lines 2992-…, `FillLitLightUniforms()`/`ComputeNormalMatrix3x3()` lines 423-467, dispatch in
  `DrawPrimitivesEx()` lines 6056-6061).

## Purpose

Four-check pixel test proving `lit_textured3d.wgsl`'s real Blinn-Phong lighting (FNA's `Lighting.fxh`
`ComputeLights()`, driven by a second UBO alongside the primary 128-byte uniform block) genuinely runs
for stride-32 draws: (A) `LightingEnabled=false` renders plain `texture*diffuse` (green), proving both
the stride-32 dispatch and the shader's unlit fallback branch; (B) a directional light facing the
visible side of a camera-facing quad fully illuminates it (white); (C) the same light redirected to hit
the back of the quad contributes no diffuse (`N·L ≤ 0` gate, black); (D) ambient light alone (no
directional light) still reaches the shader (white).

## Executive Verdict

**Healthy.** Independently re-derived all four checks' expected pixel values by hand against the
current production formula (`BasicEffect::FillGpuDrawParams()` and the WGSL shader's per-fragment/
per-vertex Blinn-Phong math) and found every one to match exactly — this is a genuine, current-formula
proof, not a stale-but-tolerant assertion (contrast with the known `easygl_basiceffect_specular_test.cpp`
F1 pattern elsewhere in this codebase).

## Checklist Results

### API / XNA / FNA parity

`setTextureEnabledProperty`/`setLightingEnabledProperty`/`setAmbientLightColorProperty`/
`DirectionalLight0.setEnabledProperty`/`setDirectionProperty`/`setDiffuseColorProperty` (lines 129-131,
145-151, 165-171, 185-189) all map directly to the FNA `BasicEffect`/`IEffectLights` surface.

### Behavioral correctness

Re-derived the shader math against `CreateLitTexturedResources()`'s per-pixel-lit `fs_main` (lines
2938-2972) — since `PreferPerPixelLighting` is left at its class default (`false`) and
`command.preferVertexLit = params.lightingEnabled && !params.preferPerPixelLighting` (line 6522)
therefore selects the per-vertex-lit sibling shader instead, this audit also confirmed the per-vertex
variant (lines 2992-…) computes textually identical Blinn-Phong math, just relocated from `fs_main` into
`vs_main` and interpolated as varyings — a distinction that happens to be immaterial for this test's own
scene (a single quad with one uniform `Normal=(0,0,-1)` across both triangles means per-vertex and
per-pixel evaluation produce bit-identical results at every point, since there is no normal variation to
interpolate). Both variants share the identical formula:
```
lightSum = ambientLighting.xyz + ndotl0*light0Diffuse + ndotl1*light1Diffuse + ndotl2*light2Diffuse
lit = lightSum * diffuseColor.rgb + emissiveColor.xyz
```
which is a correct, direct translation of FNA's `Lighting.fxh::ComputeLights()`:
`result.Diffuse = mul(diffuse, lightDiffuse) * DiffuseColor.rgb + EmissiveColor;` (ambient participates
in the pre-`DiffuseColor`-multiply sum, exactly as this shader does — **not** folded into the emissive
term the way `EnvironmentMapEffect`'s own CPU-side code does; this is the correct, effect-specific
convention for `BasicEffect`, confirmed against `BasicEffect::FillGpuDrawParams()`'s own comment at
lines 109-111: "Lit path only: `EmissiveColor` is added after the ambient/light sum is multiplied by
`DiffuseColor`").
- Check A: `LightingEnabled=false` → `fs_main`'s early-return branch (`lightingEnabled <= 0.5`) returns
  `u.diffuseColor * sampled` directly — default `DiffuseColor` (white) × green texture = green. Matches
  `Color::Lime`.
- Check B: `N=(0,0,-1)`, `light0Dir=(0,0,1)` → `dot(N,-light0Dir) = dot((0,0,-1),(0,0,-1)) = 1` →
  `ndotl0=1`; `ambient=0`; `lightSum = 1*(1,1,1) = (1,1,1)`; `lit = (1,1,1)*white(diffuse) + 0 =
  (1,1,1)`; final colour `= lit * white(texture) = white`. Matches `Color::White`.
- Check C: `light0Dir=(0,0,-1)` → `dot(N,-light0Dir) = dot((0,0,-1),(0,0,1)) = -1` → `zerol0 =
  step(0,-1) = 0` → `ndotl0 = max(-1,0) = 0` (the WGSL guard correctly zeroes the contribution via
  `zerol0`/`max`, matching FNA's `zeroL = step(0, dotL)` gate) → `lightSum=0` → black. Matches
  `Color::Black`.
- Check D: `AmbientLightColor=(1,1,1)`, `DirectionalLight0.Enabled=false` → `light0Diffuse` forwarded as
  `Vector3::Zero` by `BasicEffect::FillGpuDrawParams()`'s own `light0On` gate (matching FNA's
  `DirectionalLight.Enabled` semantics) → `lightSum = ambient(1,1,1) + 0 = (1,1,1)` → white. Matches
  `Color::White`.
All four independently re-derive to the test's own asserted expected colours with no rounding
ambiguity (every check lands on an exact `0`/`255` extreme, deliberately avoiding the WebGPU sRGB
swapchain gamma-encoding caveat the header comment (lines 25-26) correctly flags for mid-tone values).

### Logic

`DrawPrimitivesEx()`'s dispatch precedence (lines 6019-6061) correctly routes a stride-32,
`texture0 != nullptr`, non-alpha-test/non-dual-texture/non-env-map/non-skinned draw to
`QueueLitTexturedDraw()` (line 6059) — independently confirmed by reading the full precedence chain
(alpha-test wins over dual-texture/env-map/skinned/lit-textured; dual-texture wins over
env-map/skinned/lit-textured; env-map wins over lit-textured for the same stride-32 shape), none of
which this test's scene triggers (no alpha test, no `Texture2`, no `EnvironmentMap`, no skinning),
correctly leaving the plain lit-textured path as the one actually exercised.

### C++ correctness

No concerns specific to this file; standard `colorNear()`/one-shot-`Draw()` idiom shared across the
shard, with a wider-than-usual tolerance (24, vs. 16 elsewhere in the shard) appropriately justified by
the header comment's own note about WebGPU's sRGB swapchain format — though since every check here
lands on a pure `0`/`255` extreme (never a mid-tone), this tolerance headroom is not actually load-bearing
for any of the four assertions (an extreme value is gamma-invariant either way).

### Robustness

Checks B/C are a correct differential design isolating "does a light facing the surface illuminate it"
from "does the `N·L≤0` gate correctly exclude a light behind the surface" — two independently-wrong
hypotheses (always-lit, never-lit) a broken `ndotl`/`zerol` computation could produce, both covered.

### Testing

Good coverage of the unlit/lit branches, single-directional-light illumination and its N·L gate, and
ambient-only illumination for `BasicEffect`'s stride-32 lit-textured path specifically. Not covered by
this file (no claim otherwise, consistent with the header's own scope statement): fog (this shader has
none per the header comment, "deliberately deferred like the other WebGPU 3D shaders"), specular
highlights (`SpecularColor`/`SpecularPower` are packed into `FillLitLightUniforms()`'s
`specularColorPower` field and consumed by the shader's `specularRgb` computation, lines 2963-2967, but
never independently exercised — every check here uses the class default `SpecularColor=(0,0,0)` per
`BasicEffect`'s own defaults, so `specularRgb` is structurally zero throughout this file), multiple
simultaneous lights (`DirectionalLight1`/`DirectionalLight2` are always left disabled), and whether the
per-pixel-lit vs. per-vertex-lit shader selection (`PreferPerPixelLighting`) is itself correctly wired —
this file's uniform-normal scene cannot distinguish the two variants' outputs even if the selection
logic were broken.

### Architecture / Memory / Performance / Thread safety / Portability

No file-specific concerns. Follows the same one-shot `Game`/`Draw()`-guard/`Exit()` idiom as every other
file in this shard.

## Detailed Findings

None at HIGH or above.

## Cross-File Observations

- Confirms `BasicEffect`'s lit-textured formula (`lightSum*DiffuseColor + EmissiveColor`, ambient
  folded into `lightSum` rather than pre-combined with emissive) is correctly implemented on this
  backend, in useful contrast with this same shard's `webgpu_envmap3d_test.cpp` audit, which found the
  sibling `EnvironmentMapEffect` shader **incorrectly** re-multiplies its own (differently-composed,
  ambient-pre-folded) emissive term by `DiffuseColor` a second time. Reading both shaders side by side
  confirms the bug is specific to `env_map3d.wgsl`'s own authoring, not a shared uniform-packing mistake
  or a systemic misunderstanding of FNA's lighting convention across this backend's shader family.
- Per this audit's cross-cutting mandate: this file uses `BasicEffect` with no skinning, so the
  confirmed `CreateSkinnedResources()` world-space-normal-transform bug does not apply here. The
  normal-matrix computation this shader *does* use (`ComputeNormalMatrix3x3()`, lines 431-441 — a
  cofactor/determinant CPU-side inverse of the dumped world matrix, standing in for
  `WorldInverseTranspose`) is structurally the *correct* pattern the skinned path is missing, and this
  audit independently confirms `QueueLitTexturedDraw`'s use of it (via `FillLitLightUniforms()`) is
  wired correctly for this non-skinned path.

## Missing or Weak Tests

- No specular-highlight coverage for this specific stride-32 lit-textured path (covered separately by
  dedicated specular test files elsewhere in the WebGPU shard, not independently confirmed by this
  audit).
- No multi-light (`DirectionalLight1`/`DirectionalLight2` simultaneously enabled) coverage.
- No scene capable of distinguishing per-pixel-lit from per-vertex-lit shader selection (a uniform
  normal across the whole quad makes the two paths numerically identical for this test).

## Positive Findings

- All four checks were independently re-derived by hand against the current production formula and
  found exactly correct, including the non-obvious `N·L≤0` gate's `step()`-based zeroing (Check C) and
  the `Enabled=false` semantics correctly zeroing a light's GPU-facing diffuse contribution (Check D).
- Correctly avoids a mid-tone expected value (which would be vulnerable to the WebGPU sRGB-swapchain
  gamma-encoding caveat this file's own header honestly discloses), instead using pure `0`/`255`
  extremes throughout — a good, deliberate test-design choice.

## Final Assessment

A correct, rigorously-verified test with no defects found in either its own logic or the production code
paths (`BasicEffect::FillGpuDrawParams()`, `CreateLitTexturedResources()`'s two shader variants, and the
`DrawPrimitivesEx()` dispatch) it exercises. Provides a valuable point of contrast confirming the
`EnvironmentMapEffect` emissive/diffuse defect found via `webgpu_envmap3d_test.cpp`'s audit is isolated
to that shader specifically, not a systemic lighting-formula mistake across this backend.
