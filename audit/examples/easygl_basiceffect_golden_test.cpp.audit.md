# Audit: examples/easygl_basiceffect_golden_test.cpp

## Metadata

- Source file: `examples/easygl_basiceffect_golden_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — EasyGL backend `BasicEffect` golden-image regression test
- File type: C++ example/integration-test executable (`BasicEffectGoldenTest : CNA::Examples::PixelTestGame`,
  `main()`)
- Related production code: `Microsoft::Xna::Framework::Graphics::BasicEffect::FillGpuDrawParams()`
  (`BasicEffect.cpp`), shared test infra `examples/common/PixelTestGame.hpp` (`ExpectPixel`/`CompareGoldenImage`/
  `RunPixelTest`), checked-in reference image `examples/golden/easygl_basiceffect_golden_test.png` (confirmed
  present on disk)
- XNA/FNA relevance: same `TextureEnabled`/`VertexColorEnabled`/`DiffuseColor`/`EmissiveColor`,
  `LightingEnabled=false` formula as `easygl_basiceffect_combined_test.cpp` (Task 370), reused verbatim here.
- Main related tests: this file is the Task 464 golden-image counterpart to
  `easygl_basiceffect_combined_test.cpp` (Task 370) — both audited in this same batch.

## Purpose

Reuses Task 370's already-verified BasicEffect scene (texture+vertexColor+diffuse+emissive, no lighting) via the
new `PixelTestGame::CompareGoldenImage()` helper (Task 463), checking an 8×8 pixel region around the center against
a checked-in reference PNG instead of a single hand-picked point sample — described in its own header (lines 1-14)
as "the first real (non-canary) golden-image consumer." Placement matches `examples-tests-easygl`.

## Executive Verdict

**Healthy** — the scene, formula, and expected center-pixel value are all identical to (and independently verified
against, in this same audit batch) `easygl_basiceffect_combined_test.cpp`'s "top-left texel" case; the file's
own defense-in-depth design (a hardcoded `ExpectPixel` cross-check *and* the golden-image comparison) is a
genuinely good pattern, with one real inconsistency worth flagging relative to its sibling (F1).

## Checklist Results

### API / XNA / FNA parity
Uses `CNA::Examples::PixelTestGame` (a `Microsoft::Xna::Framework::Game` subclass) rather than `Game` directly —
a `NOXNA` CNA-internal test-infrastructure base class, correctly not part of the `Microsoft::Xna` namespace.
`setTextureEnabledProperty`, `setTextureProperty`, `VertexColorEnabled`, `setDiffuseColorProperty`,
`setEmissiveColorProperty` all real, correctly-used XNA members, identical usage to the Task 370 combined test.

### Behavioral correctness
Confirmed (during this audit's review of `easygl_basiceffect_combined_test.cpp`) that
`Color(99, 52, 23, 255)` (line 93, this file's `ExpectPixel` call) is the independently-verified correct value for
`TextureColor(200,100,50) × VertexColor(180,220,140)/255 × (DiffuseColor(0.6,0.4,0.8)+EmissiveColor(0.1,0.2,0.05))`
— recomputed in this audit as `(99, 52, 23)`, matching exactly. The scene setup (lines 62-86) is character-for-
character identical to Task 370's "top-left texel" sample (constant UV `(0.25,0.25)`, same `kTexels`, same
`kVertexColor`/`kDiffuse`/`kEmissive` constants) — confirmed by direct comparison against the sibling file read in
this same batch.

### Logic
`RunTest()` is a single straight-line scene: build 2×2 texture, build a constant-UV quad, construct/configure
`BasicEffect`, `Clear`/`Apply`/set `RasterizerState::CullNone`/`DrawUserPrimitives`, then two independent checks:
`ExpectPixel(...)` (single-pixel, hardcoded expected value) followed by `CompareGoldenImage(...)` (8×8 region vs.
checked-in PNG). The file's own comment (lines 88-91) explains *why* both checks exist: the `ExpectPixel` call is a
deliberate defense against the specific failure mode where a golden image was regenerated
(`CNA_UPDATE_GOLDEN=1`) against an already-broken render, which would otherwise make the golden comparison
"silently pass" against its own now-wrong reference. This is a genuinely sound test-design insight, not filler.

### Memory/resource lifetime
`Texture2D tex` is stack-local within `RunTest()`, referenced by pointer for the single draw call — no lifetime
issue. `gdm_` is a `std::unique_ptr<GraphicsDeviceManager>` member constructed in the test's own constructor,
matching every sibling file's pattern in this batch.

### C++ correctness
No manual pixel-buffer handling in this file itself — all delegated to `PixelTestGame::ExpectPixel`/
`CompareGoldenImage` (`examples/common/PixelTestGame.hpp`, out of this batch's scope but read for context during
this audit): confirmed those helpers correctly size their `std::vector<Color>` buffers from the passed
`Rectangle`'s `Width*Height` and validate the golden PNG's dimensions against the requested region
(`PixelTestGame.hpp:178-185`) before comparing, guarding against a mismatched/corrupt golden file silently
under- or over-reading.

### Performance
N/A — single-frame test, single draw.

### Robustness

### F1 — This file omits the blank-frame retry loop its own source scene (`easygl_basiceffect_combined_test.cpp`) uses, despite rendering the identical scene

- Severity: LOW
- Confidence: MEDIUM
- Category: cross-file consistency / robustness
- Location/symbol: `BasicEffectGoldenTest::RunTest()`, lines 55-98 (single Clear/Apply/Draw/readback, no loop) vs.
  `BasicEffectCombinedTest::Draw()`'s 20-iteration "skip blank/black frames" retry loop
  (`easygl_basiceffect_combined_test.cpp:141-153`)
- Evidence: this file's own header (lines 1-9) explicitly states it "renders the exact same
  quad/texture/material setup as Task 370's own 'top-left texel' sample" and even reuses that test's exact
  hardcoded expected value as a cross-check (line 92-93) — yet the combined test found it necessary to retry up to
  20 times per sample to skip a transient all-black frame, while this file (rendering the identical scene) does not.
- Why it matters: if the blank-frame race the combined test works around is a real, still-reproducing
  EasyGL/SDL timing issue, this file — described in its own header as "the first real (non-canary) golden-image
  consumer" and the more authoritative/CI-facing test of the two per its stated purpose — is more exposed to
  spurious failures than the test it explicitly models itself on. This is the same underlying inconsistency
  recorded once in full in the `easygl_basiceffect_combined_test.cpp` audit report (its Finding F2); repeated here
  because it is this file's own robustness gap, not just an observation about the other file.
- FNA/XNA comparison: N/A.
- Related files: `easygl_basiceffect_combined_test.cpp`.
- Suggested future action: same as the combined-test report — determine empirically whether the retry is still
  needed on the current EasyGL backend, and apply the same treatment to both files consistently.

### Testing
This file is itself a test. Its `ExpectPixel` + `CompareGoldenImage` dual-check design (see Logic above) is a
positive test-design pattern worth highlighting rather than a gap.

## Detailed Findings

(F1 above is the only substantive finding; no HIGH/CRITICAL findings.)

## Cross-File Observations

- Golden reference PNG (`examples/golden/easygl_basiceffect_golden_test.png`) confirmed present on disk during this
  audit — the test is not silently unable to run for lack of its fixture.
- `PixelTestGame::CompareGoldenImage()`'s `CNA_UPDATE_GOLDEN` environment-variable escape hatch (used to
  regenerate the reference) is itself gated behind manual review per its own doc comment
  (`PixelTestGame.hpp:134-138`, "review the new file... then commit it") — a reasonable process safeguard, not
  something this file itself needs to additionally guard against beyond its own `ExpectPixel` cross-check.
- Constructor pattern (`gdm_` built and sized in the test class's own constructor before `Game::Run()`) matches
  every other file in this batch.

## Missing or Weak Tests

- See F1 for the retry-loop asymmetry versus its own model test.
- Only checks one of the 4 texel samples the combined test covers (the "top-left" one) — a reasonable scope
  reduction for a golden-image test (an 8×8 region around one sample point is already a stronger per-pixel check
  than the combined test's single center pixel), but means 3 of the 4 texel-addressing paths the combined test
  exercises have no golden-image equivalent.

## Positive Findings

- Genuinely good test design: combining a hardcoded numeric cross-check (`ExpectPixel`) with an image-region
  comparison (`CompareGoldenImage`) specifically to catch the "golden image regenerated against a broken render"
  failure mode is a real, well-reasoned defense against a documented false-negative risk in golden-image testing
  generally, not just this project.
- Reuses an already-independently-verified scene/formula (Task 370) rather than inventing new, unverified expected
  values — reduces the risk of two independently-wrong-but-matching numbers.

## Final Assessment

A well-designed golden-image test whose scene and expected value are independently confirmed correct (via this same
audit's verification of its Task 370 source scene); its one real gap is the retry-loop inconsistency (F1) relative
to the test it explicitly models itself on, which could make it disproportionately flaky if that underlying race is
still live.
