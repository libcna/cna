# Audit: examples/bgfx_basiceffect_fog_test.cpp

## Metadata

- Source file: `examples/bgfx_basiceffect_fog_test.cpp` (207 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `BasicEffect` linear-fog pixel integration test (colored3d /
  stride-16 pipeline)
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_bgfx_test(cna_test_bgfx_basiceffect_fog …)` /
  `cna_register_backend_test(NAME Bgfx_BasicEffect_Fog …)`, `cmake/Tests/BgfxTests.cmake:335-338`).
- XNA/FNA relevance: direct — `BasicEffect.FogEnabled`/`FogColor`/`FogStart`/`FogEnd` (`IEffectFog`).
- FNA reference: `src/Graphics/Effect/StockEffects/EffectHelpers.cs` (`SetFogVector`),
  `HLSL/Common.fxh` (`ComputeFogFactor`/`ApplyFog`).
- Related production code: `src/CNA/Internal/Backends/Bgfx/shaders/vs_colored3d.sc` (fog-factor
  computation, lines 24-27), `fs_colored3d.sc` (fog blend, line 11).

## Purpose

Three-subtest fog pixel test on `BasicEffect`'s `colored3d` (stride-16 `VertexPositionColor`) pipeline: (a)
fog disabled → pure blue; (b) `FogStart=0, FogEnd=1, Z=0.5` → expects a 50/50 red/blue mix `(128,0,128)`,
tolerance ±30; (c) `FogStart=0, FogEnd=0.5, Z=0.9` → expects full fog (pure red). Explicitly a "direct port"
of `easygl_basiceffect_fog_test.cpp`'s Task 195, exercising the Task 888 fix that first brought real fog
rendering to Bgfx.

## Executive Verdict

**Significant correctness risk** — same production defect as `bgfx_alphatest_fog_test.cpp` (this batch,
see that file's F1 for the full derivation): `vs_colored3d.sc`'s fog-factor formula is the exact
`(FogEnd-z)/(FogEnd-FogStart)` shape this repository's own commit `74ad3bae` (Task 1111) already proved,
via oracle-diff against real XNA, is not equivalent to FNA's real fog math — yet it remains live in every
Bgfx 3D pipeline, including the one this file exercises. Sub-tests (b) and (c) here both assert values that
are the mirror-image of the true FNA-equivalent result, not merely a boundary-symmetric special case.

## Checklist Results

### Behavioral correctness / Logic — same defect as bgfx_alphatest_fog_test.cpp's F1, re-derived for this file's own FogStart/FogEnd values

Using the FNA-equivalent formula independently derived in this batch's `bgfx_alphatest_fog_test.cpp` report
(`factor_correct(z) = saturate((z+FogEnd)/(FogEnd-FogStart))`, `1`=no fog/`0`=full fog, matching this
project's own `74ad3bae` EasyGL fix):

- Sub-test (b): `FogStart=0, FogEnd=1, Z=0.5` →
  `factor_correct(0.5) = saturate((0.5+1)/(1-0)) = saturate(1.5) = 1.0` → **no fog at all** (pure blue), not
  a 50/50 mix. The test instead expects `(128,0,128)` (tolerance ±30, i.e. accepting `R,B∈[98,158]`,
  `G≤30`) — a pure-blue result (`0,0,255`) would fail this assertion outright (`|0-128|=128 > 30` on both R
  and B). `vs_colored3d.sc`'s actual (buggy) formula computes `clamp((1-0.5)/(1-0),0,1)=0.5`, which is what
  produces the asserted `(128,0,128)` — i.e. the test's expected value was derived from the current buggy
  shader's actual output, not from independently re-derived FNA math.
- Sub-test (c): `FogStart=0, FogEnd=0.5, Z=0.9` →
  `factor_correct(0.9) = saturate((0.9+0.5)/(0.5-0)) = saturate(2.8) = 1.0` → **no fog** (pure blue), not
  full fog. The test expects pure red (full fog); `vs_colored3d.sc`'s actual formula gives
  `clamp((0.5-0.9)/(0.5-0),0,1) = clamp(-0.8,0,1) = 0` → full fog, matching what's asserted.
- Sub-test (a) (fog disabled) is unaffected by the formula bug (both formulas force `factor=1`/`v_fogFactor
  =1.0` when `FogEnabled=false`).

### Cross-file consistency

`vs_colored3d.sc` (lines 24-27) contains the byte-for-byte same `(u_fogParams.z - a_position.z)` shape as
`vs_alpha_test3d.sc` (audited in this batch's `bgfx_alphatest_fog_test.cpp` report) and
`vs_lit_textured3d.sc` (this batch's `bgfx_basiceffect_lit_fog_test.cpp`) — confirming the defect is a
single shared root cause (the fog-factor formula shared across all Bgfx 3D vertex shaders), not something
specific to this file's pipeline.

### Maintainability

This file's own header comment states the formula plainly (`fogFactor = clamp((FogEnd - Z) / (FogEnd -
Start), 0, 1)`) without the "matches EasyGL" claim `bgfx_alphatest_fog_test.cpp`'s header makes — so unlike
that sibling file, this one does not contain a now-stale cross-backend-match claim; it just accurately
describes the (buggy) formula actually implemented, without asserting FNA-equivalence.

## Detailed Findings

### F1 — Same as `bgfx_alphatest_fog_test.cpp`'s F1: the shared Bgfx fog formula is the oracle-disproven "naive" shape, and this file's sub-tests (b)/(c) validate the wrong (mirrored) result

- Severity: HIGH
- Confidence: HIGH (see the full derivation and oracle-diff corroboration in this batch's
  `bgfx_alphatest_fog_test.cpp` report; re-derived independently here against this file's own
  `FogStart`/`FogEnd`/`Z` values rather than merely cross-referencing)
- Category: correctness / FNA-parity / cross-backend-consistency
- Location/symbol: `src/CNA/Internal/Backends/Bgfx/shaders/vs_colored3d.sc:24-27`; assertions at
  `bgfx_basiceffect_fog_test.cpp:144-183` (sub-tests b and c)
- Evidence: see derivation above; both (b) and (c) produce `factor_correct=1.0` (no fog) under the true
  FNA-equivalent formula, in both cases the *opposite* of what the test currently asserts and of what the
  shader currently computes.
- Why it matters: any real (asymmetric `FogStart`/`FogEnd`) scene rendered via `BasicEffect` fog on Bgfx
  today would show fog on the wrong side of the depth range relative to real XNA. Neither sub-test here
  would catch a fix that corrected this, since both were tuned to match the current buggy output.
- Suggested future action (not implemented by this audit): apply the same `74ad3bae`-derived formula fix to
  `vs_colored3d.sc` (and its siblings), then recompute this file's (b)/(c) expected values against the
  corrected formula.

## Cross-File Observations

- Identical root-cause defect to `bgfx_alphatest_fog_test.cpp` and `bgfx_basiceffect_lit_fog_test.cpp` (this
  batch) — see the first file's report for the full symbolic derivation and git-history corroboration
  (commit `74ad3bae`, `plans/plan_dx9.md D9-A6` oracle finding).
- Uses `fx.VertexColorEnabled = true;` (line 126, via the `setupBase` lambda) — same bare-public-field API
  observation as `bgfx_basiceffect_combined_test.cpp` (this batch); not re-detailed here to avoid repetition.

## Missing or Weak Tests

- Same gap as `bgfx_alphatest_fog_test.cpp`: no non-identity `World`/`View` case, so even a corrected formula
  would remain unverified against the general (camera-transform-dependent) fog path.

## Positive Findings

- Sub-test (a) (fog disabled) is correct and unaffected by F1.
- The three-case structure (off / partial / full) is methodologically sound in design, even though its
  numeric ground truth for (b)/(c) is currently wrong.
- Correctly isolates fog from lighting (`VertexColorEnabled` path, no texture) using the stride-16
  `colored3d` pipeline specifically.

## Final Assessment

Same production defect as `bgfx_alphatest_fog_test.cpp`, independently re-derived and reconfirmed against
this file's own `FogStart=0`/`FogEnd∈{1,0.5}` values: both non-trivial sub-tests assert the mirror-image of
the true FNA-equivalent fog factor. This is the second of three files in this batch carrying the same
shared-shader defect.
