# Audit: examples/easygl_basiceffect_combined_test.cpp

## Metadata

- Source file: `examples/easygl_basiceffect_combined_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — EasyGL backend `BasicEffect` pixel integration test (Phase 42 capstone)
- File type: C++ example/integration-test executable (`BasicEffectCombinedTest : Game`, `main()`)
- Related production code: `Microsoft::Xna::Framework::Graphics::BasicEffect::FillGpuDrawParams()`
  (`BasicEffect.cpp:51-141`), `CNA::Internal::Backends::EasyGL::EasyGLGraphicsBackend::BindDrawParams`
  (`EasyGLGraphicsBackend.cpp:3983-4130` region) and its stride-24 (`VertexPositionColorTexture`) shader
- XNA/FNA relevance: exercises `BasicEffect.TextureEnabled`/`.Texture`, `.VertexColorEnabled`, `.DiffuseColor`,
  `.EmissiveColor` with `LightingEnabled=false`; judged against `FNA/src/Graphics/Effect/StockEffects/
  EffectHelpers.cs::SetMaterialColor()`'s disabled-lighting branch.
- Main related tests: this file (Task 370, "closes Phase 42"); reused verbatim by
  `easygl_basiceffect_golden_test.cpp` (Task 464) as its scene source, audited separately in this batch.

## Purpose

Task 370's capstone test for BasicEffect on EasyGL: combines texture-enabled, vertex-color-enabled, and
diffuse+emissive material color (with lighting disabled) into one scene using a real 2×2 multi-texel texture,
sampling all 4 texels via 4 separate draws with per-draw constant UV, and checking each against an independently
derived expected color. Placement matches `AUDIT_SCOPE.md`'s `examples-tests-easygl` shard.

## Executive Verdict

**Healthy** — the expected-value formula (`TextureColor × VertexColor/255 × (DiffuseColor+EmissiveColor)`) was
independently re-derived from FNA's real `EffectHelpers.SetMaterialColor()` disabled-lighting branch and matched
exactly for all 4 texel samples; one genuine test-oracle robustness gap is worth flagging (F1) alongside a
cross-file inconsistency versus its own golden-image successor (F2).

## Checklist Results

### API / XNA / FNA parity
`setTextureEnabledProperty`, `setTextureProperty`, `VertexColorEnabled` (public field, matches `BasicEffect.hpp`),
`setDiffuseColorProperty`, `setEmissiveColorProperty` are all real, correctly-used XNA members.

### Behavioral correctness — formula verified against FNA + production code
Traced `FNA/src/Graphics/Effect/StockEffects/EffectHelpers.cs::SetMaterialColor()` (lines 228-238, the
`lightingEnabled==false` branch): `diffuse = (DiffuseColor + EmissiveColor) * Alpha`, and the shader multiplies
texture × vertex-color × this diffuse value directly (no separate emissive-uniform path taken when lighting is
disabled). CNA's `BasicEffect::FillGpuDrawParams()` (`BasicEffect.cpp:71-75`) implements the identical branch:
`forwardedDiffuse = lightingEnabled_ ? diffuseColor_ : (diffuseColor_ + emissiveColor_)`, then
`p.diffuseColor[i] = forwardedDiffuse[i] * alpha_`. With `kDiffuse=(0.6,0.4,0.8)`, `kEmissive=(0.1,0.2,0.05)`, sum
= `(0.7,0.6,0.85)`. For the "top-left texel" sample: `TexelColor=(200,100,50)`, `VertexColor=(180,220,140)/255`.
Independently recomputed: `200*(180/255)*0.7 ≈ 98.8`, `100*(220/255)*0.6 ≈ 51.8`, `50*(140/255)*0.85 ≈ 23.4` →
`(99, 52, 23)` — matches the file's own `kSamples[0].expected = Color(99, 52, 23, 255)` (line 67) exactly. Spot-
checked the "bottom-right" sample similarly: `TexelColor=(150,150,150)`, same VertexColor/diffuse-sum:
`150*(180/255)*0.7≈74.1`, `150*(220/255)*0.6≈77.6`, `150*(140/255)*0.85≈70.0` → `(74,78,70)` — matches
`kSamples[3].expected = Color(74, 78, 70, 255)` (line 70) exactly.

### Logic
The 4-sample loop (lines 124-156) constructs a fresh `BasicEffect` per sample (not reused/mutated), sets a
constant UV across the whole quad so every pixel samples the same texel (correctly isolating "does this UV reach
the right texel" from bilinear filtering, as the header comment claims, lines 17-20), and reads back the center
pixel. `RasterizerState::CullNone` (line 148) is set for the same documented "Task 896" winding reason as its
sibling files in this batch.

### Memory/resource lifetime
`Texture2D tex` is stack-local, constructed once, referenced via `&tex` across all 4 loop iterations — outlives
every use. No lifetime issue.

### C++ correctness
`matches()`/`closeTo()` (lines 96-103) correctly bound-check with `std::abs(a-b) <= tol` on plain `int`s (already
widened via `getRProperty()` etc. returning byte-range values promoted to `int` parameters) — no overflow/UB risk.

### Performance
N/A for a single-frame-per-sample test, but see F1 for the retry loop's cost/robustness trade-off.

### Robustness / Testing

### F1 — Retry-until-non-black loop accepts the *first* non-black frame without validating it's the *correct* frame

- Severity: MEDIUM
- Confidence: MEDIUM
- Category: test-coverage / robustness
- Location/symbol: `BasicEffectCombinedTest::Draw()`, lines 141-153
- Evidence:
  ```cpp
  for (int i = 0; i < 20; ++i)
  {
      dev.Clear(Color(0, 0, 0, 255));
      ...
      dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);
      got = readCenter(dev);
      if (got.getRProperty() != 0 || got.getGProperty() != 0 || got.getBProperty() != 0)
          break; // skip blank/black frames
  }
  ```
  The loop's only stopping condition is "not exactly black." It does not re-check on a second consecutive read that
  the value is stable, nor does it compare against the expected color before breaking — it simply takes whatever
  non-black value the first successful iteration produces as `got`, then checks that against `s.expected` only
  *after* the loop. If frame N (for some N<20) happens to render a transient, non-black, *incorrect* value (e.g. a
  half-uploaded texture or a stale scissor/viewport from a previous state), the loop stops immediately and that
  wrong value becomes the final `got` — it is never given a chance to "recover" to the correct value on a later
  iteration, and the failure this produces at the final `check()` would look identical to a genuine rendering-logic
  bug, adding noise without adding real robustness for that specific class of transient bug.
- Why it matters: this is explicitly a workaround for a known real flakiness class (per the loop's own comment,
  "skip blank/black frames") — the workaround is directionally reasonable for the specific "backbuffer was still
  showing the previous Clear() when read" race, but its acceptance criterion is too weak to be a genuine "wait for
  a valid frame" barrier; it only catches the all-black failure mode, not a partially-wrong one.
- FNA/XNA comparison: N/A — this is a CNA/EasyGL/SDL frame-presentation timing concern, not an XNA-API concern.
- Related files: none of this file's siblings without the loop (`easygl_basiceffect_combinations_test.cpp`,
  `easygl_basiceffect_fog_test.cpp`, `easygl_basiceffect_default_lighting_test.cpp`) exhibit or need this pattern —
  see F2 for why that is itself worth flagging.
- Suggested future action (not implemented by this audit): either drop the retry loop if the underlying blank-frame
  race no longer reproduces, or strengthen it to compare two consecutive non-black reads for equality before
  accepting the frame as stable.

### F2 — This file's retry-loop workaround is absent from its own golden-image successor test

- Severity: LOW
- Confidence: MEDIUM
- Category: cross-file consistency
- Location/symbol: this file's retry loop (lines 141-153) vs. `easygl_basiceffect_golden_test.cpp`'s single-shot
  `Clear()`/`Apply()`/`Draw()`/compare sequence (no retry loop at all, reusing this exact same scene per its own
  header comment)
- Evidence: `easygl_basiceffect_golden_test.cpp` explicitly says it "renders the exact same quad/texture/material
  setup as Task 370's 'top-left texel' sample" and even hardcodes the same expected `(99,52,23)` value as a
  cross-check — yet it draws and reads back exactly once, with no blank-frame retry protection.
- Why it matters: if the blank/black-frame race this file works around is a real, still-reproducing EasyGL/SDL
  timing issue (not just historical), the golden test — described in its own header as "the capstone test" and "the
  first real (non-canary) golden-image consumer" — would be more prone to spurious CI failures than the test it
  claims to be equivalent to, since it has no such protection. Conversely, if the race no longer reproduces, this
  file's own retry loop is now unnecessary dead weight (20x worst-case draw calls for one pixel check). Either way,
  the inconsistency between the two files covering literally the same scene is worth resolving one way or the
  other.
- FNA/XNA comparison: N/A.
- Suggested future action: confirm empirically (e.g. instrument frame count actually needed) whether the retry is
  still load-bearing on the current EasyGL backend; if yes, add it to the golden test too; if no, remove it here.

## Cross-File Observations

- Formula, texel set, vertex color, diffuse/emissive constants are all identical, character-for-character, between
  this file and `easygl_basiceffect_golden_test.cpp` — confirmed by direct comparison, a deliberate and successful
  "same scene, two different assertion styles" design, not accidental duplication.
- Shares the `RasterizerState::CullNone`/"Task 896" comment with every other file in this batch that draws a
  full-screen quad.

## Missing or Weak Tests

- No case in this file (or its golden sibling) exercises `Alpha < 1.0` combined with this texture×vertexColor×
  diffuse+emissive formula — `Alpha` is present in the FNA formula (`(DiffuseColor+EmissiveColor)*Alpha`) but
  always implicitly 1.0 here since `BasicEffect::alpha_` defaults to `1.0f` and is never set by this test.
- See F1 for the retry-loop's own oracle-strength gap.

## Positive Findings

- Independently re-derived expected values for 2 of the 4 texel samples during this audit and both matched the
  hardcoded expectations exactly — genuine, verified math, not guesswork.
- The 2×2 multi-texel texture design (vs. a trivial 1×1 solid color used by most sibling tests) is a real
  strengthening of test coverage: it actually exercises per-vertex UV addressing into a specific texel, which the
  file's header (lines 13-20) correctly explains was needed to catch a genuine prior bug (`MakeBgfxLayout()`
  leaving `a_texcoord0` unbound — a Bgfx-side bug, but the same test design choice benefits EasyGL's own UV-path
  coverage too).

## Final Assessment

A correct, well-derived capstone test whose expected values check out against both FNA reference math and the real
CNA/EasyGL formula chain, with one real test-oracle robustness gap (F1) and one worth-resolving inconsistency
against its own golden-image successor (F2).
