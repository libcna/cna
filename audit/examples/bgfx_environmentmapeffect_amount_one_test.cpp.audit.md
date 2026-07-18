# Audit: examples/bgfx_environmentmapeffect_amount_one_test.cpp

## Metadata

- Source file: `examples/bgfx_environmentmapeffect_amount_one_test.cpp` (182 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `EnvironmentMapEffect.EnvironmentMapAmount=1` cube-map
  lerp/replace pixel test, Bgfx backend, Task 394.
- CTest registration: `cna_bgfx_test(cna_test_bgfx_environmentmapeffect_amount_one …)` /
  `cna_register_backend_test(NAME Bgfx_EnvironmentMapEffect_AmountOne …)`
  (`cmake/Tests/BgfxTests.cmake:193-195`).
- XNA/FNA relevance: direct — `EnvironmentMapEffect.EnvironmentMapAmount`, `FresnelFactor`
  (implicitly, via its FNA default of `1`), `EnvironmentMap`.
- FNA reference: `HLSL/EnvironmentMapEffect.fx` `PSEnvMap`: `color.rgb = lerp(color.rgb,
  envmap.rgb, pin.Specular.rgb)` — an interpolate/replace, not an additive blend.
- Related production code: `src/Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.cpp`
  (ctor default `setFresnelFactorProperty(1.0f)`, line 43; `FillGpuDrawParams()`),
  `src/CNA/Internal/Backends/Bgfx/shaders/fs_env_map3d.sc` (`mix(baseColor, envSample.xyz*
  combinedAlpha, blendFactor) + ...`), `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp`
  (`envMap3DProgram_` dispatch, `params.fresnelEnabled ? 1.0f : 0.0f` packed into
  `u_envMapAmount.y`, line 2665).

## Purpose

Two-check pixel test verifying that at `EnvironmentMapAmount=1`, the cube map genuinely
**replaces** (lerps to) the lit/textured base color rather than adding on top of it — the real bug
this task found and fixed (per the header comment, all 3 backends previously computed `rgb =
litRGB*texColor.rgb + envColor*amount + specular`, additive, instead of FNA's real
`lerp(baseColor, envColor, amount)`). Check (a) uses a white cube map ("not discriminating" per its
own label — white saturates to the same result whether additive or lerped). Check (b) uses a gray
cube map, which *does* discriminate: additive would produce a lighter, non-gray result; the correct
lerp fully replaces with gray.

## Executive Verdict

**Healthy** — independently re-derived both expected constants against the current production
formula (including the non-obvious geometric reason the Fresnel-gated blend factor evaluates to
`1`, not `0`, for this scene) and both match exactly.

## Checklist Results

### Behavioral correctness — full independent re-derivation
This test uses `World`/`View`/`Projection` all `Matrix::Identity` (lines 107-109). This places
`EyePosition` (derived from `Matrix::Invert(view_).getTranslationProperty()`,
`EnvironmentMapEffect.cpp:452-453`) at world-space `(0,0,0)`, which is *inside the plane of the quad
itself* (the quad's own `z=0`, and `World=Identity` leaves vertex Z at `0` too) — a deliberately
degenerate-looking setup that this audit verified is actually well-defined, not accidental:
- `eyeVector.z = EyePosition.z - worldPos.z = 0 - 0 = 0` **identically, everywhere on the quad**
  (both terms are exact IEEE-754 zero, not merely close to it, since `View`/`World` are exact
  identity matrices — no rounding is involved). Since the quad's normal is `(0,0,1)`, `dot(E,N) =
  E.z = 0` at every fragment except the single mathematical center point where the raw (pre-
  normalize) `eyeVector` itself is `(0,0,0)` — and the actual sampled pixel (center of a 64px
  backbuffer, sampled at pixel-center convention, i.e. NDC ≈ 0.0156 off exact center) is not that
  singular point, so the degenerate case is avoided in practice, not just presumed.
- With `dot(E,N)=0`, `fs_env_map3d.sc`'s Fresnel gate evaluates `blendFactor = pow(max(1-|0|,0),
  FresnelFactor) * EnvironmentMapAmount = pow(1, 1.0) * EnvironmentMapAmount = EnvironmentMapAmount`
  — i.e. the Fresnel term reaches its *maximum* (grazing-angle) value here, not zero, because the
  test's Identity-view setup happens to put the eye vector perpendicular to the surface normal, the
  Fresnel-maximizing geometry — this is the opposite of what a naive reading of "eye embedded in the
  surface" might suggest, and this audit confirmed it algebraically rather than assuming it.
- Check (a): `blendFactor=1` (since `EnvironmentMapAmount=1`, set line 105); `envColor =
  whiteCube(1,1,1)*combinedAlpha(1) = (1,1,1)`; `mix(baseColor,(1,1,1),1) = (1,1,1) → (255,255,255)`
  — matches `Color(255,255,255,255)` (line 153-155) exactly, regardless of `baseColor`'s actual
  value (hence the file's own "not discriminating" label being accurate).
- Check (b): `envColor = grayCube(128/255=0.50196)*1 = (0.50196,...)`; `mix(baseColor,
  (0.50196,...),1) = (0.50196,...) → (128,128,128)` (`EnvironmentMapSpecular=(0,0,0)`, line 106, so
  no additive term) — matches `Color(128,128,128,255)` (line 158-160) exactly. **This is the
  correct discriminator**: an additive (pre-fix) formula would instead compute `baseColor +
  grayColor*amount`, i.e. `≈(100,50,25)/255 + (128,128,128)/255 ≈` a lighter, non-gray color, which
  this assertion would have caught.

### Logic
`baseColor` itself was independently computed (`litRGB=(0.5,0.5,0.5)` from `EmissiveColor`, no
directional-light diffuse since `DirectionalLight0`'s default diffuse is `Vector3::Zero`
(`DirectionalLight.cpp:7`) despite being `Enabled=true`; `texColor=(200,100,50)/255`;
`baseColor=litRGB*texColor≈(0.392,0.196,0.098)`) purely to confirm it is *irrelevant* to both
assertions at `blendFactor=1` — both checks fully replace it, as intended.

### Testing
Both checks are precise pixel assertions with an independently-verified, non-accidental geometric
basis (the Fresnel-maximizing eye/normal alignment), not merely "looks plausible" constants.

## Detailed Findings

None. Both expected constants are exact re-derivations from the current production formula, and
the somewhat unusual (Identity-view) scene setup was independently confirmed to produce a
well-defined, non-degenerate result at the actual sampled pixel.

## Cross-File Observations

- This file, `bgfx_environmentmapeffect_amount_zero_test.cpp`, and
  `bgfx_environmentmapeffect_combined_test.cpp` together give complementary coverage: this file
  isolates the `EnvironmentMapAmount=1` replace behavior; the `amount_zero` sibling isolates
  `EnvironmentMapAmount=0`; the `combined` file uses a proper (non-degenerate) `LookAt`/perspective
  camera where the Fresnel term evaluates near-zero instead — together these three files exercise
  the Fresnel-gated blend factor across its effective range (`0` in `combined`'s head-on geometry,
  `1` in this file's and `amount_zero`'s grazing-by-construction geometry), even though no single
  file explicitly narrates why the Identity-view geometry produces a grazing angle.
- `bgfx_env_map_test.cpp`'s config (d) (`EnvironmentMapAmount=1` with a blue cube map) exercises the
  same code path as this file's check (a)/(b) but only as a no-crash smoke test — this file's check
  (b) is the one that actually pixel-verifies the fix that config (d) merely smoke-tests.

## Missing or Weak Tests

None specific to this file — see `bgfx_env_map_test.cpp`'s audit report (F2) for the one genuine
coverage gap in this shard (specular contribution at `EnvironmentMapAmount=0`), which is outside
this file's own scope (`EnvironmentMapSpecular=(0,0,0)` here).

## Positive Findings

- The "(not discriminating)" self-label on check (a) is accurate and shows the same
  engineering-honesty pattern seen elsewhere in this shard's test comments — flagging a weak
  assertion up front rather than letting a reader assume both checks are equally strong.
- Check (b)'s choice of a mid-gray cube map is the correct, minimal discriminator between the
  additive (buggy) and lerp (correct) formulas — a black or white cube map would not have caught
  the original bug as cleanly.

## Final Assessment

A precise, well-designed pixel test whose expected values check out exactly against the current
production formula; the scene's Identity-view geometry — initially read as potentially degenerate —
was independently confirmed to produce a robust, well-defined grazing-angle Fresnel result rather
than relying on GPU-implementation-specific rounding.
