# Audit: examples/bgfx_dualtextureeffect_fog_test.cpp

## Metadata

- Source file: `examples/bgfx_dualtextureeffect_fog_test.cpp` (169 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `DualTextureEffect` linear-fog pixel integration test.
- CTest registration: `cna_bgfx_test(cna_test_bgfx_dualtextureeffect_fog …)` /
  `cna_register_backend_test(NAME Bgfx_DualTextureEffect_Fog …)` (`cmake/Tests/BgfxTests.cmake:353-355`).
- XNA/FNA relevance: direct — `DualTextureEffect.FogEnabled`/`FogColor`/`FogStart`/`FogEnd`.
- FNA reference: `HLSL/DualTextureEffect.fx` (`PSDualTexture`'s `ApplyFog(color, pin.Specular.w)`,
  via `Common.fxh`'s standard fog-vector blend).
- Related production code: `src/CNA/Internal/Backends/Bgfx/shaders/vs_dual_texture3d.sc` (fog
  factor computed from raw object-space Z), `fs_dual_texture3d.sc` (`mix(u_fogColor.xyz,
  color.rgb, v_fogFactor)`), `src/Microsoft/Xna/Framework/Graphics/DualTextureEffect.cpp`
  (`FillGpuDrawParams()`).

## Purpose

A 3-point Z-sweep (z=-0.9 no fog, z=0.9 full fog, z=0 half fog) proving the Bgfx `DualTextureEffect`
fog blend is a genuine linear interpolation rather than an on/off switch. `Texture2`=gray(128)
deliberately cancels Task 383's `color.rgb *= 2` doubling factor so the pre-fog material color
reduces to approximately `DiffuseColor` directly, isolating fog as the test's sole variable. All
of `World`/`View`/`Projection` are Identity, so raw vertex Z is used directly as clip-space Z
(documented as valid specifically for Bgfx's OpenGL-convention `[-1,1]` Z range, unlike Vulkan's
`[0,1]`).

## Executive Verdict

**Healthy** — all three expected constants were independently re-derived by this audit from the
exact current shader formula (including the non-exact `2×128/255=1.00392` cancellation the file's
own comment calls out) and match to within half a unit, i.e. exact given 8-bit rounding.

## Checklist Results

### Behavioral correctness
Independently recomputed all three expected colors from the current
`fs_dual_texture3d.sc`/`vs_dual_texture3d.sc` formulas, not merely re-stating the file's own claims:
- Material color before fog: `base.rgb = white(1)*2 = 2`; `color = 2 * gray(128/255=0.501961) *
  kDiffuse(0.8,0.2,0.4) = 1.003922 * (0.8,0.2,0.4) = (0.803138,0.200784,0.401569)`, i.e.
  `(204.8, 51.2, 102.4) → (205, 51, 102)` after 8-bit rounding — matches `kExpectedNoFog(205,51,102)`
  (line 55) exactly, including the file's own explicit note that the ideal "$204$" is wrong because
  the `*2`/gray cancellation is not exact.
- Full fog (z=kFogEnd=0.9): `v_fogFactor = clamp((0.9-0.9)/(0.9-(-0.9)),0,1) = 0`, so
  `mix(fogColor, materialColor, 0) = fogColor = (0.1,0.6,0.9) → (25.5,153,229.5) → (26,153,230)`
  — matches `kExpectedFullFog(26,153,230)` (line 56) exactly.
- Half fog (z=0): `v_fogFactor = clamp(0.9/1.8,0,1) = 0.5`; `mix(fogColor, materialColor, 0.5) =
  ((0.1+0.803138)/2, (0.6+0.200784)/2, (0.9+0.401569)/2) = (0.451569,0.400392,0.650785) →
  (115.15,102.1,165.95) → (115,102,166)` — matches `kExpectedHalfFog(115,102,166)` (line 57)
  exactly. All three values are exact re-derivations from the live shader, not merely restatements
  of the file's own comment.

### Logic
The Z-range caveat (Bgfx's GL-style `[-1,1]` clip Z vs. Vulkan's `[0,1]`, hence Vulkan's own
`DualTextureEffect` fog test is deferred per the comment) is a real, checkable constraint: since
both `World`/`View`/`Projection` here are Identity, `gl_Position.z` is literally the raw vertex Z
passed in (`vs_dual_texture3d.sc:13`: `gl_Position = mul(u_wvp, vec4(a_position,1.0))`), and the
fog-factor formula (`v_fogFactor = clamp((FogEnd - a_position.z)/(FogEnd-FogStart),...)`,
`vs_dual_texture3d.sc:20-22`) is computed directly from object-space Z, not from any actual clip/
NDC Z — so this test's fog-factor derivation is actually independent of the backend's clip-Z
convention entirely (it reads `a_position.z` before any projection is applied). The comment's
framing ("raw vertex Z is `gl_Position.z` directly ... Bgfx's OpenGL backend uses the same [-1,1]
clip-space Z range as EasyGL") is slightly imprecise — the fog factor never actually depends on
`gl_Position.z` at all, only on `a_position.z` — but this does not affect the correctness of this
test's own math, since it derives its expected values from `a_position.z` too.

### Robustness
Same `RasterizerState::CullNone` requirement and read-until-nonblack retry loop as the sibling
doubling/null-texture tests in this shard, applied consistently.

### Testing
Three assertions, each isolating a different point on the fog blend curve; the half-fog check
(z=0) is specifically noted as proving genuine interpolation rather than a step function, which
this audit confirms is a real discriminator (a broken on/off fog implementation would produce
either the no-fog or full-fog color at z=0, not the computed 50/50 blend).

## Detailed Findings

None. All three expected pixel values were independently recomputed against the live shader
source and match exactly (to 8-bit rounding).

## Cross-File Observations

- Git history (`git log --follow`) shows this file (`Task 888`) postdates
  `bgfx_dualtextureeffect_doubling_test.cpp` (`Task 383`), and its own header comment correctly
  attributes the doubling-factor interaction to that earlier task rather than re-deriving it from
  scratch.
- The minor imprecision noted above (fog factor depends on `a_position.z`, not
  `gl_Position.z`) is a documentation nuance in this file's own comment, not a functional
  discrepancy — flagged as INFO rather than a finding since it does not affect any assertion's
  correctness.

## Missing or Weak Tests

None specific to this file — the Z-range caveat correctly scopes this test to Bgfx only and
explains (rather than hides) why Vulkan's equivalent is deferred (Task 899, referenced by name in
this project's task history for `EnvironmentMapEffect`'s analogous fog test).

## Positive Findings

- The deliberate `Texture2=gray(128)` cancellation trick to isolate fog from the doubling factor
  is a genuinely clever test-design choice, and this audit's independent re-derivation confirms it
  produces the intended near-cancellation (`1.00392`, not exactly `1.0`) — the file's own comment
  correctly flags the resulting `205` (not the naively-expected `204`) rather than silently getting
  it wrong.
- The half-fog check is a real interpolation-vs-switch discriminator, not just a third data point.

## Final Assessment

A rigorous, mathematically verified fog-blend test; all three expected constants check out exactly
against the current production shader, and the test's own documentation of its Z-range/backend
scoping is accurate (net of one minor, inconsequential wording imprecision).
