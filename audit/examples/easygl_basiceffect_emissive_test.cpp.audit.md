# Audit: examples/easygl_basiceffect_emissive_test.cpp

## Metadata

- Source file: `examples/easygl_basiceffect_emissive_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — EasyGL backend `BasicEffect` pixel integration test
- File type: C++ example/integration-test executable (`BasicEffectEmissiveTest : Game`, `main()`)
- Related production code: `Microsoft::Xna::Framework::Graphics::BasicEffect::FillGpuDrawParams()`
  (`BasicEffect.cpp:60-75`)
- XNA/FNA relevance: `BasicEffect.DiffuseColor`/`.EmissiveColor` with `LightingEnabled=false`, judged against
  `FNA/src/Graphics/Effect/StockEffects/EffectHelpers.cs::SetMaterialColor()`'s disabled-lighting branch.
- Main related tests: this file (Task 369) is itself the origin of a real, fixed bug in production code (see
  Positive Findings) — its own header comment documents this.

## Purpose

Verifies that with `LightingEnabled=false`, `BasicEffect`'s rendered pixel equals
`TextureColor × (DiffuseColor + EmissiveColor) × Alpha`, specifically to guard against `EmissiveColor` being
silently dropped from the disabled-lighting formula — which, per the file's own header comment (lines 13-20), is
**exactly the real bug this test discovered and whose fix it now guards against regressing**. Placement matches the
`examples-tests-easygl` shard.

## Executive Verdict

**Healthy** — the test's documented bug-discovery narrative was independently corroborated against the current
production code (`BasicEffect.cpp:71`), and the discriminating-power design (three mutually-exclusive expected
colors: correct-sum, emissive-ignored, diffuse-ignored) is a genuinely strong, non-trivial test oracle.

## Checklist Results

### API / XNA / FNA parity
`setTextureEnabledProperty`, `setTextureProperty`, `setDiffuseColorProperty`, `setEmissiveColorProperty` are all
real XNA members used correctly. `LightingEnabled` is left at its default `false` (never explicitly set) — matches
`BasicEffect.hpp:367`'s `bool lightingEnabled_ = false;` default, and matches FNA's own `BasicEffect` default
(`lightingEnabled` starts `false` until `EnableDefaultLighting()`/`LightingEnabled=true` is called) — this is a
correct, deliberate choice to exercise the *default* no-lighting path without extra setup.

### Behavioral correctness — verified against production code and FNA
Traced `BasicEffect::FillGpuDrawParams()` (`BasicEffect.cpp:65-75`):
```cpp
const Vector3 forwardedDiffuse = lightingEnabled_ ? diffuseColor_ : (diffuseColor_ + emissiveColor_);
p.diffuseColor[0] = forwardedDiffuse.X * alpha_;
...
```
With `lightingEnabled_=false` (this test's setup), `forwardedDiffuse = kDiffuse(0.3,0.2,0.1) + kEmissive(0.2,0.1,0.4)
= (0.5,0.3,0.5)`, matching the file's own comment (line 63: "Expected: TextureColor*(DiffuseColor+EmissiveColor) =
TextureColor*(0.5,0.3,0.5)"). Independently recomputed against `kTexColor=(200,100,50)`:
`200*0.5=100`, `100*0.3=30`, `50*0.5=25` → `(100,30,25)`, exactly matching `kExpected` (line 64). The two
"discriminator" colors are also independently verified: `kEmissiveIgnored` (line 66, `TextureColor*DiffuseColor`
alone) = `200*0.3=60, 100*0.2=20, 50*0.1=5` → `(60,20,5)` ✓ matches; `kDiffuseIgnored` (line 67,
`TextureColor*EmissiveColor` alone) = `200*0.2=40, 100*0.1=10, 50*0.4=20` → `(40,10,20)` ✓ matches. All three
computed independently during this audit and all check out.

This test's own header (lines 13-20) claims to document a real bug it found and fixed: `FillGpuDrawParams()`
"forwarded `DiffuseColor*Alpha` alone, in all cases, completely dropping `EmissiveColor` from the no-lighting
formula." Cross-checked against the *current* state of `BasicEffect.cpp:71` — the fix described (baking
`EmissiveColor` into the forwarded diffuse when `lightingEnabled_` is false) is present in the code today, i.e. the
bug this test claims to have found and fixed is genuinely fixed in the current tree, and this test is the live
regression guard for it.

### Logic
Single scene, single `BasicEffect`, three independent `check()` assertions against the *same* rendered pixel
(correct-value check, emissive-not-ignored check, diffuse-not-ignored check) — an efficient, well-designed test
oracle that would fail distinctly depending on *which* component a regression dropped, not just "something's
wrong."

### Memory/resource lifetime
`Texture2D tex` stack-local, referenced by pointer for the single draw — no lifetime issue.

### C++ correctness
Same `closeTo`/`matches` pattern as sibling files (verified safe, `int`-domain absolute-difference comparison).

### Performance
See F1 below (shared retry-loop pattern, same as `easygl_basiceffect_combined_test.cpp`).

### Robustness / Testing

### F1 — Same retry-until-non-black loop weakness as `easygl_basiceffect_combined_test.cpp`

- Severity: MEDIUM
- Confidence: MEDIUM
- Category: test-coverage / robustness
- Location/symbol: `BasicEffectEmissiveTest::Draw()`, lines 132-144
- Evidence: identical shape to the sibling file's loop — `for (i<20) { Clear; Apply; Draw; got=readCenter(dev); if
  (got != black) break; }` — accepts the first non-black frame unconditionally as final, without re-validating
  stability or correctness before breaking.
- Why it matters: same reasoning as `easygl_basiceffect_combined_test.cpp`'s Finding F1 — a transient, non-black,
  *wrong* first frame would be accepted and only later fail the final `check()` calls, indistinguishable from a
  genuine logic regression. Recorded once in full there; cross-referenced here rather than repeated verbatim.
- FNA/XNA comparison: N/A.
- Related files: `easygl_basiceffect_combined_test.cpp` (same pattern, same finding), `easygl_basiceffect_multilight_emissive_test.cpp`
  (same pattern, also flagged there).
- Suggested future action: see the combined_test report's F1 suggestion (verify two consecutive stable reads before
  accepting).

## Detailed Findings

(F1 above is the only substantive finding; no HIGH/CRITICAL findings.)

## Cross-File Observations

- This file, `easygl_basiceffect_combined_test.cpp`, and `easygl_basiceffect_multilight_emissive_test.cpp` share
  the identical 20-iteration retry-loop shape verbatim — worth fixing/removing consistently across all three if
  addressed, rather than in just one.
- `easygl_basiceffect_combinations_test.cpp`'s sub-case (c) ("Diffuse-tint red") and this file both test
  `DiffuseColor` tinting a texture, but this file is the only one that also varies `EmissiveColor` non-zero,
  correctly making it the more discriminating test for that specific interaction — no redundancy concern.

## Missing or Weak Tests

- No case in this file exercises `EmissiveColor` with `LightingEnabled=true` (the file's own header, lines 22-27,
  explicitly and correctly scopes this out, noting it needs its own task since it requires forwarding
  `SpecularColor`/`SpecularPower`/multi-light data — that gap is exactly what
  `easygl_basiceffect_multilight_emissive_test.cpp`, Task 885, later fills; confirmed by reading that file in this
  same batch — so this is a resolved, not open, gap).
- See F1 for the retry-loop oracle-strength gap.

## Positive Findings

- This is a genuine bug-discovery test: its own documented claim of finding and fixing a real `EmissiveColor`-
  dropping bug in `FillGpuDrawParams()` was independently corroborated against the current production source
  during this audit — the fix is real and present.
- The three-way discriminating expected-color design (correct/emissive-ignored/diffuse-ignored) is a genuinely
  strong test oracle that would localize a regression's exact nature, not just detect "something changed."
- All three expected colors were independently recomputed during this audit and matched exactly.

## Final Assessment

A well-designed, correctly-derived regression test for a real, confirmed-fixed production bug, with the same
retry-loop oracle-strength gap (F1) shared by its `combined_test`/`multilight_emissive_test` siblings.
