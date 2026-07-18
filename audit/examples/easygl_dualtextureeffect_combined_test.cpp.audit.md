# Audit: examples/easygl_dualtextureeffect_combined_test.cpp

## Metadata

- Source file: `examples/easygl_dualtextureeffect_combined_test.cpp`
- Audit status: AUDITED
- Subsystem: EasyGL backend integration test (`examples-tests-easygl` shard)
- File type: C++ example/integration test, registered as CTest `EasyGL_DualTextureEffect_Combined`
  (`cmake/Tests/EasyGLTests.cmake:1174-1176`, `cna_test_easygl_dualtextureeffect_combined`)
- Related production code: `DualTextureEffect` full `OnApply()`/`FillGpuDrawParams()` chain,
  `EasyGLGraphicsBackend::EnsureDualTextured3DProgram()`
  (`EasyGLGraphicsBackend.cpp:3009-3070`), `Texture2D::SetData` (multi-texel upload/sampling path).
- XNA/FNA relevance: `DualTextureEffect` full pixel formula (`Texture × 2 × Texture2 × DiffuseColor ×
  Alpha`), FNA reference `DualTextureEffect.fx` `PSDualTexture`.
- Main related tests: capstone for Tasks 383-388 (doubling/alpha/fog fixes); reused verbatim by
  `easygl_dualtextureeffect_golden_test.cpp` (Task 469) for one of its 4 sample points via
  `PixelTestGame::CompareGoldenImage`.

## Purpose

`DualTextureCombinedTest` is explicitly the "capstone" test of this batch: it combines a real 2×2
multi-texel `Texture` (not a flat 1×1 color, unlike most sibling tests), a gray `Texture2` chosen to
approximately cancel the `×2` doubling factor, and a non-trivial `DiffuseColor=(0.6,0.4,0.8)`, then
samples all 4 distinct texels via 4 separate draws with a UV held constant across the whole quad
(avoiding bilinear-filtering ambiguity). Its own header comment states it is designed to prove the
individually-verified fixes from Tasks 383-388 "compose correctly rather than only working in
isolation." Correct placement and CMake registration.

## Executive Verdict

**Healthy.** The math for all 4 sample points checks out against the real shader formula and the
constant-UV-per-quad technique genuinely isolates texel-sampling correctness from filtering
ambiguity. One documented, deliberate scope exclusion (fog) is correctly justified given the other
backends' known limitations, not a coverage gap in this file specifically.

## Checklist Results

### API / XNA / FNA parity
Exercises `DualTextureEffect::setTextureProperty`/`setTexture2Property`/`setDiffuseColorProperty`;
correct XNA-property-style API usage throughout.

### Behavioral correctness
Verified sample "top-left texel" by hand: `kTexels[0]=(200,100,50)`, `texGray=(128,128,128)≈0.502`,
`kDiffuse=(0.6,0.4,0.8)`.
- `base = (200/255,100/255,50/255) = (0.7843,0.3922,0.1961)`; `base.rgb*=2 →
  (1.5686,0.7843,0.3922)` (R channel exceeds 1, will clamp after the final multiply only if the
  running product exceeds 1 — GL fragment output clamps the *final* color to `[0,1]`, not
  intermediate `base`, so `base` legitimately holds values `>1` mid-shader).
- `FragColor = base * texGray(0.502) * diffuse(0.6,0.4,0.8)`:
  - R: `1.5686 * 0.502 * 0.6 = 0.4725` → `120.5/255` ≈ **120** (comment's expected `120`, matches).
  - G: `0.7843 * 0.502 * 0.4 = 0.1575` → `40.2/255` ≈ **40** (matches).
  - B: `0.3922 * 0.502 * 0.8 = 0.1575` → `40.2/255` ≈ **40**, but the file's own table lists expected
    `(120,40,40)` for top-left — **matches** (comment line 75).
  All four sample expectations were independently recomputed by hand from the real shader formula:
  top-left `kTexels[0]=(200,100,50)→(120,40,40)` ✓, top-right `kTexels[1]=(50,200,100)→(30,80,80)` ✓,
  bottom-left `kTexels[2]=(100,50,200)→(60,20,161)` ✓ (the odd `161` — not `160` — falls out of the
  exact floating-point rounding: `0.629911570×255=160.628→161`, confirming the table wasn't just
  hand-rounded loosely), bottom-right `kTexels[3]=(150,150,150)→(90,60,120)` ✓. The formula and the
  full expected-value table are internally consistent and match the real shader exactly, to within
  8-bit rounding.
- Tolerance is `±8` (`matches()`, line 106-111) — meaningfully tighter than the `±20`/`±40` used by
  most other files in this batch, appropriate given these are now derived, non-round expected values
  rather than saturated 0/255 endpoints, and still wide enough to absorb the ~0.4% doubling/gray
  cancellation residual noted in the comment.

### Logic
The retry loop (`for (int i = 0; i < 20; ++i) { ...; if (nonzero) break; }`, lines 149-161) to "skip
blank/black frames" is a defensive pattern against a transient first-frame black readback (windowing/
swap-chain warm-up) — reasonable given `Draw()` runs before the first present may have completed on
some platforms, though it means a genuine rendering bug that produces an all-black *first* frame but
a correct *second* frame would be silently accepted. This same retry pattern recurs in
`easygl_dualtextureeffect_fog_test.cpp`, so it's a project-wide convention, not unique risk here.

