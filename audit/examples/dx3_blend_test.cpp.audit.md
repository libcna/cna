# Audit: examples/dx3_blend_test.cpp

## Metadata
- Source file: `examples/dx3_blend_test.cpp` (153 lines)
- Audit status: AUDITED (full read)
- Subsystem: `examples-tests-dx3` shard
- File type: standalone backend integration-test executable (`Game` subclass)
- XNA/FNA relevance: exercises `BlendState`/`SpriteBatch::Draw` (public XNA API) against the DX3
  (DirectDraw, via `free-direct`) backend's blend-mode compositing math

## Purpose
Verifies the DX3 backend's 4 `BlendState` presets (Opaque/AlphaBlend/NonPremultiplied/Additive)
produce their real, distinct compositing formulas exactly, plus DX3-44's preset-detection fallback
for custom (non-preset) `BlendState` combinations.

## Executive Verdict
Correct and carefully constructed: the fixed source/background colors are explicitly chosen "with
comfortable margin from any rounding boundary" so every check is an exact-match assertion, not
tolerance-based — and Check F specifically distinguishes "factors match a preset" from "the blend
equation (BlendFunction) also matches," proving preset detection isn't fooled by factor-only
matches.

## Checklist Results
- Each of the 4 preset checks documents its own expected formula in the header comment (e.g.
  AlphaBlend: "premultiplied convention: src used as-is, dst*(1-srcAlpha)") and the code asserts the
  exact numeric (R,G) result derived from that formula — a real mathematical correctness proof, not
  an approximate/tolerance-based one.
- Check F's "Opaque-matching factors with `BlendFunction::Subtract`" test is a genuinely
  non-trivial edge case: it specifically guards against a preset-detection implementation that only
  compares the 4 blend factors (One/One/Zero/Zero) without also checking the blend equation itself,
  which would misdetect this combination as `Opaque` (r=200,g=0) rather than correctly falling back
  to `AlphaBlend` (r=200,g=30).

## Detailed Findings
None.

## Cross-File Observations
This is a real blend-math correctness test distinct from and complementary to the standing
`Dx3_SpriteBatch` 2/10-failing-checks investigation (from persistent memory) — that investigation's
"Check D" concern was specifically about premultiplied-alpha semantics in `dx3_spritebatch_test.cpp`
(audited separately in this batch), not this file. This file's own `AlphaBlend` check (asserting the
premultiplied convention: "src used as-is, dst*(1-srcAlpha)") independently confirms the DX3
backend's `AlphaBlend` preset itself computes the correct premultiplied formula — if
`dx3_spritebatch_test.cpp`'s Check D failure is a real backend bug rather than a test-authoring bug,
it is NOT a blend-preset-formula bug (this file proves that specific formula is correct), narrowing
the likely cause to something specific to how that other test's fixture/assertions are constructed.

## Missing or Weak Tests
None identified for this file's stated scope — all 4 presets plus 2 fallback edge cases are
covered with exact-match assertions.

## Positive Findings
Check F is an excellent piece of test design: it's easy to write a preset-detection test that only
tries "different factors" combinations; testing "same factors, different equation" specifically
targets the actual axis a real implementation bug is most likely to hide on.

## Final Assessment
No findings. Provides a useful negative-corroboration data point for the standing
`dx3_spritebatch_test.cpp` premultiplied-alpha investigation (see Cross-File Observations).
