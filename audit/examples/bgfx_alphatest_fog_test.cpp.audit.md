# Audit: examples/bgfx_alphatest_fog_test.cpp

## Metadata

- Source file: `examples/bgfx_alphatest_fog_test.cpp` (168 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `AlphaTestEffect` linear-fog pixel integration test
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_bgfx_test(cna_test_bgfx_alphatest_fog …)` / `cna_register_backend_test(NAME Bgfx_AlphaTest_Fog …)`,
  `cmake/Tests/BgfxTests.cmake:347-350`).
- XNA/FNA relevance: direct — `AlphaTestEffect.FogEnabled`/`FogColor`/`FogStart`/`FogEnd` (via
  `IEffectFog`).
- FNA reference: `src/Graphics/Effect/StockEffects/EffectHelpers.cs` (`SetFogVector`),
  `HLSL/Common.fxh` (`ComputeFogFactor`/`ApplyFog`).
- Related production code: `src/CNA/Internal/Backends/Bgfx/shaders/vs_alpha_test3d.sc` (fog-factor
  computation), `src/CNA/Internal/Backends/Bgfx/shaders/fs_alpha_test3d.sc` (fog blend),
  `src/Microsoft/Xna/Framework/Graphics/AlphaTestEffect.cpp` (`FillGpuDrawParams()`).

## Purpose

Proves `AlphaTestEffect`'s linear-fog blend is real interpolation (not an on/off switch) on Bgfx, using a
white 1×1 texture and `AlphaFunction` left at its always-passing default so only the fog blend is under
test. Three samples: `Z=FogStart` ("no fog" per the file's own labeling), `Z=FogEnd` ("pure fog"), `Z=0`
("halfway"). The header comment states this is a direct port of `easygl_alphatest_fog_test.cpp` with "the
same formula, same expected values."

## Executive Verdict

**Significant correctness risk** — the fog formula actually implemented in `vs_alpha_test3d.sc` (and shared
by every other Bgfx 3D pipeline) is a formula that this repository's own git history has already identified,
via real oracle-diff-against-XNA testing, as **not equivalent to FNA's real fog behavior** and fixed in the
sibling EasyGL backend nine days after this file was authored — but the fix was never back-ported to Bgfx
(or Vulkan, which shares the identical formula). This test's three expected values encode the *wrong*
(pre-fix, oracle-disproven) formula as if it were correct, with `FogStart`/`FogEnd`'s "no fog"/"full fog"
roles backwards relative to true FNA fog. See F1.

## Checklist Results

### API / XNA / FNA parity

`AlphaTestEffect::FillGpuDrawParams()` (`AlphaTestEffect.cpp` lines 329-335) correctly forwards
`FogEnabled`/`FogColor`/`FogStart`/`FogEnd` to the backend-common `GpuDrawParams` struct — this part of the
C++ plumbing is correct and matches `IEffectFog`. The defect (F1) is entirely on the GPU shader side.

### Behavioral correctness / Logic — F1 (fog formula)

I independently re-derived FNA's real fog factor from first principles, using
`EffectHelpers.SetFogVector` and `Common.fxh`'s `ComputeFogFactor`/`ApplyFog`:

```
scale = 1 / (FogStart - FogEnd)
FogVector = (worldView.M13*scale, worldView.M23*scale, worldView.M33*scale, (worldView.M43+FogStart)*scale)
fogFactorFNA(position) = saturate(dot(position, FogVector))          // 1 = full fog, 0 = no fog
ApplyFog: color.rgb = lerp(color.rgb, FogColor*color.a, fogFactorFNA)
```

With `World=View=Identity` (every scene in this test family), `worldView=Identity`
(`M13=M23=0, M33=1, M43=0`), so for a homogeneous object-space position `(x,y,z,1)`:

```
fogFactorFNA(z) = saturate((z + FogStart) / (FogStart - FogEnd))
```

Converting to the Bgfx/EasyGL shader convention actually used here (`mix(FogColor, color, factor)`, i.e.
`factor = 1 - fogFactorFNA`, "fraction of original color") via the exact identity `1 - saturate(x) =
saturate(1 - x)`:

```
factor_correct(z) = saturate((z + FogEnd) / (FogEnd - FogStart))
```

This is **exactly** the formula this project's own `74ad3bae` commit ("fix(Task 1111): correct EasyGL's fog
formula to genuinely match FNA, not just at z=0", 2026-07-16) independently derived and installed into
EasyGL, validated by a real XNA-oracle diff (`plans/plan_dx9.md D9-A6`: divergent pixels dropped from 23,716/65,536
to near-zero). That commit's own message explicitly calls out the *other* formula —
`(FogEnd-z)/(FogEnd-FogStart)` — as "a naive... falloff... **never actually equivalent to FNA even for
FogStart<FogEnd**."

`vs_alpha_test3d.sc` (lines 18-22) currently computes exactly that disproven formula:

```
v_fogFactor = clamp((u_fogParams.z - a_position.z) / max(u_fogParams.z - u_fogParams.y, 1e-6), 0.0, 1.0)
            = clamp((FogEnd - z) / (FogEnd - FogStart), 0, 1)
```

This differs from `factor_correct(z)` by the sign of `z` (`FogEnd-z` vs. `z+FogEnd`); algebraically,
`Bgfx's formula(z) == factor_correct(-z)` for any `FogStart`/`FogEnd` — a full mirror-image of the correct
falloff along the Z axis, not a coincidental near-miss.

Concretely, for this file's own scene (`kFogStart=-0.9`, `kFogEnd=0.9`):

| Z | This test's expected label | `factor_correct` (true FNA-equivalent) | Bgfx's actual/tested formula |
|---|---|---|---|
| `-0.9` (=FogStart) | `kExpectedNoFog` — unblended material | `saturate(0/1.8)=0` → **full fog** | `clamp(1.8/1.8,0,1)=1` → no fog |
| `+0.9` (=FogEnd)   | `kExpectedFullFog` — pure fog color   | `saturate(1.8/1.8)=1` → **no fog**  | `clamp(0/1.8,0,1)=0` → full fog |
| `0`                | `kExpectedHalfFog` — 50/50 blend      | `saturate(0.9/1.8)=0.5` → 50/50 (matches by symmetry) | `0.5` |

The `Z=0` midpoint happens to agree either way (this scene's `FogStart=-FogEnd` symmetry masks the sign
flip at exactly one point), but the `Z=FogStart`/`Z=FogEnd` labels are **exactly backwards** relative to true
FNA fog — the same specific class of error Task 1111's own commit message says it found and fixed in two of
EasyGL's five pre-existing fog tests ("two had z=FogStart/z=FogEnd labels exactly backwards").

### Cross-file consistency

`fs_alpha_test3d.sc`'s `mix(u_fogColor.xyz, color.rgb, v_fogFactor)` convention matches EasyGL's own
"`vFogFactor` = fraction of original color" convention exactly (so the two backends' *conventions* agree);
it is only the upstream `v_fogFactor` *computation* that diverges. `vs_colored3d.sc` and
`vs_lit_textured3d.sc` (used by `bgfx_basiceffect_fog_test.cpp` and `bgfx_basiceffect_lit_fog_test.cpp`
respectively) contain the byte-for-byte identical `(u_fogParams.z - a_position.z)` formula, and
`src/CNA/Internal/Backends/Vulkan/shaders/colored3d.vert.glsl` (`fragFogFactor = clamp((fog.fogStartEnd.y -
inPos.z)/..., 0, 1)`, explicitly commented "Task 899: ...matches the established Task 888 formula") carries
the same bug — this is a shared Vulkan+Bgfx defect, not Bgfx-specific, dating to the same `Task 888`/`899`
commits (`401deaed`, `c2386302`, 2026-07-07), all predating the EasyGL fix by nine days.

### Maintainability

The file's own header comment ("Formula (matches EasyGL's already-tested formula exactly, Task 378): ...")
was accurate when written (2026-07-06/07, before Task 1111 existed) but is now stale/misleading — it
describes the *pre-fix* EasyGL formula as still-current, ten days after EasyGL itself moved on. This is not
a fabricated claim by this file's author (it was true at authoring time); it is an unnoticed side effect of
a later, unrelated-looking fix landing in a sibling backend without a corresponding Bgfx/Vulkan follow-up
task being opened.

## Detailed Findings

### F1 — Bgfx's (and Vulkan's) 3D fog formula uses a z-sign that this repository's own oracle-diff testing already proved is not equivalent to FNA fog; this test's three expected values validate that incorrect behavior

- Severity: HIGH
- Confidence: HIGH — derived independently from FNA's real `EffectHelpers.SetFogVector`/`Common.fxh`
  source with exact algebra (not approximation), and the conclusion matches this repository's own
  already-landed, oracle-verified fix for the mathematically identical EasyGL case
- Category: correctness / FNA-parity / cross-backend-consistency
- Location/symbol: `src/CNA/Internal/Backends/Bgfx/shaders/vs_alpha_test3d.sc:20-22`
  (`v_fogFactor = clamp((u_fogParams.z - a_position.z)/..., 0, 1)`); test assertions at
  `bgfx_alphatest_fog_test.cpp:136-146` (`kExpectedNoFog`/`kExpectedFullFog`/`kExpectedHalfFog`)
- Evidence: see the full derivation above; corroborated by commit `74ad3bae`'s own message identifying
  `(FogEnd-z)/(FogEnd-FogStart)` by name as disproven, and by direct comparison of `vs_alpha_test3d.sc`,
  `vs_colored3d.sc`, `vs_lit_textured3d.sc` (Bgfx) and `colored3d.vert.glsl` (Vulkan), all sharing the exact
  same formula, none touched by any commit after `74ad3bae` (`git log --since=2026-07-16 -- .../Bgfx/shaders/
  .../Vulkan/shaders/` shows only unrelated PBR-shader commits).
- Why it matters: any real (non-`FogStart=-FogEnd`-symmetric) scene using `AlphaTestEffect`/`BasicEffect`
  fog on Bgfx or Vulkan today renders with fog appearing on the *wrong side* of the Z range relative to real
  XNA/FNA — e.g. objects meant to fade *into* fog as they recede would instead clear up, and vice versa.
  This test (and its two `bgfx_basiceffect_*_fog_test.cpp` siblings) cannot catch this because their expected
  values were derived from the current (buggy) implementation's actual output, not from independently
  re-derived FNA math — exactly the failure mode Task 1111's commit message describes finding and fixing in
  EasyGL's own pre-existing fog tests.
- FNA/XNA comparison: genuine divergence — confirmed via exact symbolic derivation from
  `EffectHelpers.SetFogVector`/`Common.fxh` (see above), independently corroborated by this project's own
  `plans/plan_dx9.md D9-A6` XNA-oracle-diff finding for the mathematically identical EasyGL case.
- Related files: `bgfx_basiceffect_fog_test.cpp` (colored3d pipeline, same bug), `bgfx_basiceffect_lit_fog_test.cpp`
  (lit_textured3d pipeline, same bug); Vulkan's `colored3d.vert.glsl` and siblings (same bug, out of this
  batch's scope but flagged as a cross-backend consequence); `74ad3bae` (the EasyGL fix that was never
  back-ported).
