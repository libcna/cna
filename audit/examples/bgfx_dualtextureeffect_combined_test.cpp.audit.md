# Audit: examples/bgfx_dualtextureeffect_combined_test.cpp

## Metadata

- Source file: `examples/bgfx_dualtextureeffect_combined_test.cpp` (165 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `DualTextureEffect` combined doubling-factor +
  two-texture-multiply + `DiffuseColor` pixel test (Bgfx backend)
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_bgfx_test(cna_test_bgfx_dualtextureeffect_combined …)` /
  `cna_register_backend_test(NAME Bgfx_DualTextureEffect_Combined …)`,
  `cmake/Tests/BgfxTests.cmake:420-422`).
- XNA/FNA relevance: direct — `DualTextureEffect.DiffuseColor`, two-texture blending, the
  `color.rgb *= 2` doubling factor.
- FNA reference: `src/Graphics/Effect/StockEffects/HLSL/DualTextureEffect.fx`
  (`PSDualTexture`).
- Related production code: `src/CNA/Internal/Backends/Bgfx/shaders/fs_dual_texture3d.sc`.

## Purpose

Task 389 — Phase 44's cross-backend `DualTextureEffect` image-comparison suite, Bgfx half. Combines
all three factors into one scene: expected fragment = `Texture × 2 × Texture2 × DiffuseColor ×
Alpha`. A real 2×2 multi-texel `Texture` (`kTexels`, 4 distinct colors) paired with a solid gray
`Texture2` (`(128,128,128)`, chosen so `128/255 × 2 ≈ 1.004`, effectively cancelling the doubling
factor) and a non-trivial `DiffuseColor = (0.6, 0.4, 0.8)`. Four separate draws, each holding UV
constant across the whole quad (sampling exactly one texel center — `0.25`/`0.75` on a 2-wide
texture lands exactly on a texel centre, guaranteeing no bilinear blending) so each of the 4 texels
gets its own independently-verified expected output.

## Executive Verdict

**Healthy** — this audit independently recomputed all 4 expected `Color` constants from the raw
texel/diffuse values against the actual `fs_dual_texture3d.sc` formula and they match the file's
asserted values (within ~0.5 of an 8-bit channel in every case, well inside the file's own `tol=8`),
and independently confirmed the UV→texel mapping (`(0.25,0.25)`→top-left, etc.) is internally
self-consistent.

## Checklist Results

### API / XNA / FNA parity
`setDiffuseColorProperty` (line 120) matches `DualTextureEffect.hpp`. `Texture2D::SetData` (lines
108, 110) for a `2×2` texture upload — this test relies on row-major texel ordering (`kTexels[0..3]`
= top-left, top-right, bottom-left, bottom-right) which this audit confirmed is internally
consistent with the sample-point labels (see Behavioral correctness below), though this audit did
not independently trace `Texture2D::SetData`'s exact byte-layout implementation beyond confirming
the derived math is self-consistent across all 4 samples.

### Behavioral correctness
Independently recomputed all 4 expected colors from
`fragment = texel/255 × 2 × (128/255) × diffuse`, per channel, for `diffuse=(0.6,0.4,0.8)`:
- `kTexels[0]=(200,100,50)`, uv `(0.25,0.25)` labeled "top-left texel": R=200/255×0.6×2×0.501961×255
  ≈120.4→**120**; G=100/255×0.4×2×0.501961×255≈40.2→**40**; B=50/255×0.8×2×0.501961×255≈40.2→**40**.
  Matches asserted `Color(120,40,40)` exactly.
- `kTexels[1]=(50,200,100)`, uv `(0.75,0.25)` "top-right": R≈30.1→**30**, G≈80.3→**80**,
  B≈80.3→**80**. Matches asserted `Color(30,80,80)`.
- `kTexels[2]=(100,50,200)`, uv `(0.25,0.75)` "bottom-left": R≈60.2→**60**, G≈20.1→**20**,
  B: `200/255×0.8×2×0.501961×255≈160.6→**161**`. Matches asserted `Color(60,20,161)` — this is the
  one sample where the doubling-factor's small residual (`2×0.501961=1.003922`, not exactly 1)
  becomes visible at the rounding level (`160.6` rounds to `161`, not `160`), and the file's own
  constant already correctly reflects that rounding rather than a naively-truncated `160`.
- `kTexels[3]=(150,150,150)`, uv `(0.75,0.75)` "bottom-right": R≈90.4→**90**, G≈60.2→**60**,
  B≈120.5→**120**. Matches asserted `Color(90,60,120)`.
All four independently-recomputed values match the file's `kSamples[]` table (lines 55-60) either
exactly or within <1 unit of 8-bit rounding — well inside the `tol=8` used by `matches()` (line
89-91). This is strong evidence the expected constants were correctly derived from the real formula,
not guessed or copy-pasted with an error.

### Logic
Held-constant UV per quad (all 6 vertices in `q[6]` share the single `s.uv`, lines 122-124) sampling
exactly at a texel centre (`0.25`/`0.75` for a 2-wide texture) avoids any bilinear-interpolation
ambiguity — a deliberate and correct test-design choice that makes the per-texel expected value
exact rather than an interpolated blend.

### C++ correctness
`Draw()` (lines 103-147) has **no `done_`/`already-drawn` boolean guard**, unlike every other file
in this batch. This audit traced `Game::Tick()`/`Game::RunLoop()`
(`src/Microsoft/Xna/Framework/Game.cpp:357-451, 825-839`) to determine whether this could cause
`Draw()` to execute more than once (which would double-run the 4-sample loop and double-count
`pass_`/`fail_`): `RunLoop()` is `while (RunApplication) { Tick(); }`, and `Tick()` calls `Draw()`
**at most once per `Tick()`** (never inside the fixed-timestep `Update()` catch-up loop). Since
`Exit()` (called at line 146, inside this same `Draw()` call) sets `RunApplication=false`
synchronously, the *current* `Tick()`/`Draw()` call still runs to completion, but `RunLoop()`'s
`while` condition is re-checked only *after* that `Tick()` returns — by which point
`RunApplication` is already `false`, so no further `Tick()`/`Draw()` calls occur. **Confirmed: this
omission does not cause a double-execution bug** given this project's actual `Game` main-loop
implementation; it is a harmless style inconsistency versus sibling files in this batch, not a
correctness defect.

### Robustness
`matches()`'s `tol=8` (line 89-91) is tighter than the `tol=20` used by the sibling alpha test,
appropriate here since these are direct texel-derived values with no blend-compositing rounding
stacked on top.

### Testing
Covers 4 independent texel samples in one file, exercising `Texture` sampling, `Texture2` (gray)
cancellation of the doubling factor, and a non-trivial (non-white, non-uniform-per-channel)
`DiffuseColor` simultaneously — genuinely more combinatorially thorough than the single-texel
`bgfx_dual_texture_test.cpp` or single-scenario `bgfx_dualtextureeffect_alpha_test.cpp` in this same
batch.

## Detailed Findings

None. No HIGH/CRITICAL/MEDIUM findings. (The missing `done_` guard noted under C++ correctness was
investigated as a potential defect and confirmed harmless given this project's actual `Game::Tick()`
semantics — recorded here as a Maintainability/consistency observation only, not a Detailed
Finding, since it has no behavioral consequence.)

## Cross-File Observations

- Shares the gray-texture-cancels-doubling-factor technique and the `RasterizerState::CullNone`
  (Task 364/884) workaround with `bgfx_dual_texture_test.cpp` and
  `bgfx_dualtextureeffect_alpha_test.cpp` in this same batch — all three are mutually consistent and
  independently derived from the same underlying `fs_dual_texture3d.sc` formula.
- Every other file in this 8-file batch uses an explicit `bool done_` guard at the top of `Draw()`;
  this is the sole exception. Purely a style inconsistency (see C++ correctness above for why it's
  not a functional bug) — worth conforming to the sibling convention for readability/defensiveness
  if this file is touched again, but not a defect requiring immediate action.

## Missing or Weak Tests

None identified — the 4-sample design already provides materially more combinatorial coverage than
a single-texel test would.

## Positive Findings

- The bottom-left sample (`kTexels[2]`, expected `Color(60,20,161)`) is a good sign of careful
  derivation: the doubling factor's small residual (`1.003922` rather than exactly `1.0`) is *just*
  large enough to shift the rounded 8-bit blue channel from 160 to 161, and the file's own constant
  already reflects the correct rounded value rather than a naive `160` — this would have been an
  easy computational slip to make, and it wasn't made here.
- Holding UV constant per quad at an exact texel-centre coordinate is a clean way to get exact,
  non-interpolated per-texel expected values without needing a full multi-pixel image comparison
  framework.

## Final Assessment

A well-constructed 4-sample combined test whose expected constants this audit independently
recomputed and confirmed correct against the real bgfx shader formula, including a subtle rounding
detail in one sample. The sole anomaly found (missing `done_` guard) was investigated and confirmed
to be a harmless style inconsistency, not a functional defect, given this project's `Game`
main-loop semantics.