### Memory/resource lifetime
`tex`/`texGray` are declared once outside the sample loop, each `DualTextureEffect fx` is
re-constructed fresh per sample — correct, no lifetime issue since both textures outlive every `fx`
that references them by raw pointer.

### C++ correctness
Correctly includes `<cstdlib>` for `std::abs`. `static bool closeTo`/`matches` are appropriately
`static` member helpers.

### Performance
20 iterations × up to several hundred `Clear`+`Draw`+`GetBackBufferData` round-trips per one CTest
invocation is a heavier-than-typical single-frame test but bounded and acceptable for a CI
integration test, not a hot path.

### Thread safety / Portability
N/A / no platform-specific code.

### Architecture
Correct XNA-only public API usage.

### Maintainability
Well-commented, explains its own scope boundaries clearly (fog explicitly excluded with a reasoned
justification, lines 27-29).

### Robustness
N/A (test file).

### Testing
Strong: this is the first `DualTextureEffect` test in the project (per its own comment) to use a real
multi-texel texture, proving per-vertex UV correctly reaches the intended texel through the full
doubling+two-texture+diffuse pipeline — a meaningfully different failure mode than the earlier 1×1
solid-color tests in this batch could ever catch (e.g. a UV-coordinate transposition bug that a 1×1
texture is blind to).

### Cross-file consistency
`easygl_dualtextureeffect_golden_test.cpp` (Task 469) explicitly reuses this file's exact scene setup
(identical `kTexels`, `kGrayHalf`, `kDiffuse`) for one golden-image sample point, and its own comment
cross-references this file's derivation — verified the two files' expected value for "top-left"
match exactly (`(120,40,40,255)` in both).

## Detailed Findings

No HIGH/MEDIUM findings.

### F1 — Retry-until-nonblack loop could mask a "first frame black" regression

- Severity: LOW
- Confidence: MEDIUM (pattern-based; not traced to a concrete current failure)
- Category: robustness / test-coverage
- Location/symbol: `Draw()`, lines 149-161
- Evidence: the loop breaks on the first non-all-black readback within 20 attempts; there is no
  logged warning or distinction in output between "passed on iteration 1" and "passed on iteration
  17."
- Why it matters: if a future regression makes only the *first* draw of a frame come back blank
  (e.g. a swap-chain double-buffering bug), this test would silently retry past it and still report
  PASS, hiding a real defect from CI.
- Suggested action (not implemented by this audit): log which iteration succeeded, or reduce the
  retry count with an explicit skip/warn if more than 1-2 retries were needed.

## Cross-File Observations

- See `easygl_dualtextureeffect_golden_test.cpp`'s audit report for the specific cross-check that
  this file's derived "top-left" expected value is reused there and matches.

## Missing or Weak Tests

Fog is deliberately and correctly excluded here (documented, cross-backend reasoning) — no gap.

## Positive Findings

- The multi-texel-texture technique is a genuine step up in test rigor from the batch's other
  1×1-texture tests, and its constant-UV-per-quad sampling technique is a well-reasoned way to
  eliminate bilinear-filtering ambiguity as a confound.
- Clear, well-justified scope boundary (fog exclusion) rather than a silent gap.

## Final Assessment

A well-constructed capstone integration test whose expected-value derivations check out against the
real shader math to within its stated tolerance; the only concern is a generic, project-wide
retry-loop pattern that could theoretically mask a first-frame-only regression, not a defect specific
to this file's own logic.
