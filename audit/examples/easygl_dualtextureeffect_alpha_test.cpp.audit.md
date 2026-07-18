# Audit: examples/easygl_dualtextureeffect_alpha_test.cpp

## Metadata

- Source file: `examples/easygl_dualtextureeffect_alpha_test.cpp`
- Audit status: AUDITED
- Subsystem: EasyGL backend integration test (`examples-tests-easygl` shard)
- File type: C++ example/integration test, registered as CTest `EasyGL_DualTextureEffect_Alpha`
  (`cmake/Tests/EasyGLTests.cmake:1150-1152`, `cna_test_easygl_dualtextureeffect_alpha`)
- Related production code: `DualTextureEffect::OnApply()`/`getAlphaProperty`/`setAlphaProperty`
  (`src/Microsoft/Xna/Framework/Graphics/DualTextureEffect.cpp:113-118,224-233`),
  `BlendState::AlphaBlend` (premultiplied-alpha blend factors), EasyGL
  `EnsureDualTextured3DProgram()` fragment shader.
- XNA/FNA relevance: `DualTextureEffect.Alpha` and FNA's `OnApply()` diffuse-color premultiplication
  formula (`Graphics/Effect/StockEffects/DualTextureEffect.cs:291-319`).
- Main related tests: `tests/Microsoft/Xna/Framework/Graphics/DualTextureEffectTests.cpp` (referenced
  by this file's own header comment as already covering the CPU-side `FillGpuDrawParams()` formula
  directly).

## Purpose

`DualTextureAlphaTest` verifies that `DualTextureEffect::Alpha` produces genuinely
**premultiplied-alpha** output end-to-end on real GPU hardware (through `BlendState::AlphaBlend`),
not just in the CPU-side parameter-computation formula. It draws a single quad with
`DiffuseColor=red(1,0,0)`, `Alpha=0.5`, over an opaque blue background, and checks the blended result
distinguishes correct premultiplication `(128,0,128)` from a hypothetical non-premultiplied bug
`(255,0,128)` — a 127-unit R-channel separation. Correct placement/naming per this project's
integration-test convention.

## Executive Verdict

**Healthy.** The test's chosen numbers are deliberately engineered to isolate exactly one variable
(alpha premultiplication) from two others (the doubling factor and diffuse-color scaling) that other
sibling tests already cover separately, and the expected/rejected values are independently derivable
from both `DualTextureEffect::OnApply()`'s actual formula and `BlendState::AlphaBlend`'s blend
equation. No defect found.

## Checklist Results

### API / XNA / FNA parity
Exercises `DualTextureEffect::setTextureProperty`, `setTexture2Property`, `setDiffuseColorProperty`,
`setAlphaProperty`, `Apply()` — all correct XNA-property-style names. `Alpha` matches FNA's
`DualTextureEffect.Alpha` (`DualTextureEffect.cs:117-126`).

### Behavioral correctness
Verified the full chain by hand:
1. `DualTextureEffect::OnApply()` (`DualTextureEffect.cpp:225-234`): with `diffuseColor_=(1,0,0)`,
   `alpha_=0.5`, sets `diffuseColorParam_ = Vector4{1*0.5, 0*0.5, 0*0.5, 0.5} = (0.5,0,0,0.5)` —
   exactly matching FNA's `new Vector4(diffuseColor * alpha, alpha)`.
2. Fragment shader (`EnsureDualTextured3DProgram`, `EasyGLGraphicsBackend.cpp:3050-3052`):
   `base=texWhite=(1,1,1,1)`; `base.rgb*=2 → (2,2,2,1)`; `FragColor = base * texGray(≈0.502) *
   diffuse(0.5,0,0,0.5) = (2*0.502*0.5, 0, 0, 1*1*0.5) = (≈0.502, 0, 0, 0.5)`.
3. `BlendState::AlphaBlend` (`One`, `InverseSourceAlpha`): `result = src.rgb*1 + dst.rgb*(1-0.5) =
   (0.502,0,0) + (0,0,1)*0.5 = (0.502,0,0.5)` ≈ `(128,0,128)` in 8-bit — matches the test's expected
   value and its documented rejection value `(255,0,128)` (what a non-premultiplied
   `Vector4(diffuseColor, alpha)` would produce: `src=(1,0,0,0.5)→(1,0,0)+(0,0,0.5)=(1,0,0.5)`).
