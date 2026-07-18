# Audit: examples/sdlrenderer_blendstate_alphablend_test.cpp

## Metadata

- Source file: `examples/sdlrenderer_blendstate_alphablend_test.cpp` (116 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlrenderer` shard — `BlendState::AlphaBlend` (genuinely premultiplied source) pixel test
- File type: standalone `Game`-subclass executable, CTest-registered (`SDL_Renderer_BlendState_AlphaBlend`,
  `cmake/Tests/SdlRendererTests.cmake`).
- XNA/FNA relevance: direct — `BlendState.AlphaBlend`'s premultiplied-alpha contract.
- FNA reference: `Graphics/States/BlendState.cs` (lines 172-178: `AlphaBlend = new BlendState(..., Blend.One,
  Blend.One, Blend.InverseSourceAlpha, Blend.InverseSourceAlpha)`).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/BlendState.cpp` (line 7),
  `src/CNA/Internal/Backends/SdlRenderer/SdlGraphicsBackend.cpp` (`ApplyBlendState`, `ToSdlBlendFactor`,
  lines 648-697).
- Sibling test: `examples/sdlrenderer_blendstate_audit_test.cpp` (Task 695) proves the *opposite* case — feeding
  `AlphaBlend` non-premultiplied data to demonstrate the well-known over-bright artifact. This file complements
  that by feeding genuinely premultiplied data and checking the textbook-correct result.

## Purpose

`SdlBlendStateAlphaBlendTest` draws a single 1x1 texture whose stored colour has already been premultiplied by its
own alpha (`Color premultipliedRed(128, 0, 0, 128)` at line 69 — R scaled to `255*128/255≈128`, not left at the
full 255) over a solid blue background, using `BlendState::AlphaBlend`, and checks the result matches the
textbook premultiplied-alpha blend equation.

## Executive Verdict

**Healthy.** The single check is correctly derived and the test correctly distinguishes "premultiplied source
fed to AlphaBlend" (this file) from "non-premultiplied source fed to AlphaBlend" (the sibling audit test) — two
genuinely different scenarios that a less careful test suite could conflate.

## Checklist Results

### API / XNA / FNA parity

`BlendState::AlphaBlend` (`BlendState.cpp` line 7: `colorSrc=One, alphaSrc=One, colorDst=InverseSourceAlpha,
alphaDst=InverseSourceAlpha`) matches FNA's own `AlphaBlend` preset exactly (`BlendState.cs` lines 172-178, same
four values in the same order). The header comment's stated equation `dst = src*1 + dst*(1-srcA)` is the correct
reading of `ColorSourceBlend=One, ColorDestinationBlend=InverseSourceAlpha`.

### Behavioral correctness

Hand-derivation: `src=(128,0,0)` (already scaled by its own alpha 128/255), `srcA=128/255≈0.502`,
`dst_bg=(0,0,255)`. `dst = src*1 + dst_bg*(1-0.502) = (128,0,0) + (0,0,255*0.498) ≈ (128, 0, 127)`. This matches
the test's own expected `Color(128, 0, 127, 255)` (line 91) with tolerance 15 — independently confirmed correct,
not merely internally consistent with the file's own comment.

The key point this test isolates — that `AlphaBlend`'s equation assumes the SOURCE colour is *already*
premultiplied, and produces the textbook-correct 50/50 blend only when fed data satisfying that precondition — is
exactly the contrast the sibling `sdlrenderer_blendstate_audit_test.cpp` needs (that file deliberately violates the
precondition to reproduce the well-documented "over-bright" artifact). Reading both files together, the pairing is
a genuinely useful, non-redundant discriminating pair, not two near-duplicate checks.

### Logic

Single texture, single draw, single sample — no branching logic to evaluate beyond `colourMatch`'s tolerance check
(same helper as the other blend-state tests in this batch, R/G/B only, alpha ignored, correct for this purpose).

### C++ correctness

No issues — `(int)` casts before `std::abs` correctly avoid unsigned-subtraction wraparound (same pattern as the
other files in this batch).

### Robustness

`PresentationMode::NativeBackBuffer` correctly set (line 105) with the same Task 915 physical/logical coordinate
rationale as the other files in this batch.

### Testing

The comment block (lines 6-10) correctly cross-references Task 695's own audit test as the complementary
"non-premultiplied fed to AlphaBlend" case — this is accurate: `sdlrenderer_blendstate_audit_test.cpp`'s own
`AlphaBlend` check (see that file's audit report) does feed a genuinely non-premultiplied straight-alpha source
and expects the over-bright `(255,127,0)` result, confirming the cross-reference is not a stale claim.

## Detailed Findings

None at HIGH/CRITICAL severity, and none at MEDIUM/LOW either — this is a small, correctly-scoped, verified-correct
file.

## Cross-File Observations

- Genuinely complements (not duplicates) `sdlrenderer_blendstate_audit_test.cpp`'s `AlphaBlend` check — this audit
  independently confirmed both files' respective expected constants are each correct for their own distinct input
  precondition (premultiplied here, straight-alpha there), rather than one of them merely being a copy-paste with
  a changed number.
- Same shared boilerplate pattern (`colourMatch`, `check`, `DrawAndSample`-style helper) as the other 5 blend-state
  files in this batch.

## Missing or Weak Tests

None identified for this file's narrow, well-defined scope.

## Positive Findings

- The premultiplication in the source data (`Color(128, 0, 0, 128)`, not the more obvious but incorrect
  `Color(255, 0, 0, 128)`) is done correctly and is exactly the detail that makes this test meaningfully different
  from a naive "just call AlphaBlend and check something blended" test.
- The file's header comment explicitly and correctly cross-references its sibling audit test rather than silently
  duplicating scope.

## Final Assessment

A small, precise, correctly-derived test with an accurate cross-reference to its complementary sibling file. No
defects found in either the test or the production code it exercises.
