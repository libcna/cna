# Audit: examples/bgfx_basiceffect_emissive_test.cpp

## Metadata

- Source file: `examples/bgfx_basiceffect_emissive_test.cpp` (155 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `BasicEffect.DiffuseColor + EmissiveColor` (no lighting) pixel
  test
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_bgfx_test(cna_test_bgfx_basiceffect_emissive …)` /
  `cna_register_backend_test(NAME Bgfx_BasicEffect_Emissive …)`, `cmake/Tests/BgfxTests.cmake:365-368`).
- XNA/FNA relevance: direct — `BasicEffect.EmissiveColor`/`DiffuseColor` with `LightingEnabled=false`
  (BasicEffect's own default).
- FNA reference: `src/Graphics/Effect/StockEffects/EffectHelpers.cs`
  (`SetMaterialColor`'s lighting-disabled branch).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/BasicEffect.cpp` (`FillGpuDrawParams()`
  line 71).

## Purpose

Regression test for a real, previously-fixed bug (Task 369): an `EmissiveColor`-dropped defect in the
shared `BasicEffect::FillGpuDrawParams()` common C++ code (not backend-specific — a single fix covers
EasyGL/Vulkan/Bgfx simultaneously). Proves the pixel equals `TextureColor × (DiffuseColor + EmissiveColor)`
when lighting is disabled, and — via two deliberately-negative assertions — that neither color is silently
ignored.

## Executive Verdict

**Healthy** — the expected value and both negative-control values were independently recomputed and are
exactly correct; the test design (positive value + 2 distinguishing negative controls) is a genuinely
rigorous way to prove neither input is dropped.

## Checklist Results

### API / XNA / FNA parity

Same `FillGpuDrawParams()` line 71 mechanism as `bgfx_basiceffect_combined_test.cpp` (this batch):
`lightingEnabled_ ? diffuseColor_ : (diffuseColor_ + emissiveColor_)`, matching FNA's
`EffectHelpers.SetMaterialColor` lighting-disabled branch exactly. `LightingEnabled` is left at its correct
FNA default (`false`, `BasicEffect.hpp:367`) rather than being explicitly set, which is itself a small but
real piece of coverage: this test would catch a regression to the *default* value, not just the explicit
setter.

### Behavioral correctness

Independently recomputed with `kTexColor=(200,100,50)`, `kDiffuse=(0.3,0.2,0.1)`, `kEmissive=(0.2,0.1,0.4)`:

- Combined material color: `(0.3+0.2, 0.2+0.1, 0.1+0.4) = (0.5, 0.3, 0.5)`.
- `TextureColor × combined = (200×0.5, 100×0.3, 50×0.5) = (100, 30, 25)` — matches `kExpected` exactly.
- Diffuse-alone control: `(200×0.3, 100×0.2, 50×0.1) = (60, 20, 5)` — matches `kEmissiveIgnored` (the value
  the pixel would be if `EmissiveColor` were silently dropped).
- Emissive-alone control: `(200×0.2, 100×0.1, 50×0.4) = (40, 10, 20)` — matches `kDiffuseIgnored` (the value
  the pixel would be if `DiffuseColor` were silently dropped).

All three values independently confirmed exact.

### Robustness

The two negative assertions (`!matches(got, kEmissiveIgnored)`, `!matches(got, kDiffuseIgnored)`, lines
128-133) are the actual regression guard for Task 369's specific failure mode (one color silently dropped)
— a test that only asserted the combined value would still pass if, say, the combination arithmetic were
subtly wrong in a way that happened to coincide with one of the "ignored" values for a *different* input
pair; picking `kDiffuse`/`kEmissive` values whose sum, and whose two individual components, are all mutually
distinguishable (no channel collision across the three candidate outputs) makes this a genuinely
well-targeted, not just decorative, pair of negative controls.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings.

## Cross-File Observations

- Uses the identical `FillGpuDrawParams()` line and FNA source citation as
  `bgfx_basiceffect_combined_test.cpp` (this batch); both independently confirm the same formula from two
  different angles (this file isolates diffuse+emissive alone; the combined test adds texture-UV and
  vertex-color on top).
- Does not touch `BasicEffect.VertexColorEnabled` (the bare-public-field API wart noted in the combined
  test's report), so that observation does not apply here.

## Missing or Weak Tests

- No case combines `EmissiveColor` with `LightingEnabled=true` (the "merge with ambient" path,
  `EffectHelpers.SetMaterialColor`'s other branch: `emissive = (EmissiveColor + Ambient*Diffuse)*alpha`) —
  that combination is presumably covered elsewhere in the lit-effect test family (not in this batch), but is
  worth noting as an adjacent, not-covered-here combination.

## Positive Findings

- All three expected/control values independently recomputed and confirmed exactly correct.
- Genuinely well-targeted two-pronged negative control design that would catch the specific "one color
  silently dropped" failure mode it was written against.
- Correctly relies on `LightingEnabled`'s real default rather than over-specifying it, giving incidental
  coverage of the default value itself.

## Final Assessment

A precise, correctly-derived regression test with no issues found. Both the formula and its negative
controls were independently verified arithmetically correct against the stated inputs and against FNA's
real `EffectHelpers.SetMaterialColor` source.