- The comment's own claim that `texGray` "exactly cancels" the doubling factor is only approximately
  true (`2×128/255≈1.00392`, not exactly `1`) — but the ±20 tolerance in `colourMatch` (line 53-58)
  comfortably absorbs the ~1-unit-of-255 discrepancy, so this does not weaken the test.

### Logic
`colourMatch` (line 51-58) uses the same `closeTo`/tolerance-20 pattern as the rest of this batch,
appropriately loose for a blended (not flat) result while still tight enough to reject the 127-unit
non-premultiplied hypothesis with large margin.

### Memory/resource lifetime
`texWhite`/`texGray` are stack-local `Texture2D`s referenced only by raw, non-owning pointer for the
duration of a single `Draw()` call — correct, matches this batch's established pattern.

### C++ correctness
Includes `<cstdio>`, `<cstdlib>`, `<memory>` — correctly includes `<cstdlib>` for `std::abs`, unlike
the sibling `easygl_dualtexture_test.cpp` (see that file's F2 finding).

### Performance / Thread safety
N/A — single-frame test.

### Architecture
Correct XNA-only API surface usage; no direct backend symbols.

### Maintainability
Comment block (lines 2-26) is unusually thorough and directly derives the expected/rejected pixel
values from first principles — a strong example of self-documenting test rationale.

### Portability
No platform-specific code.

### Robustness
N/A (test file).

### Testing
This file *is* a test for `DualTextureEffect::Alpha`. It correctly isolates alpha premultiplication
as its sole variable via the gray-cancels-doubling technique. It does not test `Alpha` values other
than `0.5`, nor `Alpha=0`/`Alpha=1` edge cases (though `Alpha=1` is implicitly exercised as the
default in every other sibling test in this batch) — a minor, acceptable scope choice given the
suite-level coverage.

### Cross-file consistency
Consistent with `DualTextureEffect.cpp`'s premultiplication formula and with FNA's
`DualTextureEffect.cs` `OnApply()`. Consistent with the doubling factor established in
`easygl_dualtextureeffect_doubling_test.cpp` (same gray-cancellation technique, same shader).

## Detailed Findings

No HIGH/MEDIUM findings. One minor observation:

### F1 — "exactly cancels" claim in comment is an approximation, not flagged as such

- Severity: LOW
- Confidence: HIGH
- Category: maintainability (documentation precision)
- Location/symbol: header comment lines 12-15
- Evidence: `2 × 128/255 = 1.00392...`, not `1.0` exactly; the comment says the gray overlay "exactly
  cancels" the doubling factor.
- Why it matters: purely cosmetic — the ±20 tolerance already absorbs the ~1/255 discrepancy, and the
  same approximation is used consistently (and more precisely worded, e.g. "not exactly 1 -- absorbed
  by tolerance") in the sibling `easygl_dualtextureeffect_combined_test.cpp` and `_fog_test.cpp` files.
  This file alone omits that caveat.
- Suggested action (not implemented by this audit): align the wording with the more precise sibling
  comments if this file is touched again.

## Cross-File Observations

- This test, `easygl_dualtextureeffect_doubling_test.cpp`, `_combined_test.cpp`, and `_fog_test.cpp`
  all independently use the same "gray texture cancels the ×2 doubling factor" technique to isolate
  their own variable of interest — a consistent, well-reasoned methodology across the batch, not
  reinvented ad hoc per file.

## Missing or Weak Tests

None significant — `Alpha=0.5` combined with `AlphaBlend` gives strong, real discriminating power for
the premultiplication behavior this file targets.

## Positive Findings

- Best-documented file in this batch: the header comment derives both the correct and the
  hypothetical-buggy expected pixel values algebraically before the reader even reaches the code.
- Correctly includes `<cstdlib>` (contrast with the sibling `easygl_dualtexture_test.cpp`, F2 there).

## Final Assessment

A well-targeted, correctly-implemented test that isolates `DualTextureEffect.Alpha`'s
premultiplication behavior with real discriminating power against a plausible regression, verified
end-to-end against both the CPU formula and the GPU blend equation.
