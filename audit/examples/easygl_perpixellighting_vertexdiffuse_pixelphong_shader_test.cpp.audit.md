# Audit: examples/easygl_perpixellighting_vertexdiffuse_pixelphong_shader_test.cpp

## Metadata

- Source file: `examples/easygl_perpixellighting_vertexdiffuse_pixelphong_shader_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — HLSL→GLSL shader-conversion proof for XNA Game Studio's
  `PerPixelLighting.fx`, `PerVertexDiffuseAndPerPixelPhong` technique
- File type: C++ example/integration-test executable
  (`EasyGLPerPixelLightingVertexDiffusePixelPhongTest : Microsoft::Xna::Framework::Game`, `main()`)
- Related production code: `Microsoft::Xna::Framework::Graphics::ShaderEffect` (`ShaderEffect.cpp`),
  `ContentManager`'s `.cnj` `EffectTypeReader` (`ContentManager.cpp` lines 715-820)
- XNA/FNA relevance: real XNA Game Studio sample content — independently confirmed the file's transcription of
  `PerVertexDiffuseVS`/`PhongPS` against the actual
  `PerPixelLightingSample_4_0/PerPixelLighting/Content/PerPixelLighting.fx` (lines 59-100, technique
  `PerVertexDiffuseAndPerPixelPhong` lines 229-241) — verbatim match.
- Main related tests: this file itself (Task 947, Phase 78 rollout); 3rd of the 3
  `PerPixelLighting.fx` technique ports in this batch, alongside
  `easygl_perpixellighting_shader_test.cpp` and `easygl_perpixellighting_diffuseonly_shader_test.cpp`.

## Purpose

Ports the hybrid `PerVertexDiffuseAndPerPixelPhong` technique — diffuse+ambient computed once per vertex
(interpolated as a `vColor` varying), specular recomputed per pixel — demonstrating the "faceted diffuse, smooth
specular" middle ground between the sample's fully-per-vertex and fully-per-pixel techniques. Verified for the
same Identity/RotationY180 `World` pair as its 2 siblings in this batch.

## Executive Verdict

**Healthy.** This file's own most notable technical claim — that the fragment shader must use `precision highp
float` rather than the more common `mediump`, specifically because `lightPosition` is now a uniform shared across
*both* pipeline stages (unlike its 2 siblings, where it's fragment-only) — was checked for internal consistency
against those 2 siblings and found correct: only this file declares `lightPosition` in both the vertex and
fragment shader. Both expected pixel triples were independently recomputed and matched exactly, including the
non-trivial "all 4 corners compute the identical per-vertex diffuse value" symmetry argument. No HIGH/CRITICAL
findings; one shared LOW housekeeping item (temp-directory cleanup).

## Checklist Results

### API / XNA / FNA parity
N/A directly, but independently confirmed the ported logic against the real sample source. `PerVertexDiffuseVS`
(real source lines 59-89, abbreviated in this file's own header at lines 11-21): computes `output.Position`,
`output.WorldNormal`, `output.WorldPosition`, then (per-vertex) `directionToLight`/`diffuseIntensity`/`diffuse`/
`output.Color = diffuse + ambientLightColor` — matches the ported `kVertSrc` (lines 93-116) term-for-term,
including the diffuse-and-ambient computation happening in the *vertex* stage, correctly distinguishing this
technique from its 2 per-pixel-diffuse siblings. `PhongPS` (real source lines 100-... via the file's own header
transcription, lines 22-32): recomputes `directionToLight`/`reflectionVector`/`directionToCamera`/`specular` per
pixel from the interpolated `WorldPosition`/`WorldNormal`, then `color = input.Color + specular` — matches the
ported `kFragSrc` (lines 125-146) exactly, including reusing the vertex-computed `vColor` rather than recomputing
diffuse in the fragment stage.

### Behavioral correctness
Independently re-derived both checks using the file's own stated symmetry argument (quad's 4 corners are
symmetric around the origin relative to the light on the Z axis, so each corner computes an *identical* diffuse
value regardless of `(x,y)` sign — verified this is geometrically sound: for a corner at `(±0.5,±0.5,0)`, the
vector to light `(0,0,5)-(±0.5,±0.5,0) = (∓0.5,∓0.5,5)` has the same magnitude and the same Z-component ratio
after normalization regardless of the X/Y signs, so `dot(directionToLight,(0,0,1))` — the Z-component of the
normalized vector — is identical at all 4 corners):
- Check A (`World=Identity`): per-corner `directionToLight` Z-component `= 5/sqrt(0.5²+0.5²+5²) =
  5/sqrt(25.5) ≈ 0.99015`, matching the file's own stated `0.99015` — `diffuse = diffuseLightColor*0.99015 =
  (0.4,0.3,0.2)*0.99015 = (0.39606,0.29704,0.19803)`, matching the file's own values exactly. Per-pixel specular at
  the exact centre is identical to the sibling `PerPixelDiffuseAndPhong` test's own Check A: `(0.3,0.3,0.3)` (same
  light/camera-on-axis geometry). `color = (0.3+0.39606+0.1, 0.3+0.29704+0.05, 0.3+0.19803+0.02) =
  (0.79606,0.64704,0.51803)` → bytes `round(255*...) = (203.0→203, 165.0→165, 132.1→132)`. **Matches the file's
  claimed `(203,165,132)` exactly.**
- Check B (`World=RotationY180`): per-vertex `diffuseIntensity` clamps to 0 at every corner (the flipped
  `worldNormal` reasoning is identical to the sibling tests' Check B), so `diffuse=(0,0,0)`; per-pixel specular at
  the centre is unchanged (same on-axis symmetry): `(0.3,0.3,0.3)`. `color = (0.3,0.3,0.3)+(0,0,0)+(0.1,0.05,0.02)
  = (0.4,0.35,0.32)` → bytes `(102, 89.25→89, 81.6→82)`. **Matches the file's claimed `(102,89,82)` exactly.**
Both checks recompute correctly, including the specific numeric symmetry claim (`0.99015`) rather than just the
qualitative direction of each check.

### Logic
The precision-qualifier claim (lines 118-124, "precision must be `highp`, not the more usual `mediump` ...
confirmed empirically: `mediump` here produces a real link error") was checked for self-consistency against this
file's 2 siblings in this batch: `easygl_perpixellighting_shader_test.cpp` and
`easygl_perpixellighting_diffuseonly_shader_test.cpp` both declare `lightPosition` **only** in their fragment
shaders (never in their vertex shaders), so neither of them faces a cross-stage precision-qualifier conflict for
that uniform — this file is the only one of the 3 where `lightPosition` is read in both stages (the vertex shader
needs it for the per-vertex diffuse calculation, the fragment shader needs it again for
`directionToLight`/`reflect()`), which is exactly the situation GLSL ES's "matching precision across a linked
program's stages" rule would flag. This is a genuinely correct, non-obvious piece of GLSL ES reasoning, not an
unverified assertion.

### Memory/resource lifetime
Same per-instance-pointer temp-directory pattern as every sibling test in this batch (lines 163-176), never
cleaned up — see Detailed Findings F1.

### Robustness
`Draw()` checks `!fx || !fx->IsEffectValid()` (lines 237-243) — consistent with the rest of this batch.

### Testing
This file is itself a test. Its 2-check design is identical in shape to its 2 siblings (Identity vs. RotationY180)
but targets the specific hybrid-shading concern this technique introduces: does the vertex-stage diffuse
computation and the fragment-stage specular computation compose correctly (additive, `input.Color + specular`)
rather than one silently overwriting the other. Check B (diffuse zeroed, specular preserved) is exactly the case
that would catch a bug where the fragment shader accidentally discarded `vColor` instead of adding to it.

## Detailed Findings

### F1 — Temp directory written per test run, never cleaned up

- Severity: LOW
- Confidence: HIGH
- Category: maintainability / resource lifetime
- Location/symbol: `Initialize()`, lines 163-176
- Evidence: no cleanup call for the created temp directory exists anywhere in this file.
- Why it matters: identical, shared, low-priority finding already recorded for every other hand-rolled
  `ShaderEffect` test in this batch.
- FNA/XNA comparison: N/A.
- Related files: `easygl_particleeffect_shader_test.cpp`,
  `easygl_perpixellighting_diffuseonly_shader_test.cpp`, `easygl_perpixellighting_shader_test.cpp`,
  `easygl_postprocesseffect_shader_test.cpp`.

## Cross-File Observations

- Together with its 2 siblings in this batch, this file completes 3 of `PerPixelLighting.fx`'s 5 real techniques;
  the file's own header (lines 3-4) correctly numbers itself "3 of 5" and names the 2 remaining un-ported
  techniques are implied by elimination (the fully-per-vertex `PerVertexDiffuse` and whichever 5th technique the
  sample defines) — consistent bookkeeping across the 3 files audited in this batch.
- The `precision highp` requirement this file documents is a real, project-relevant GLSL ES gotcha specific to
  uniforms shared across both shader stages — worth flagging for any future port of a technique that similarly
  needs the same uniform in both stages (a class of bug that would otherwise only surface as an opaque driver link
  error).

## Missing or Weak Tests

- Both checks share the exact same specular value (`(0.3,0.3,0.3)`) between Check A and Check B by construction
  (the scene's own on-axis symmetry) — a bug that broke the *vertex-stage* diffuse-and-ambient computation in a
  way that happened to still zero out at RotationY180 (e.g. an inverted clamp) could still pass Check B for the
  wrong reason, since Check B's expected diffuse contribution is zero either way. A 3rd `World` orientation with a
  partial (non-zero, non-maximal) diffuse intensity would close this gap, but is not present here or in either
  sibling file.

## Positive Findings

- FNA-Game-Studio-sample transcription (`PerVertexDiffuseVS` + `PhongPS`) independently confirmed verbatim against
  the real `.fx` source.
- Both expected pixel triples independently recomputed from first principles, including the specific
  `0.99015` per-corner diffuse-intensity symmetry value, and found to match exactly.
- The `precision highp` cross-stage-uniform reasoning is genuinely correct and non-obvious — a good example of a
  test file documenting *why* a seemingly-arbitrary GLSL qualifier choice is load-bearing rather than just
  asserting it works.

## Final Assessment

The most technically subtle of the 3 `PerPixelLighting.fx` technique ports in this batch, correctly handling a
real cross-stage GLSL ES precision-qualifier constraint that its 2 siblings don't need to. Both expected pixel
values were independently re-derived and confirmed exact. Its only gap is the shared, low-priority temp-directory
cleanup omission common to this test family, plus a minor, largely theoretical coverage gap around partial
(non-zero, non-maximal) diffuse intensity.
