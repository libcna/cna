# Audit: examples/bgfx_environmentmapeffect_combined_test.cpp

## Metadata

- Source file: `examples/bgfx_environmentmapeffect_combined_test.cpp` (166 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `EnvironmentMapEffect` cross-backend capstone test, Bgfx
  backend, Task 399.
- CTest registration: `cna_bgfx_test(cna_test_bgfx_environmentmapeffect_combined …)` /
  `cna_register_backend_test(NAME Bgfx_EnvironmentMapEffect_Combined …)`
  (`cmake/Tests/BgfxTests.cmake:231-233`).
- XNA/FNA relevance: direct — exercises `EnvironmentMapEffect.EnvironmentMapAmount`,
  `EnvironmentMapSpecular`, `FresnelFactor`, `EyePosition` (derived), `World`/`View`/`Projection`
  together in one scene, the broadest single-scene test of this effect in the shard.
- FNA reference: `HLSL/EnvironmentMapEffect.fx` `PSEnvMapSpecular`/`ComputeFresnelFactor`/
  `ComputeEnvMapVSOutput`.
- Related production code: `src/Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.cpp`
  (`OnApply()`/`FillGpuDrawParams()`, `EyePosition` derivation lines 452-456),
  `src/CNA/Internal/Backends/Bgfx/shaders/fs_env_map3d.sc`/`vs_env_map3d.sc` (`u_normalMatrix`
  inverse-transpose, `u_envMapSpecular.w` repurposed to carry `FresnelFactor`).

## Purpose

A single, deliberately non-degenerate scene combining every major `EnvironmentMapEffect` input at
once: non-identity `World` (`Matrix::CreateScale(2,1,1)`), a real `LookAt` `View`
(`eye=(0,0,3)`, `target=Zero`, `up=Y`), a real perspective `Projection`
(`CreatePerspectiveFieldOfView(PiOver4, aspect=1, near=0.1, far=100)`), `EnvironmentMapAmount=1`,
non-zero `EnvironmentMapSpecular=(0.4,0.4,0.4)`, `FresnelFactor=1`, and a translucent
(`alpha=128/255`) cube map — asserting one precise pixel value against the full combination.

## Executive Verdict

**Healthy** — independently re-derived the single expected pixel value from first principles
(camera geometry, normal transform, Fresnel gate, lerp, unconditional specular term) without
relying on the file's own arithmetic, and it matches to within 0.2 of a unit per channel (exact
after 8-bit rounding).

## Checklist Results

### Behavioral correctness — full independent geometric + shading re-derivation
- World scale `(2,1,1)` maps the quad's `z=0` plane vertices from `x∈[-1,1]` to `x∈[-2,2]`;
  `y∈[-1,1]` unchanged. The normal `(0,0,1)` is unaffected by an x-only scale under the
  inverse-transpose transform (`v_normal = mul(u_normalMatrix, a_normal)`,
  `vs_env_map3d.sc:23`), remaining `(0,0,1)` after normalize.
- Camera at `(0,0,3)` looking at the origin along `-Z`, `up=Y`: the ray from the eye through the
  exact center of the viewport intersects the quad's `z=0` plane exactly at world `(0,0,0)`, which
  is where the sampled center pixel (`Rectangle(kSize/2,kSize/2,1,1)`) lands (up to the same
  sub-pixel offset discussed in the `amount_one` report, immaterial here since the geometry is not
  degenerate).
- At world `(0,0,0)`: `eyeVector = normalize(EyePosition(0,0,3) - (0,0,0)) = (0,0,1)`; `N=(0,0,1)`;
  `dot(E,N)=1` exactly (a genuine head-on view, unlike the `amount_one`/`amount_zero` siblings'
  deliberately grazing Identity-view setup). `blendFactor = pow(max(1-|1|,0), FresnelFactor)*
  EnvironmentMapAmount = pow(0,1.0)*1.0 = 0` — the Fresnel term is at its *minimum* here (head-on),
  the opposite regime from the other two files in this shard, giving this test genuinely different,
  complementary coverage of the Fresnel gate's range.
- `litRGB = (EmissiveColor(0.5,0.5,0.5) + lightSum(0, no light diffuse set)) * DiffuseColor(1,1,1)
  = (0.5,0.5,0.5)`; `texColor=(200,100,50)/255=(0.7843,0.3922,0.1961)`; `baseColor =
  litRGB*texColor = (0.39215,0.19608,0.09804)`; `combinedAlpha = DiffuseColor.a(1) * texColor.a(1) =
  1`.
- `mix(baseColor, envColor, blendFactor=0) = baseColor` — the lerp contributes nothing (as
  expected for a head-on view under Fresnel gating), matching this file's own header note that this
  scene tests "lerp+specular+Fresnel+EyePosition+non-identity World" together — specifically, this
  scene isolates the *specular* term's unconditional nature since the lerp itself is fully gated
  out.
