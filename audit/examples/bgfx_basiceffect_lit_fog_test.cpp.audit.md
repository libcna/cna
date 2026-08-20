# Audit: examples/bgfx_basiceffect_lit_fog_test.cpp

## Metadata

- Source file: `examples/bgfx_basiceffect_lit_fog_test.cpp` (164 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `BasicEffect` linear-fog pixel test on the `lit_textured3d`
  (stride-32 `VertexPositionNormalTexture`) pipeline
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_bgfx_test(cna_test_bgfx_basiceffect_lit_fog …)` /
  `cna_register_backend_test(NAME Bgfx_BasicEffect_LitFog …)`, `cmake/Tests/BgfxTests.cmake:341-344`).
- XNA/FNA relevance: direct — `BasicEffect.FogEnabled`/`FogColor`/`FogStart`/`FogEnd` on the
  `LightingEnabled=false` + textured path that routes through a *different* shader pair
  (`vs`/`fs_lit_textured3d.sc`) than the plain-color `colored3d` pipeline `bgfx_basiceffect_fog_test.cpp`
  exercises.
- FNA reference: `src/Graphics/Effect/StockEffects/EffectHelpers.cs` (`SetFogVector`),
  `HLSL/Common.fxh` (`ComputeFogFactor`/`ApplyFog`).
- Related production code: `src/CNA/Internal/Backends/Bgfx/shaders/vs_lit_textured3d.sc` (fog-factor
  computation, lines 29-31), `fs_lit_textured3d.sc` (fog blend, line 53).

## Purpose

Companion test to `bgfx_basiceffect_fog_test.cpp` (this batch): proves the identical fog code, separately
added to `vs`/`fs_lit_textured3d.sc` (a completely distinct shader pair), also works — since any
`BasicEffect` draw using `VertexPositionNormalTexture` routes through this pipeline instead of `colored3d`.
Same three-case structure (off / 50% at `FogStart=0,FogEnd=1,Z=0.5` / full at `FogStart=0,FogEnd=0.5,Z=0.9`)
and, per its own header, "same expected values as `examples/vulkan_basiceffect_fog_test.cpp`."

## Executive Verdict

**Significant correctness risk** — the third occurrence in this batch of the same shared-shader fog defect
(see `bgfx_alphatest_fog_test.cpp`'s F1 for the full derivation). `vs_lit_textured3d.sc` implements the
byte-for-byte identical `(FogEnd-z)/(FogEnd-FogStart)` formula this repository's own oracle-diff testing
(commit `74ad3bae`) already disproved, and this file's sub-tests (b)/(c) use the exact same `FogStart=0`/
`FogEnd∈{1,0.5}`/`Z` values as `bgfx_basiceffect_fog_test.cpp`, so the same corrected-vs-actual mismatch
applies numerically unchanged.

## Checklist Results

### Behavioral correctness / Logic

Re-derived using the same FNA-equivalent formula (`factor_correct(z)=saturate((z+FogEnd)/(FogEnd-FogStart))`,
`1`=no fog) established in this batch's `bgfx_alphatest_fog_test.cpp` report:

- (b) `FogStart=0, FogEnd=1, Z=0.5`: `factor_correct(0.5)=saturate(1.5)=1.0` → pure blue expected under true
  FNA math; this file instead asserts `(128,0,128)` (a 50/50 mix, tolerance ±30 via `matches(..., 30)`,
  line 74-79) — matching what the *current buggy* `vs_lit_textured3d.sc` formula
  (`clamp((1-0.5)/(1-0),0,1)=0.5`) actually produces.
- (c) `FogStart=0, FogEnd=0.5, Z=0.9`: `factor_correct(0.9)=saturate(2.8)=1.0` → pure blue (no fog) expected
  under true FNA math; this file asserts pure red (full fog) — matching the buggy formula's
  `clamp(-0.8,0,1)=0` output, again the mirror image of the correct result.
- (a) (fog disabled) is unaffected, as with every other file in this defect family.

These are numerically identical derivations to `bgfx_basiceffect_fog_test.cpp`'s (b)/(c) (same
`FogStart`/`FogEnd`/`Z` triples), independently re-confirmed here against this file's own shader
(`vs_lit_textured3d.sc`) rather than assumed by similarity alone.

### Cross-file consistency

Confirmed `vs_lit_textured3d.sc:29-31` contains the identical `(u_fogParams.z - a_position.z)` formula
(byte-for-byte matching `vs_colored3d.sc` and `vs_alpha_test3d.sc`, both audited elsewhere in this batch) —
this is a single shared root cause across all three pipelines this batch touches, confirming the defect is
systemic to Bgfx's fog implementation as a whole (traced to the shared `Task 888`/`899` commits,
2026-07-07), not to any one shader file.

### API / XNA / FNA parity

Correctly uses `setLightingEnabledProperty(false)` and `setTextureEnabledProperty(true)` (lines 88-90) — the
proper get/set-property accessors, not the bare-field pattern `BasicEffect.VertexColorEnabled` has (noted in
`bgfx_basiceffect_combined_test.cpp`'s report); this file doesn't touch `VertexColorEnabled` at all, so that
observation doesn't apply here.

## Detailed Findings

### F1 — Same shared Bgfx fog-formula defect as `bgfx_alphatest_fog_test.cpp` and `bgfx_basiceffect_fog_test.cpp`, on the `lit_textured3d` pipeline

- Severity: HIGH
- Confidence: HIGH (full derivation and oracle-diff corroboration established in this batch's
  `bgfx_alphatest_fog_test.cpp` report; independently re-verified here against this file's own
  `vs_lit_textured3d.sc` shader and its `(b)`/`(c)` sub-test values)
- Category: correctness / FNA-parity / cross-backend-consistency
- Location/symbol: `src/CNA/Internal/Backends/Bgfx/shaders/vs_lit_textured3d.sc:29-31`; assertions at
  `bgfx_basiceffect_lit_fog_test.cpp:135-142`
- Evidence: see derivation above; both non-trivial sub-tests compute the mirror-image of the true
  FNA-equivalent fog factor.
- Why it matters: same as the other two occurrences — any `BasicEffect` draw using
  `VertexPositionNormalTexture` (the common case for lit/textured 3D geometry) with fog enabled on Bgfx
  renders fog reversed relative to real XNA for non-symmetric `FogStart`/`FogEnd` configurations.
- Suggested future action (not implemented by this audit): fix `vs_lit_textured3d.sc` alongside its two
  siblings in the same pass, then recompute all three files' (b)/(c) values together (they currently share
  identical numeric expectations, so a single corrected-value derivation covers all three).

## Cross-File Observations

- Third and final occurrence, in this batch, of the fog-formula defect first fully derived in
  `bgfx_alphatest_fog_test.cpp`'s report; see that file for the complete symbolic proof and git-history
  corroboration (commit `74ad3bae` / Task 1111, `plans/plan_dx9.md D9-A6`).
- The header's claim of "same expected values as `examples/vulkan_basiceffect_fog_test.cpp`" is consistent
  with Vulkan's `colored3d.vert.glsl` sharing the identical formula (confirmed in this batch's
  `bgfx_alphatest_fog_test.cpp` cross-file check) — i.e. Bgfx and Vulkan are mutually consistent with each
  other, just both diverging from true FNA/EasyGL-corrected behavior.

## Missing or Weak Tests

- Same gap as its two siblings: no non-identity `World`/`View` coverage.

## Positive Findings

- Correctly identifies and exercises a genuinely distinct shader pair (`lit_textured3d` vs. `colored3d`) —
  this is not a redundant duplicate of `bgfx_basiceffect_fog_test.cpp`; it provides real, additional
  pipeline coverage (confirmed by inspecting `vs_lit_textured3d.sc` as a materially different file from
  `vs_colored3d.sc`, sharing only the buggy fog subroutine, not the whole shader).
- Correctly uses the proper `getXProperty()`/`setXProperty()` accessors throughout, unlike the sibling
  combined/fog tests that touch `VertexColorEnabled`.

## Final Assessment

Same root-cause defect as its two siblings in this batch, independently re-derived and reconfirmed against
this file's own shader and sub-test values. The test itself is well-designed and provides genuine additional
pipeline coverage; its numeric ground truth for the two fog-active sub-cases needs to be corrected once the
underlying shader formula is fixed.
