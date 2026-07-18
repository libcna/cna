# Audit: examples/easygl_alphatest_fog_test.cpp

## Metadata

- Source file: `examples/easygl_alphatest_fog_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — `AlphaTestEffect` fog × EasyGL backend pixel test
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_easygl_test(cna_test_easygl_alphatest_fog …)` /
  `cna_register_backend_test(NAME EasyGL_AlphaTest_Fog …)`, `cmake/Tests/EasyGLTests.cmake:1132-1134`).
- XNA/FNA relevance: direct — exercises `AlphaTestEffect`'s `IEffectFog` properties
  (`FogEnabled`/`FogColor`/`FogStart`/`FogEnd`).
- FNA reference: `AlphaTestEffect.cs` (`FogEnabled`/`FogColor`/`FogStart`/`FogEnd` properties,
  `OnApply()`'s `EffectHelpers.SetWorldViewProjAndFog` call).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/AlphaTestEffect.cpp`
  (`FillGpuDrawParams()` lines 329-335 forward the 4 fog fields),
  `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp`
  (`EnsureColored3DProgram()`, the `uFogEnabled`/`uFogColor`/`uFogStart`/`uFogEnd`/`vFogFactor`
  uniforms and `mix()` blend around lines 2593-2627).

## Purpose

Verifies that `AlphaTestEffect`'s fog properties actually reach the GPU and blend correctly on
EasyGL, via a 3-point sweep at `z=FogEnd` (expect unblended material color), `z=FogStart` (expect
pure fog color), and `z=0` (expect an exact 50/50 blend) — proving genuine interpolation, not a
binary on/off toggle. The file's own header comment (lines 4-11) states this test *found and fixed*
a real bug: `FillGpuDrawParams()` previously never forwarded any of the 4 fog fields at all.

## Executive Verdict

**Healthy** — the test's 3 expected pixel values were independently re-derived from the current
`EasyGLGraphicsBackend.cpp` fog formula and match exactly (see F1), and the current
`AlphaTestEffect.cpp::FillGpuDrawParams()` does forward all 4 fog fields (lines 329-335), consistent
with the "found and fixed" claim in the header comment being about a past, now-resolved state
rather than a live gap. One genuine, already-acknowledged-in-comment project-wide gap (fog is a
total no-op on Vulkan/Bgfx) is correctly *not* silently asserted as passing here — see Cross-File
Observations.

## Checklist Results

### API / XNA / FNA parity
`setFogEnabledProperty`/`setFogColorProperty`/`setFogStartProperty`/`setFogEndProperty` (lines
129-132) match FNA's `IEffectFog` properties exactly (verified against
`include/.../AlphaTestEffect.hpp` lines 132-167, which declares all four as `override`s of
`IEffectFog`). No `World`/`View`/`Projection` are set in this test (left at `Matrix::Identity`
default), which the test's own comment (lines 13-27) correctly identifies as the reason raw
object-space `z` becomes `gl_Position.z` directly.

### Behavioral correctness
Independently re-derived the expected pixel values from EasyGL's actual fragment-shader formula
(`EasyGLGraphicsBackend.cpp` line 2610/2732/etc.:
`vFogFactor=(uFogEnabled>0.5)?((abs(uFogEnd-uFogStart)<1e-6)?0.0:clamp((aPos.z+uFogEnd)/(uFogEnd-uFogStart),0.0,1.0)):1.0`,
then `FragColor.rgb=mix(uFogColor,FragColor.rgb,vFogFactor)`), with this test's constants
`kFogStart=-0.9`, `kFogEnd=0.9`:
- `z=kFogEnd=0.9`: `vFogFactor=(0.9+0.9)/(0.9-(-0.9))=1.8/1.8=1.0` → `mix` returns the *original*
  material color unblended → `(204,51,102)`. Matches `kExpectedNoFog` (line 82) exactly.
- `z=kFogStart=-0.9`: `vFogFactor=(-0.9+0.9)/1.8=0` → `mix` returns pure fog color.
  `kFogColor=(0.1,0.6,0.9)*255=(25.5,153,229.5)` rounds to `(26,153,230)` — the material's own
  8-bit precision loss (0.1*255=25.5, rounds to 25 or 26 depending on GPU rounding mode) is well
  within this test's `closeTo(...,8)` tolerance (line 108-115). Matches `kExpectedFullFog` (line 83).
- `z=0`: `vFogFactor=(0+0.9)/1.8=0.5` → exact halfway `mix`: `(204+26)/2≈115`, `(51+153)/2≈102`,
  `(102+230)/2≈166`. Matches `kExpectedHalfFog=(115,102,166)` (line 84) exactly.