- `reflDir = reflect(-E,N)` at `E=N=(0,0,1)` evaluates to `(0,0,1)` (reflecting a vector already
  aligned with the normal back onto itself), sampling the cube's `PositiveZ` face — irrelevant here
  since all 6 faces were set to the same translucent color (`kTranslucentCube(0,0,0,128)`,
  `SetData` loop lines 84-91), so `envSample=(0,0,0,128/255=0.50196)`.
- Unconditional specular term: `rgb += EnvironmentMapSpecular(0.4,0.4,0.4) * envSample.w(0.50196) *
  combinedAlpha(1) = (0.200784,0.200784,0.200784)`.
- Total: `rgb = baseColor + specularTerm = (0.39215+0.200784, 0.19608+0.200784,
  0.09804+0.200784) = (0.592934, 0.396864, 0.298824) → (151.2, 101.2, 76.2) → (151,101,76)` after
  8-bit rounding — **matches the asserted `Color(151,101,76,255)` (line 142-144) exactly**, derived
  independently of the file's own arithmetic (which is not shown in-file; this audit computed it
  from the production shader/effect source alone).

### Logic
This scene's specific value of `FresnelFactor=1` combined with a perfectly head-on camera means the
lerp term is fully suppressed and the *only* signal in this test's non-baseline pixel value is the
unconditional specular `+=` term — a well-chosen configuration that (unlike a Fresnel=0 scene, which
would blend the full cube color in) isolates and verifies that `EnvironmentMapSpecular` is added
*outside* the Fresnel-gated lerp, exactly matching FNA's real `PSEnvMapSpecular` structure
(`color.rgb = lerp(...); color.rgb += EnvironmentMapSpecular * envmap.a;`, two separate statements,
not fused into a single gated term).

### C++ correctness / Robustness
Same `RasterizerState::CullNone` and read-until-nonblack retry loop pattern as the rest of this
shard; `MathHelper::PiOver4` used for FOV (line 125), a real perspective matrix rather than a
degenerate/Identity one, making this the only file among the 8 audited in this batch that exercises
a genuine 3D camera transform end-to-end for `EnvironmentMapEffect`.

### Testing
A single assertion, but one that meaningfully combines more independent effect parameters than any
other file in this shard, and — per the re-derivation above — specifically isolates the
"specular term is unconditional, not gated by the Fresnel/lerp blend factor" behavior that this
audit separately flagged (`bgfx_env_map_test.cpp` finding F2) as *not* independently pixel-verified
at `EnvironmentMapAmount=0`. This file verifies that unconditional-specular property only at
`EnvironmentMapAmount=1`; F2's gap (the same property at `EnvironmentMapAmount=0`) remains open.

## Detailed Findings

None. The single expected pixel value is an exact independent re-derivation matching the current
production formula across every combined input (world scale, camera, Fresnel, specular, alpha).

## Cross-File Observations

- This file, `amount_one_test.cpp`, and `amount_zero_test.cpp` together exercise the Fresnel gate's
  full practical range: head-on (`blendFactor=0`, this file) vs. grazing-by-construction
  (`blendFactor=1`, the other two) — a detail not called out explicitly in any of the three files'
  own comments, but confirmed by this audit's independent geometric analysis across all three
  reports.
- See `bgfx_env_map_test.cpp`'s audit (finding F2): this file is the closest existing pixel-verified
  proof that `EnvironmentMapSpecular` is unconditional, but only at `EnvironmentMapAmount=1`: F2's
  gap (same property at `Amount=0`) is not closed by this file.

## Missing or Weak Tests

See F2 in `bgfx_env_map_test.cpp`'s report — this file does not close that gap (it uses
`EnvironmentMapAmount=1`, not `0`).

## Positive Findings

- The most rigorous single test in this shard: it is the only file combining a non-identity
  `World`, a real `LookAt` view, and a real perspective projection with `EnvironmentMapEffect`, and
  its expected value was independently confirmed correct against first-principles geometry and
  shading math, not merely restated from the file.
- Correctly isolates and proves the specular term's independence from the Fresnel-gated lerp via a
  deliberately head-on camera angle that fully suppresses the lerp contribution — a non-obvious and
  effective test-design choice.

## Final Assessment

The strongest and most comprehensive `EnvironmentMapEffect` test in this batch; its single pixel
assertion was independently re-derived from first principles across every combined input and
matches exactly, and it meaningfully (if not completely — see the linked `Amount=0` gap) verifies
that `EnvironmentMapSpecular` is added unconditionally rather than gated by the Fresnel/lerp blend
factor, exactly matching FNA's real shader structure.
