# Audit: examples/sdlrenderer_blendstate_nonpremultiplied_test.cpp

## Metadata

- Source file: `examples/sdlrenderer_blendstate_nonpremultiplied_test.cpp` (115 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlrenderer` shard — `BlendState::NonPremultiplied` dedicated pixel test.
- File type: standalone `Game`-subclass executable, CTest-registered (`SDL_Renderer_BlendState_NonPremultiplied`,
  `cmake/Tests/SdlRendererTests.cmake`).
- XNA/FNA relevance: direct — `BlendState.NonPremultiplied`'s straight-alpha blend contract.
- FNA reference: `Graphics/States/BlendState.cs` (lines 180-186: `NonPremultiplied = new BlendState(...,
  Blend.SourceAlpha, Blend.SourceAlpha, Blend.InverseSourceAlpha, Blend.InverseSourceAlpha)`).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/BlendState.cpp` (line 8),
  `src/CNA/Internal/Backends/SdlRenderer/SdlGraphicsBackend.cpp` (`ApplyBlendState`, lines 683-697).

## Purpose

Single-check dedicated verification of `BlendState::NonPremultiplied`: draws a straight (non-premultiplied)
half-transparent (alpha=128) red quad — full-brightness `R=255` with `alpha=128`, i.e. genuinely NOT scaled by its
own alpha — over a solid blue background, and checks the result matches the textbook straight-alpha blend
equation `dst = src*srcA + dst*(1-srcA)`.

## Executive Verdict

**Healthy.** The single check is correctly derived and the file's own header comment accurately notes this
preset's factor tuple happened to already be correctly mapped even *before* Task 695's fix (since
`SourceAlpha`/`InverseSourceAlpha` collapses to plain `SDL_BLENDMODE_BLEND`, the old fallback default) — this
audit independently confirmed that claim is consistent with the bug description in the sibling
`sdlrenderer_blendstate_audit_test.cpp`'s own header comment.

## Checklist Results

### API / XNA / FNA parity

`BlendState::NonPremultiplied` (`BlendState.cpp` line 8: `colorSrc=SourceAlpha, alphaSrc=SourceAlpha,
colorDst=InverseSourceAlpha, alphaDst=InverseSourceAlpha`) matches FNA's own preset (`BlendState.cs` lines
180-186) exactly, same 4 values in the same order.

### Behavioral correctness

Hand-derivation: `src=(255,0,0)`, `srcA=128/255≈0.502`, `dst_bg=(0,0,255)`.
`dst = src*srcA + dst_bg*(1-srcA) = (255,0,0)*0.502 + (0,0,255)*0.498 ≈ (128, 0, 127)` — matches the test's own
expected `Color(128, 0, 127, 255)` (line 90) with tolerance 15, independently confirmed correct.

Note this test and `sdlrenderer_blendstate_alphablend_test.cpp` draw structurally near-identical scenes (same
blue background, same red-quad size/position) but feed the source colour differently — this file uses straight
`Color(255, 0, 0, 128)` (line 83), the `AlphaBlend` sibling file uses pre-scaled `Color(128, 0, 0, 128)` — and
both land on the *same* final expected pixel `(128, 0, 127)`, which is the expected and correct outcome: the two
different presets, each fed the input format they are individually designed for, converge on the same textbook
answer, precisely the point both files are individually making.

### Logic

Single texture, single draw, single sample; no additional branching.

### C++ correctness

No issues; same `colourMatch` helper pattern (R/G/B only, unsigned-safe `(int)` casts) as the rest of this batch.

### Robustness

`PresentationMode::NativeBackBuffer` correctly set (line 105), same Task 915 rationale as the rest of the batch.

### Testing

Correctly scoped as "dedicated, focused verification" per its own header comment (line 10) — a single, precise
check rather than an attempt to re-prove the aliasing bug the sibling audit test already covers.

## Detailed Findings

None at HIGH/CRITICAL/MEDIUM/LOW severity — a small, correct, well-scoped file.

## Cross-File Observations

- Forms a deliberate, verified-correct trio with `sdlrenderer_blendstate_alphablend_test.cpp` (premultiplied
  input → `AlphaBlend`) and `sdlrenderer_blendstate_audit_test.cpp` (straight input fed to both presets,
  demonstrating divergence) — this audit confirms all three files' numeric constants are mutually consistent with
  each preset's respective formula, not just individually plausible in isolation.

## Missing or Weak Tests

None identified for this file's narrow, well-defined scope.

## Positive Findings

- Correctly identifies (in its own header comment) that this specific preset's mapping was already correct even
  before Task 695's fix — an accurate, non-overstated historical claim, cross-checked against the sibling audit
  test's own bug narrative.
- Expected constant independently re-derived and confirmed exactly correct.

## Final Assessment

A small, correctly-scoped, verified-correct dedicated preset test with no defects found.
