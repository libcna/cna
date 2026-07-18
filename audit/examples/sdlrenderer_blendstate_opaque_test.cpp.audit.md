# Audit: examples/sdlrenderer_blendstate_opaque_test.cpp

## Metadata

- Source file: `examples/sdlrenderer_blendstate_opaque_test.cpp` (116 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlrenderer` shard — `BlendState::Opaque` dedicated pixel test.
- File type: standalone `Game`-subclass executable, CTest-registered (`SDL_Renderer_BlendState_Opaque`,
  `cmake/Tests/SdlRendererTests.cmake`).
- XNA/FNA relevance: direct — `BlendState.Opaque`'s "ignore alpha and destination entirely" contract.
- FNA reference: `Graphics/States/BlendState.cs` (lines 188-194: `Opaque = new BlendState(..., Blend.One,
  Blend.One, Blend.Zero, Blend.Zero)`).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/BlendState.cpp` (line 9),
  `src/CNA/Internal/Backends/SdlRenderer/SdlGraphicsBackend.cpp` (`ApplyBlendState`/`ToSdlBlendFactor`,
  lines 648-697).

## Purpose

Single-check dedicated verification of `BlendState::Opaque`: draws a *low*-alpha (64/255) red quad over a solid
green background using `Opaque`, and checks the result is pure, fully-opaque red — proving alpha is genuinely
ignored and the destination is genuinely fully overwritten, not merely "mostly red" from an alpha-blend fallback
that happens to look similar at a glance. The comment (lines 11-15) is explicit that the failure mode this guards
against is "silently falls back to alpha-blending," which a low (not zero, not full) alpha value like 64/255 is
well-chosen to expose (a true `Opaque` result and an alpha-blended-with-green result would look visibly different
at 64/255, unlike at alpha=0 or alpha=255 where the two could coincidentally look similar).

## Executive Verdict

**Healthy.** The check is correctly derived, and the deliberate choice of a low, non-degenerate alpha value (64,
not 0 or 255) is a genuinely well-thought-out test design decision that maximizes discriminating power between
"real Opaque" and "silently-alpha-blended" failure modes.

## Checklist Results

### API / XNA / FNA parity

`BlendState::Opaque` (`BlendState.cpp` line 9: `colorSrc=One, alphaSrc=One, colorDst=Zero, alphaDst=Zero`) matches
FNA's own preset (`BlendState.cs` lines 188-194) exactly. The header comment's equation `dst = src*1 + dst*0 = src`
is the correct reading of `ColorSourceBlend=One, ColorDestinationBlend=Zero`.

### Behavioral correctness

Hand-derivation: with `colorSrc=One, colorDst=Zero`, the blend equation completely ignores both `dst_bg` and
`srcA` — `dst = src*1 + dst_bg*0 = src`, regardless of what `src`'s own alpha channel says. Drawing
`Color(255, 0, 0, 64)` (line 84, low alpha) over `kGreen=(0,255,0)` should therefore produce pure
`(255, 0, 0)` — matches the test's own expected `kRed=(255,0,0,255)` (line 91) with tolerance 10.

This is a genuinely discriminating check: had `ApplyBlendState` silently degraded `Opaque` to
`SDL_BLENDMODE_BLEND` (the exact class of bug Task 695's own audit found for `AlphaBlend`/`NonPremultiplied`, see
`sdlrenderer_blendstate_audit_test.cpp`'s audit report), the low alpha=64/255 would have let ~75% of the green
background survive: `dst ≈ (255,0,0)*0.251 + (0,255,0)*0.749 ≈ (64, 191, 0)` — a result that would fail this
test's tolerance-10 check against `(255,0,0)` by a wide margin (191 vs. an allowed deviation of 10 on the G
channel alone). The choice of alpha=64 (rather than, say, alpha=200, which would produce a less starkly different
false-positive result) is a deliberately strong choice for this specific regression's discriminating power.

### Logic

Single texture, single draw, single sample; no additional branching to evaluate.

### C++ correctness

No issues; same `colourMatch` helper pattern as the rest of this batch.

### Robustness

`PresentationMode::NativeBackBuffer` correctly set (line 105), same Task 915 rationale as the rest of the batch.

### Testing

Correctly scoped and well-targeted at the specific "was already correct, still verify" concern its own header
comment (lines 6-9) states (`Opaque` was one of only 2 presets the old, incomplete mapping already special-cased
correctly via `SDL_BLENDMODE_NONE`) — this file exists to guard against a future regression reintroducing the
generic fallback for this preset too, not to discover a currently-unknown bug.

## Detailed Findings

None at HIGH/CRITICAL/MEDIUM/LOW severity.

## Cross-File Observations

- The choice of a low, non-degenerate alpha (64) as the input is notably more rigorous than a hypothetical
  simpler version of this test that used `alpha=0` or omitted alpha entirely — worth calling out positively since
  weaker test design choices (e.g. always using full or zero alpha) are exactly the kind of accidentally-weak test
  this audit has been asked to watch for; this file does not have that weakness.
- Complements the other 5 blend-state files in this batch by covering the one preset (`Opaque`) whose entire
  defining characteristic is "the alpha channel and destination are irrelevant" — a fundamentally different
  property to verify than the arithmetic-blend presets the other 5 files check.

## Missing or Weak Tests

None identified — the single check is well-targeted and appropriately scoped for this preset's simple contract.

## Positive Findings

- Deliberate, well-reasoned choice of a low (not degenerate) alpha value to maximize the test's power to detect
  a "silently alpha-blends instead of truly ignoring alpha" regression.
- Header comment accurately and specifically describes the historical context (this preset was already correctly
  mapped pre-Task-695) rather than a generic "verify this works" statement.

## Final Assessment

A small, well-designed, correctly-derived dedicated preset test. No defects found in either the test or the
production `Opaque` mapping it exercises.
