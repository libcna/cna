# Audit: examples/bgfx_alphatest_null_texture_test.cpp

## Metadata

- Source file: `examples/bgfx_alphatest_null_texture_test.cpp` (159 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `AlphaTestEffect.Texture=null` fallback regression test
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_bgfx_test(cna_test_bgfx_alphatest_null_texture …)` /
  `cna_register_backend_test(NAME Bgfx_AlphaTest_NullTexture …)`, `cmake/Tests/BgfxTests.cmake:383-386`).
- XNA/FNA relevance: direct — `AlphaTestEffect.Texture` accepting `null` (FNA's `Texture` property has no
  null-guard; a null texture simply means "nothing bound", and both FNA/D3D9 and CNA's other backends treat
  an unbound texture sampler as opaque white).
- FNA reference: `AlphaTestEffect.cs` (`Texture` property, no null validation).
- Related production code: `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp` (the
  `tex.textureHandle` valid → `bgfx::setTexture(...)` / else → `defaultWhiteTexture3D_` fallback pattern,
  now present at ~20+ call sites, e.g. lines 2251-2258, 2422-2429, 2455-2462).

## Purpose

Regression test for a real, previously-fixed bug (`Task 379`): Bgfx's texture-binding code used to call
`bgfx::setTexture()` only when a real texture was bound, with no `else` branch at all — meaning a
`Texture=null` draw silently kept whatever texture handle the *previous* draw call had bound to that slot,
rather than falling back to a neutral white texture the way EasyGL/Vulkan already did. Fixed by adding
`defaultWhiteTexture3D_` and an `else` branch at (per the file's header) all 7 of Bgfx's texture-binding call
sites at the time.

## Executive Verdict

**Healthy** — the math is correctly derived and independently re-confirmed, and the specific fix it
regression-tests (`defaultWhiteTexture3D_` fallback) is confirmed still present and pervasive in the current
`BgfxGraphicsBackend.cpp` (not just at the original 7 sites — the same `if (tex) ... else
defaultWhiteTexture3D_` idiom now appears at every texture-binding call site this audit found, including
newer PBR-pipeline additions).

## Checklist Results

### Behavioral correctness / Logic

`renderWith()` (lines 88-109) draws with `DiffuseColor=(0.6,0.4,0.8)`, `Texture=&tex` (a 1×1 texture of
`(200,100,50,255)`) for the first case and `Texture=nullptr` for the second, both without touching
`AlphaFunction`/`ReferenceAlpha` (left at FNA defaults `Greater`/`0`, always passing since combined alpha
stays 1.0 throughout). I independently recomputed both expected pixels:

- With texture: `TextureColor × DiffuseColor = (200×0.6, 100×0.4, 50×0.8) = (120, 40, 40)` — matches
  `kExpectedWithTexture` exactly.
- Null texture (fallback to opaque white `(1,1,1)`): `(1×0.6, 1×0.4, 1×0.8) × 255 = (153, 102, 204)` —
  matches `kExpectedNullTexture` exactly.

Both figures were independently recomputed by this audit, not merely re-stated from the file's own comments.

### Robustness

The third assertion (line 135-137, `!matches(nullTexGot, kExpectedWithTexture)`) is a deliberately distinct
check from the second: it isolates "does null fall back to a *specific correct* white value" from "does null
at least *not* leak the previous draw's stale texture" — two independently-failing hypotheses a naive fix
could get wrong in different ways (e.g. a fallback to black, or to some other stale default, would still
pass the "not the previous texture" check while failing the "is actually white" one). This is good test
design, mirroring the same two-pronged isolation technique seen in this shard's other regression-style tests
(e.g. `bgfx_alphatest_vertexcolor_test.cpp`'s combined-alpha isolation).

### Cross-file consistency

Verified the described fix is real and current, not just narrated: `BgfxGraphicsBackend.cpp` declares
`defaultWhiteTexture3D_` (created at line 1180, destroyed at line 1296) and the `if (bgfx::isValid(tex...))
{ bgfx::setTexture(...) } else { bgfx::setTexture(..., defaultWhiteTexture3D_, ...) }` pattern recurs at
every texture-binding call site this audit grepped (well beyond the original "7" the header comment cites —
newer PBR call sites for normal/metallic-roughness/emissive/occlusion maps at lines 2195-2244 reuse the same
`defaultWhiteTexture3D_`/`defaultFlatNormalTexture3D_` idiom). The regression this test guards against
remains fixed in the current tree.

### Testing

Uses the same retry-loop readback pattern (`renderWith()` lines 96-107) as most of this batch's other files
— present and correctly applied here (contrast with `bgfx_alphatest_comparefunction_sweep_test.cpp`'s F1 in
this same batch, which omits it).

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings. This file is a clean, correctly-derived regression test with no material
issues found.

## Cross-File Observations

- Shares the `RasterizerState::CullNone` requirement and its Task 364/884 attribution with every other file
  in this batch — accurately described, not re-litigated here.
- The `defaultWhiteTexture3D_` fallback this test guards is architecturally the same fix pattern EasyGL and
  Vulkan already had (per the header comment); this audit did not independently re-verify EasyGL/Vulkan's
  own equivalents (out of this batch's scope) but has no reason to doubt the claim given the parallel
  structure observed in Bgfx.

## Missing or Weak Tests

- No case exercises `Texture=null` combined with `VertexColorEnabled=true` (i.e. does the null-texture
  fallback still correctly gate through the stride-24 `alphaTestColoredTextured3DProgram_` path, or only the
  plain stride-20 `alphaTest3DProgram_` path exercised here). Not a defect, just an adjacent untested
  combination worth flagging per the checklist's parity/combination-coverage guidance.

## Positive Findings

- Both expected pixel values were independently recomputed and confirmed exactly correct.
- The fix it regression-tests was independently confirmed still present and, in fact, now more widely
  applied than the original fix's scope (PBR texture slots reuse the identical idiom).
- Good test design distinguishing "correct fallback value" from "no stale-state leak" as two separate,
  independently-falsifiable assertions.

## Final Assessment

A solid, correctly-derived regression test for a real, previously-fixed texture-binding bug. No issues
found; the underlying fix was independently confirmed still in place and current.
