# Audit: examples/sdlrenderer_blendstate_audit_test.cpp

## Metadata

- Source file: `examples/sdlrenderer_blendstate_audit_test.cpp` (168 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlrenderer` shard — Task 695's own audit test: proves `AlphaBlend` and
  `NonPremultiplied` are now distinguishable on `SDL_Renderer`.
- File type: standalone `Game`-subclass executable, CTest-registered (`SDL_Renderer_BlendState_Audit`,
  `cmake/Tests/SdlRendererTests.cmake`).
- XNA/FNA relevance: direct — `BlendState.AlphaBlend` vs `BlendState.NonPremultiplied` distinction, a
  well-documented, easy-to-conflate pair of presets in real XNA (only differ in whether `ColorSourceBlend` is
  `One` or `SourceAlpha`).
- FNA reference: `Graphics/States/BlendState.cs` lines 172-186 (`AlphaBlend`/`NonPremultiplied` presets).
- Related production code: `src/CNA/Internal/Backends/SdlRenderer/SdlGraphicsBackend.cpp`
  (`ToSdlBlendFactor`/`ToSdlBlendOperation`/`ApplyBlendState`, lines 648-697 — the code this test's own header
  comment says was fixed).

## Purpose

This is the regression test for the bug the file's own header comment (lines 1-47) describes in detail: prior to
Task 695, `SdlGraphicsBackend::ApplyBlendState` only special-cased 2 of the 4 XNA blend presets by their exact
`(colorSrcBlend, colorDstBlend)` pair (`Opaque`→`SDL_BLENDMODE_NONE`, `Additive`→`SDL_BLENDMODE_ADD`) and fell
through to a single generic `SDL_BLENDMODE_BLEND` constant for everything else — making `AlphaBlend`
(`colorSrc=One, colorDst=InverseSourceAlpha`) and `NonPremultiplied` (`colorSrc=SourceAlpha,
colorDst=InverseSourceAlpha`) indistinguishable, despite XNA defining genuinely different blend equations for
premultiplied vs. non-premultiplied source data. `SdlBlendStateAuditTest::DrawAndSample` (lines 93-109) draws a
straight-alpha (non-premultiplied) half-transparent red quad over green, once per `BlendState`, and 3 checks
assert: (a) `AlphaBlend` produces the well-known "over-bright" artifact for this deliberately-wrong-precondition
input, (b) `NonPremultiplied` produces the textbook-correct 50/50 blend for the same input, and (c) the two
results are now *meaningfully different* (the actual regression-guard for the bug).

## Executive Verdict

**Healthy** — this audit independently re-derived all three checks' expected values by hand and confirmed the
current `ToSdlBlendFactor`/`ApplyBlendState` implementation (`SdlGraphicsBackend.cpp` lines 648-697) produces
exactly these values via `SDL_ComposeCustomBlendMode`, matching the file's own account of the fix. This is also
independently corroborated by `git log` (`ce15e028`/history shows Task 695 predates this test's own commit) and
by cross-reading the actual current `ToSdlBlendFactor` switch, which does correctly map all 10 representable
`Blend` values 1:1 rather than falling through to one generic mode.

## Checklist Results

### API / XNA / FNA parity

Both presets' definitions in `BlendState.cpp` (lines 7-8) exactly match FNA's `BlendState.cs` (lines 172-186):
`AlphaBlend={One,One,InverseSourceAlpha,InverseSourceAlpha}`,
`NonPremultiplied={SourceAlpha,SourceAlpha,InverseSourceAlpha,InverseSourceAlpha}` — differing only in the source
factor, exactly as this test's own framing states. `ToSdlBlendFactor`'s switch (lines 648-668) maps
`Blend::One`→`SDL_BLENDFACTOR_ONE` (case 0), `Blend::SourceAlpha`→`SDL_BLENDFACTOR_SRC_ALPHA` (case 4), and
`Blend::InverseSourceAlpha`→`SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA` (case 5) — all three factors this test actually
exercises are correctly, distinctly mapped, confirming the "no longer both fall through to the same constant"
claim.

### Behavioral correctness

Hand-derivation, source `Color(255,0,0,128)` (straight-alpha, i.e. NOT premultiplied — `srcA=128/255≈0.502`) over
`kGreen=(0,255,0)`:
- **AlphaBlend** (`colorSrc=One, colorDst=InverseSourceAlpha`): `dst = src*1 + bg*(1-srcA) = (255,0,0) +
  (0,255,0)*0.498 ≈ (255, 127, 0)` — matches the test's expected `Color(255,127,0,255)` (line 133) exactly. This
  is the "over-bright" artifact: because `AlphaBlend` assumes the source is already scaled by its own alpha but
  this source is NOT, the full, un-scaled `255` red passes straight through, added to a partially-surviving green
  background.
- **NonPremultiplied** (`colorSrc=SourceAlpha, colorDst=InverseSourceAlpha`): `dst = src*srcA + bg*(1-srcA) =
  (255,0,0)*0.502 + (0,255,0)*0.498 ≈ (128, 127, 0)` — matches the test's expected `Color(128,127,0,255)`
  (line 138) exactly. This is the textbook-correct straight-alpha blend, since `NonPremultiplied` correctly scales
  the source by its own alpha internally.
- **Distinguishability** (lines 143-146): `|255-128|=127 > 50` — a robust, tolerance-free-in-spirit
  discriminating margin (the two results differ by more than half the channel range), not a borderline pass.

All three checks independently re-derived and confirmed numerically correct — this is a genuinely strong,
non-boilerplate regression test.

### Logic

The three-check structure (isolated AlphaBlend value, isolated NonPremultiplied value, then their difference) is
methodologically sound: it separately verifies "each preset individually computes its own textbook-correct
formula for its own precondition" from "the two presets are not accidentally aliased to the same SDL blend mode,"
which are two different failure modes a regression could reintroduce independently (e.g., a future change could
make both presets correct in isolation yet still alias them to the same underlying constant by coincidence for a
specific input — the explicit difference check guards against exactly that).

### C++ correctness

`Color(std::abs((int)alphaBlendResult.getRProperty() - (int)nonPremulResult.getRProperty()), 0, 0, 255)` (line 146)
is used only for diagnostic `printf` output in `check()`, not for the pass/fail decision itself (`distinguishable`
is the actual boolean) — a slightly indirect way to smuggle a diagnostic number through the `Color got` parameter
of the shared `check()` helper, but not a correctness issue since the boolean predicate is computed independently
beforehand.

### Robustness

`PresentationMode::NativeBackBuffer` correctly set (line 157), consistent with the rest of this batch's Task 915
rationale.

### Testing

Strong test: not a "compiles and doesn't crash" check, but a genuine before/after regression guard tied to a
specific, previously-real bug, with an explicit discriminating assertion (`distinguishable`) rather than relying
solely on two independently-plausible-looking absolute values.

## Detailed Findings

None at HIGH/CRITICAL severity. No MEDIUM/LOW findings either.

## Cross-File Observations

- This file's header comment (lines 1-47) is unusually detailed and itself constitutes a mini design-decision
  record (documents which of XNA's 13 `Blend` values have no SDL equivalent and why throwing was chosen over
  silent substitution) — this narrative was independently checked against the current `ToSdlBlendFactor`
  implementation and found accurate, not stale.
- Complements `sdlrenderer_blendstate_alphablend_test.cpp` (feeds `AlphaBlend` genuinely premultiplied data,
  proving the *correct*-precondition case) — together the two files cover both the correct-input and
  wrong-input-but-still-correctly-computed behavior of `AlphaBlend`, a thorough pairing.

## Missing or Weak Tests

None identified — the 3-check structure is appropriately scoped to this specific regression.

## Positive Findings

- All three numeric assertions independently re-derived and confirmed exactly correct, not just self-consistent.
- The explicit "are these two now different" check is a well-designed regression guard against exactly the kind
  of future refactor that could silently re-introduce blend-mode aliasing without either individual value looking
  obviously wrong in isolation.
- Header comment's historical/technical narrative checked against current code and found accurate.

## Final Assessment

A well-constructed, evidence-backed regression test for a real, well-documented prior bug. No defects found in
either the test file or the production blend-mapping code it exercises.