- Suggested future action (not implemented by this audit — audit-only task): port `74ad3bae`'s
  `(z+FogEnd)/(FogEnd-FogStart)` formula (with the `FogStart==FogEnd` degenerate guard) into every Bgfx 3D
  vertex shader and the Vulkan equivalents, then rewrite this test's (and its two siblings') expected values
  and Z↔label pairings to match the corrected, FNA-equivalent formula.

## Cross-File Observations

- Shares this exact bug with `bgfx_basiceffect_fog_test.cpp` and `bgfx_basiceffect_lit_fog_test.cpp` (this
  batch) and with Vulkan's fog implementation (out of batch scope, noted for completeness).
- The retry-loop readback pattern (`renderAtZ()` lines 92-125) is present and correctly used here, unlike
  `bgfx_alphatest_comparefunction_sweep_test.cpp` in the same batch (see that file's F1).

## Missing or Weak Tests

- No test in this family exercises fog with a non-identity `World`/`View` — meaning even a *correct* fix to
  F1 would still leave the more general `World`/`View`-dependent fog path (a proper `worldView`-derived
  `FogVector`, vs. this test family's `Identity`-only simplification) with zero pixel coverage on Bgfx.

## Positive Findings

- The C++-side plumbing (`AlphaTestEffect::FillGpuDrawParams()` forwarding `FogEnabled`/`FogColor`/
  `FogStart`/`FogEnd`) is correct and not implicated in F1 — the defect is confined to the GPU shader's
  fog-factor arithmetic.
- The test's structure (3-point sweep proving a genuine blend, not a threshold) is methodologically sound;
  only its *numeric ground truth* is wrong, not its design.
- Correctly isolates the fog blend from the alpha-test discard path (default `AlphaFunction=Greater`,
  `ReferenceAlpha=0`, combined alpha always 1.0) and from texture sampling (white 1×1, identity factor).

## Final Assessment

A well-constructed test exercising a real production defect it does not catch: Bgfx's fog formula is the
exact naive/incorrect one this repository already disproved via XNA-oracle testing and fixed in EasyGL, but
never back-ported to Bgfx or Vulkan. This test's own expected values encode the bug as correct behavior.
This is the most significant finding in this batch and recurs identically in two sibling files.
