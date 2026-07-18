# Audit: examples/easygl_basiceffect_default_lighting_test.cpp

## Metadata

- Source file: `examples/easygl_basiceffect_default_lighting_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — EasyGL backend `BasicEffect` property/constant integration test
- File type: C++ example/integration-test executable (`DefaultLightingTest : Game`, `main()`)
- Related production code: `Microsoft::Xna::Framework::Graphics::BasicEffect::EnableDefaultLighting()`
  (`BasicEffect.cpp:186-205`)
- XNA/FNA relevance: `BasicEffect.EnableDefaultLighting()` is a real XNA 4.0 API member; judged against
  `FNA/src/Graphics/Effect/StockEffects/EffectHelpers.cs::EnableDefaultLighting()`.
- Main related tests: this file only (Task 194) — no other file in this batch or its known siblings duplicates this
  exact-constant check.

## Purpose

Verifies that `BasicEffect::EnableDefaultLighting()` sets `AmbientLightColor` and all three `DirectionalLight`
instances' `Direction`/`DiffuseColor`/`SpecularColor`/`Enabled` to the exact XNA/FNA-specified constant values — a
pure property-state test, deliberately with **no pixel readback** (per its own header comment, line 18), since the
concern here is "were the literal constants copied correctly," not "does lighting render correctly" (that concern is
covered by the sibling `easygl_basiceffect_combinations_test.cpp`'s sub-case (e) and
`easygl_basiceffect_multilight_emissive_test.cpp`). Placement matches the `examples-tests-easygl` shard.

## Executive Verdict

**Healthy** — every expected constant in this file is character-for-character identical to
`FNA/src/Graphics/Effect/StockEffects/EffectHelpers.cs::EnableDefaultLighting()`'s own literals, and identical to
what CNA's `BasicEffect::EnableDefaultLighting()` actually assigns; this is an accurate, real fidelity check, not
boilerplate.

## Checklist Results

### API / XNA / FNA parity
`fx.EnableDefaultLighting()`, `getLightingEnabledProperty()`, `getAmbientLightColorProperty()`,
`getDirectionalLight0/1/2Property()`, and each returned `DirectionalLight`'s `getEnabledProperty()`/
`getDirectionProperty()`/`getDiffuseColorProperty()`/`getSpecularColorProperty()` are all real XNA members, called
with correct signatures (`getDirectionalLight0Property()` returns `DirectionalLight&`, consistent with
`BasicEffect.hpp:130` — a reference, not a value copy, matching this test's `auto& L = fx.getDirectionalLight0Property();`
usage at line 91).

### Behavioral correctness — literal-by-literal comparison
Directly diffed this file's expected constants (lines 7-16, 85, 94-96, 112-114, 130-132) against both
`FNA/src/Graphics/Effect/StockEffects/EffectHelpers.cs::EnableDefaultLighting()` (lines 42-64) and CNA's own
`BasicEffect::EnableDefaultLighting()` (`BasicEffect.cpp:186-205`):

| Field | FNA (EffectHelpers.cs) | CNA (BasicEffect.cpp) | Test expectation | Match |
|---|---|---|---|---|
| Ambient | `(0.05333332f, 0.09882354f, 0.1819608f)` | identical | identical (line 85) | Yes |
| Light0.Direction | `(-0.5265408f, -0.5735765f, -0.6275069f)` | identical | identical (line 94) | Yes |
| Light0.Diffuse | `(1, 0.9607844f, 0.8078432f)` | identical | identical (line 95) | Yes |
| Light0.Specular | `(1, 0.9607844f, 0.8078432f)` | identical | identical (line 96) | Yes |
| Light1.Direction | `(0.7198464f, 0.3420201f, 0.6040227f)` | identical | identical (line 112) | Yes |
| Light1.Diffuse | `(0.9647059f, 0.7607844f, 0.4078432f)` | identical | identical (line 113) | Yes |
| Light1.Specular | `Vector3.Zero` | `Vector3::Zero` | `Vector3::Zero` (line 114) | Yes |
| Light2.Direction | `(0.4545195f, -0.7660444f, 0.4545195f)` | identical | identical (line 130) | Yes |
| Light2.Diffuse | `(0.3231373f, 0.3607844f, 0.3937255f)` | identical | identical (line 131) | Yes |
| Light2.Specular | `(0.3231373f, 0.3607844f, 0.3937255f)` | identical | identical (line 132) | Yes |

All three lights' `Enabled` flags are checked `true` (lines 92, 110, 128), matching FNA's `light0.Enabled = true;`
etc. All 10 constant vectors and 3 boolean flags check out exactly.

### Logic
Straight-line `Draw()` override (no branching): construct `BasicEffect`, call `EnableDefaultLighting()` once, then
15 independent `check()`/`checkBool()` calls. No loops, no state transitions to verify.

### Memory/resource lifetime
`BasicEffect fx(dev)` is stack-local within `Draw()`, no ownership concerns.

### C++ correctness
`veq()` (lines 34-39) uses `std::fabs(a.X - b.X) <= tol` with a default `tol=1e-5f` — appropriate for comparing
`float`s that are copied from identical literal source text on both sides (test and production), so the comparison
should in practice be exact (difference `0.0f`) rather than merely "close"; the tolerance is a reasonable defensive
margin, not load-bearing for these specific values.

### Performance
N/A — no rendering, no hot path; this file doesn't even draw a primitive.

### Architecture
Correctly avoids any pixel readback (no `GraphicsDevice::Clear`/`DrawUserPrimitives`/`GetBackBufferData` calls at
all) since the property values, not their rendered effect, are what's under test — an appropriately scoped, minimal
test that doesn't over-reach into rendering concerns already covered elsewhere.

### Robustness
No malformed input path exists; this is a fixed, deterministic single-call test.

### Testing
This file itself is the test for `EnableDefaultLighting()`'s constant fidelity. See Missing or Weak Tests for what
it deliberately (and reasonably) leaves to other files.

## Detailed Findings

No findings at MEDIUM or above. One LOW/INFO observation:

### F1 — `PreferPerPixelLighting` and `Alpha`/`DiffuseColor` are untouched by `EnableDefaultLighting()` and correctly not asserted here

- Severity: INFO
- Confidence: HIGH
- Category: parity
- Location/symbol: `BasicEffect::EnableDefaultLighting()` (`BasicEffect.cpp:186-205`) only touches
  `lightingEnabled_`, `ambientLightColor_`, and the three `DirectionalLight` instances — it does not touch
  `diffuseColor_`, `alpha_`, `preferPerPixelLighting_`, matching FNA's `EnableDefaultLighting()` (`BasicEffect.cs`,
  which calls `EffectHelpers.EnableDefaultLighting` and separately assigns only `ambientLightColor = ...` — the
  material `DiffuseColor`/`Alpha` are untouched there too, left at whatever the caller previously set).
- Why it matters: not a defect — the test correctly does not assert these fields, since `EnableDefaultLighting()` is
  not specified (in FNA or here) to touch them. Recorded only because a superficial reading of "test every property"
  might otherwise flag their absence as a gap; it isn't one.
- FNA/XNA comparison: confirmed — `BasicEffect.cs`'s own `EnableDefaultLighting()` body only sets
  `lightingEnabled`, `ambientLightColor`, and the three lights (via `EffectHelpers.EnableDefaultLighting`).
- Suggested future action: none.

## Cross-File Observations

- This file is the *only* one of the 8 files in this batch that performs zero rendering/pixel-readback — every
  other file constructs a device, clears, draws, and reads back pixels. This is an intentional and correct scope
  split, not an inconsistency: rendered-lighting correctness is covered by
  `easygl_basiceffect_combinations_test.cpp` sub-case (e) and `easygl_basiceffect_multilight_emissive_test.cpp`,
  which both exercise `DirectionalLight0`'s fields manually (not via `EnableDefaultLighting()`) rather than
  re-testing this file's exact-constant concern.

## Missing or Weak Tests

- No test in this batch (or, from what was inspected, elsewhere) verifies that `EnableDefaultLighting()`'s constants
  actually *render* correctly when combined (i.e., a pixel test using the real 3-light rig via
  `EnableDefaultLighting()` rather than manually-set single-light values as the other files in this batch do) — a
  reasonable, low-priority gap given the property-level fidelity is already fully verified here and the *formula*
  for combining ambient+multiple lights is independently verified by
  `easygl_basiceffect_multilight_emissive_test.cpp` using hand-set light values.

## Positive Findings

- All 10 constant vectors and 3 boolean flags were independently diffed against both the FNA reference source and
  the CNA production implementation during this audit, and all matched exactly — a genuinely verified, non-trivial
  fidelity test, not a placeholder.
- Correct, minimal scope: doesn't try to also test rendering, appropriately leaving that to sibling files.

## Final Assessment

An accurate, fully-verified exact-constant fidelity test for `BasicEffect::EnableDefaultLighting()` with no defects
found; its explicit choice to test only property state (not rendered pixels) is well-reasoned given the coverage
provided by sibling files in this batch.
