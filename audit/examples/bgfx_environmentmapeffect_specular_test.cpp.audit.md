# Audit: examples/bgfx_environmentmapeffect_specular_test.cpp

## Metadata

- Source file: `examples/bgfx_environmentmapeffect_specular_test.cpp` (181 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `EnvironmentMapEffect.EnvironmentMapSpecular`
  contribution, Bgfx backend, Task 395.
- CTest registration: `cna_bgfx_test(cna_test_bgfx_environmentmapeffect_specular …)` /
  `cna_register_backend_test(NAME Bgfx_EnvironmentMapEffect_Specular …)`
  (`cmake/Tests/BgfxTests.cmake:199-201`).
- XNA/FNA relevance: direct — `EnvironmentMapEffect.EnvironmentMapSpecular`.
- FNA reference: `HLSL/EnvironmentMapEffect.fx`'s `PSEnvMapSpecular`: `color.rgb +=
  EnvironmentMapSpecular * envmap.a` — added *after* the `lerp`, scaled by the cube map's own
  alpha channel (and, per Task 891, by the combined texture/diffuse alpha too).
- Related production code: `src/CNA/Internal/Backends/Bgfx/shaders/fs_env_map3d.sc:41` — `rgb =
  mix(baseColor, envSample.xyz*combinedAlpha, blendFactor) + u_envMapSpecular.xyz * envSample.w *
  combinedAlpha;`.

## Purpose

Two-check pixel test proving `EnvironmentMapSpecular` is scaled by the cube map's alpha channel
(not applied as a flat constant) — the real bug this task found and fixed (per the header comment,
lines 2-7): "CNA's env-map fragment shaders (all 3 backends) added `EnvironmentMapSpecular` as a
flat constant instead of FNA's real `EnvironmentMapSpecular * envmap.a`." `EnvironmentMapAmount=0`
(line 104) removes the cube map's own RGB blend entirely, isolating the specular-alpha add. Check
(a) uses an opaque (`alpha=255`) cube; check (b) uses a translucent (`alpha=128`) cube of the same
color, expecting a proportionally smaller specular contribution.

## Executive Verdict

**Healthy** — both expected constants were independently re-derived from the current production
formula (including the Task 891 `combinedAlpha` scaling of `envmap.a` itself) and match essentially
exactly (sub-1-unit rounding only).

## Checklist Results

### Behavioral correctness — full independent re-derivation
`emissiveColor_=(0.5,0.5,0.5)` (line 103), `DiffuseColor` left at its default `(1,1,1,1)`,
`DirectionalLight0`'s default zero diffuse: `litRGB=(0.5,0.5,0.5)`. `texColor=(200,100,50,255)/255
≈(0.7843,0.3922,0.1961)` (line 132, 136-137). `baseColor=litRGB*texColor≈
(0.39215,0.19608,0.09804)`. `combinedAlpha=DiffuseColor.a(1)*texColor.a(1)=1`.
`EnvironmentMapAmount=0` → `blendFactor=0` → the `mix()` term contributes exactly `baseColor`.
- Check (a) opaque cube (`alpha=255→1.0`, line 133, 138): `envSample.w=1.0`. Specular add
  `=EnvironmentMapSpecular(0.4,0.4,0.4)*1.0*combinedAlpha(1)=(0.4,0.4,0.4)`. `rgb=baseColor+
  (0.4,0.4,0.4)≈(0.79215,0.59608,0.49804)` → `(201.99,152.0,127.0)≈(202,152,127)`. Matches
  `Color(202,152,127,255)` (line 152-154) — this audit's independent derivation lands on the *exact*
  same integers.
- Check (b) translucent cube (`alpha=128→0.50196`, line 134, 139): specular add
  `=(0.4,0.4,0.4)*0.50196≈(0.200784,0.200784,0.200784)`. `rgb=baseColor+0.200784≈
  (0.592934,0.396864,0.298824)` → `(151.2,101.2,76.2)≈(151,101,76)`. Matches
  `Color(151,101,76,255)` (line 156-159) exactly (sub-unit rounding only).

Both checks are exact, and the choice of opaque-vs-translucent-cube-of-the-same-base-color is the
correct minimal discriminator for the specific Task 395 bug (a flat-constant implementation would
render the *same* specular add in both checks; this test would catch that immediately since
`(202,152,127)` and `(151,101,76)` are clearly distinct).

### Logic
`makeSolidCube()` (lines 84-95) with matching RGB but different alpha across the two checks is a
well-targeted fixture — it isolates the alpha-scaling behavior from any color-blend concern (the
cube's own RGB `(0,0,0)` never contributes to `baseColor` since `EnvironmentMapAmount=0` zeroes the
`mix()` blend entirely — only the cube's *alpha* channel, sampled through `envSample.w`, is
exercised).

## Detailed Findings

### F1 — Same `DiffuseColor`/`EmissiveColor` recombination defect as this batch's other `EnvironmentMapEffect` files; masked here identically

- Severity: HIGH
- Confidence: HIGH
- Category: fna-parity (production code exercised by, but not exposed by, this test)
- Location/symbol: `src/CNA/Internal/Backends/Bgfx/shaders/fs_env_map3d.sc:28`; same defect
  reproduced in `src/CNA/Internal/Backends/Vulkan/shaders/env_map3d.frag.glsl:39`. Full derivation
  in this batch's `bgfx_environmentmapeffect_eyeposition_test.cpp.audit.md` (F1).
- This file's own coverage: `EmissiveColor=(0.5,0.5,0.5)` (line 103) is non-zero but `DiffuseColor`
  is never set away from its default `(1,1,1,1)` — the buggy and correct formulas coincide
  numerically here, masking the defect.
- Suggested future action (not implemented by this audit): see the `eyeposition` report's F1 for the
  concrete shader fix.

## Cross-File Observations

- Per Task 364/884 (comment lines 9-13), the standard quad winding needs `RasterizerState::CullNone`
  (line 117) — independently re-verified against `RasterizerState.cpp:11`/
  `BgfxGraphicsBackend.cpp:1781-1782` in this batch's `eyeposition` report.
- `World=View=Projection=Identity` (lines 106-108) reproduces the same "eye embedded in the quad's
  own plane" geometry documented non-degenerate (via the pixel-center sampling offset) in the
  `amount_one`/`eyeposition` sibling reports; irrelevant to this file's own assertions since
  `EnvironmentMapAmount=0` removes the Fresnel-gated blend term (and hence any `viewAngle`
  dependency) entirely regardless of how that angle evaluates.
- Task 891's `combinedAlpha` scaling of `envSample.w` (`fs_env_map3d.sc:33,41`) matters for this
  file's own two checks only insofar as `combinedAlpha=1` throughout (texture and DiffuseColor are
  both fully opaque) — this file does not itself exercise a non-1 `combinedAlpha`, so it validates
  the `envmap.a`-scaling half of Task 891's fix but not the `combinedAlpha`-scaling half (see
  "Missing or Weak Tests").

## Missing or Weak Tests

- See F1.
- Neither check varies `Texture`'s or `DiffuseColor`'s alpha away from `1.0`, so this file does not
  independently exercise the `* combinedAlpha` half of the Task 891 fix (`fs_env_map3d.sc:41`'s
  `u_envMapSpecular.xyz * envSample.w * combinedAlpha` — only the `envSample.w` factor is varied
  here). A third check with, e.g., `alpha=128` texture or `DiffuseColor.A=0.5` alongside a fully
  opaque cube would close this gap.

## Positive Findings

- Both expected constants are precise, independently-reproducible re-derivations of the current
  production formula (matching to the integer, not merely within a tolerance band).
- The opaque-vs-translucent-cube fixture choice is the correct minimal discriminator for the
  specific historical bug (flat-constant specular) this task fixed.

## Final Assessment

A precise, well-targeted specular-alpha-scaling test whose two checks are exact and correctly
isolate the Task 395 fix from the cube-map RGB blend and Task 891's `combinedAlpha` term (the latter
held fixed at `1` rather than independently exercised — see "Missing or Weak Tests"). This audit's
review of the shared shader surfaced the same untracked `DiffuseColor`/`EmissiveColor`
recombination bug (F1) documented across this batch.