- All three derivations independently confirm the file's own header-comment derivation (lines
  75-84) is correct, and that the "half fog" case genuinely proves interpolation rather than a
  step function — the file's own claimed "discriminating power verified by mutation" methodology
  (dropping `blend_color.w *= FrameBlend.y` — actually documented in the *sibling* animsprite
  file, not this one; this file does not itself claim a mutation test, only asserts the 3-point
  sweep, correctly).

`renderAtZ()`'s retry loop (lines 143-155, up to 20 iterations skipping black frames) is present
here — consistent with two of its three sibling files in this shard (`_null_texture_test.cpp`,
`_vertexcolor_diffuse_test.cpp`) and, per this audit's cross-file check, a pattern the older
`_modes_test.cpp`/`_comparefunction_sweep_test.cpp` files lack (flagged in those files' own
reports, not repeated here as a finding against this file, which already has the guard).

### Logic
`RasterizerState::CullNone` applied per draw (line 150, "Task 896 finding") — consistent workaround,
correctly documented as pre-existing/known rather than novel to this test.

### C++ correctness
`closeTo`/`matches` (lines 108-115) use a fixed `±8` per-channel tolerance — reasonable for
GPU/driver rounding on 3 independently-derived expected colors, none of which are borderline
(closest theoretical gap between two of the three expected values is `102` vs `115`, far outside
tolerance collision range).

### Robustness
Correctly declines to add Vulkan/Bgfx coverage for the same feature (header comment lines 29-41)
rather than encode a known-broken no-op as a passing assertion — this is the right call and matches
this shard's established convention (see the sibling `_vertexcolor_diffuse_test.cpp`'s identical
stance on Task 887).

### Testing
Genuinely validates the fog blend is a real per-vertex-Z interpolation (3 numerically distinct
expected values, including a non-trivial halfway point) rather than merely "fog doesn't crash" —
satisfies this audit's anti-boilerplate bar.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings. The one point worth flagging is INFO-level:

### F1 — Fog formula and constants independently re-verified against live shader source

- Severity: INFO
- Confidence: HIGH
- Category: correctness (verification note)
- Location/symbol: `kFogStart`/`kFogEnd`/`kExpectedNoFog`/`kExpectedFullFog`/`kExpectedHalfFog`
  (lines 72-84), `EasyGLGraphicsBackend.cpp` fog uniform block (~lines 2593-2627)
- Evidence: see Behavioral correctness above.
- Why it matters: recorded so this doesn't need re-derivation on the next audit pass; the test's
  own claimed derivation checks out exactly against the current tree.

## Cross-File Observations

- The file's header comment (lines 29-41) documents, and this audit independently confirmed via
  `grep -c fog` across Vulkan/Bgfx `.glsl`/`.sc` shader sources being effectively silent, that fog
  is a total no-op on those two backends today, tracked as "Task 888" — a real, project-wide gap
  correctly *not* hidden by a false-passing test here. Worth surfacing again in whatever
  cross-cutting findings doc aggregates backend parity gaps (the Vulkan/Bgfx shard audits should
  independently confirm/refute the "zero fog uniforms anywhere" claim rather than take this file's
  word for it).
- `AlphaTestEffect.cpp::OnApply()` (not in this shard) separately computes a classic FNA-style
  `fogVectorParam_` dot-product vector (lines 198-228 of that file) that is *never consumed* by
  EasyGL's rendering path — EasyGL instead receives raw `fogStart_`/`fogEnd_`/`fogColor` via
  `FillGpuDrawParams()` and recomputes its own simpler per-vertex formula. Two parallel,
  independently-implemented fog computations coexisting in the same class is worth a note in the
  `AlphaTestEffect.cpp`/`xna-graphics` shard audit (this file's own scope is only the EasyGL-side
  formula, which was independently confirmed correct here).

## Missing or Weak Tests

None specific to this file — the 3-point sweep already establishes real interpolation, which is the
main thing worth proving for a fog feature.

## Positive Findings

- Choosing 3 numerically distinct expected outputs (not just "on" vs "off") is exactly the right
  design to prove a blend is a genuine `mix()`/lerp rather than a binary switch — reused correctly
  by this test.
- Correct restraint in not adding false-passing Vulkan/Bgfx assertions for a feature confirmed
  broken there.

## Final Assessment

A correct, well-verified fog test whose 3 expected values check out exactly against the live
EasyGL shader formula; its header comment's own bug-fix narrative is consistent with the current
state of `AlphaTestEffect.cpp::FillGpuDrawParams()`, which does forward all 4 fog fields today.
