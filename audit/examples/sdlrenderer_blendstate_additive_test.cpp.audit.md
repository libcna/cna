# Audit: examples/sdlrenderer_blendstate_additive_test.cpp

## Metadata

- Source file: `examples/sdlrenderer_blendstate_additive_test.cpp` (130 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlrenderer` shard — `BlendState::Additive` pixel test
- File type: standalone `Game`-subclass executable, CTest-registered (`cna_test_sdl_blendstate_additive` /
  `SDL_Renderer_BlendState_Additive`, per `cmake/Tests/SdlRendererTests.cmake`).
- XNA/FNA relevance: direct — `BlendState.Additive`, `SpriteBatch.Draw`/`Begin`/`End`, `GraphicsDevice.Clear`/
  `GetBackBufferData`.
- FNA reference: `Graphics/States/BlendState.cs` (lines 164-170: `Additive = new BlendState("BlendState.Additive",
  Blend.SourceAlpha, Blend.SourceAlpha, Blend.One, Blend.One)`).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/BlendState.cpp` (preset definitions, line 6),
  `src/CNA/Internal/Backends/SdlRenderer/SdlGraphicsBackend.cpp` (`ToSdlBlendFactor`/`ToSdlBlendOperation`/
  `ApplyBlendState`, lines 648-697).

## Purpose

`SdlBlendStateAdditiveTest` (class, lines 52-123) is a 2-check pixel test proving `BlendState::Additive`'s
saturation behavior specifically: (1) a half-alpha (128/255) draw over a dim background produces the correct
non-saturating additive sum, and (2) a full-alpha, bright draw over an already-bright background produces a result
that clamps to 255 rather than wrapping or otherwise misbehaving. `DrawAndSample` (lines 68-83) is the shared
helper: clear to a background colour, `Begin(SpriteSortMode::Deferred, BlendState::Additive, PointClamp, ...)`,
draw a 1x1 white texture tinted by `drawColor` over a 16x16 destination rect, `End()`, then read back the centre
pixel via `GetBackBufferData`.

## Executive Verdict

**Healthy.** Both checks are correctly derived, correctly target the specific "saturation" behavior the file's own
name and header comment claim to test (not just "additive blending happens"), and the underlying production code
(`BlendState::Additive`'s factor tuple, `ToSdlBlendFactor`/`ApplyBlendState`'s mapping) independently verified
correct against both FNA's `BlendState.cs` and SDL3's `SDL_ComposeCustomBlendMode` contract.

## Checklist Results

### API / XNA / FNA parity

`BlendState::Additive`'s C++ definition (`BlendState.cpp` line 6: `{colorSrc=SourceAlpha, alphaSrc=SourceAlpha,
colorDst=One, alphaDst=One}`) is byte-for-byte identical to FNA's own `Additive` preset (`BlendState.cs` lines
164-170) — same four `Blend` values in the same (colorSrc, alphaSrc, colorDst, alphaDst) constructor-argument
order. The file's own header comment states this preset resolves to `dst = src*srcA + dst*1`, which is exactly
`ColorSourceBlend=SourceAlpha, ColorDestinationBlend=One` applied to the colour channel — correct.

### Behavioral correctness

Re-derived both checks by hand:
- Check 1 (non-saturating, lines 102-104): `src=(100,0,0)`, `srcA=128/255≈0.502`, `dst_bg=(50,0,0)`.
  `dst = src*srcA + dst_bg*1 = (100*0.502, 0, 0) + (50,0,0) ≈ (100.2, 0, 0)`. The test's expected value
  `Color(100,0,0,255)` with tolerance 15 is correct arithmetic, not just a plausible-looking round number.
- Check 2 (saturating, lines 106-108): `src=(200,0,0)`, `srcA=1.0`, `dst_bg=(200,0,0)`.
  `dst = 200*1 + 200 = 400 -> clamp to 255`. Expected `Color(255,0,0,255)` tolerance 5 is correct, and a tight
  tolerance is appropriate here since clamping is an exact, not approximate, operation — a real bug that clamped
  to, say, 250 or wrapped to 144 (400 mod 256) would be caught by this tight tolerance.

Both checks are genuinely discriminating: check 1 could not pass under a broken mapping that (pre-Task-695)
resolved `Additive` incorrectly, and check 2 specifically stresses the boundary the file's own name targets
(saturation), not merely "additive blending occurred."

### Logic

`colourMatch` (lines 45-50) compares only R/G/B, ignoring alpha — appropriate here since `GetBackBufferData`'s
returned alpha for an opaque backbuffer read is not the subject under test.

### C++ correctness

`Color::getRProperty()` etc. return `bytecs`/`uint8_t`; the `(int)` casts in `colourMatch` (lines 47-49) correctly
avoid unsigned-underflow wraparound in `std::abs(...)` on an unsigned subtraction — a genuine, correctly-applied
defensive cast, not superfluous.

### Robustness

`PresentationMode::NativeBackBuffer` is explicitly set in the constructor (line 119) with a comment (lines 21-23)
correctly citing the Task 915 finding (`SDL_RenderReadPixels` operates in physical coordinates, and the default
`FixedHeightDynamicWidth` presentation mode does not map 1:1) — this is the same requirement independently
verified in the `SdlGraphicsBackend.cpp` audit's own `ReadBackbuffer` discussion; the test correctly opts out of
the mismatch rather than risking an off-by-scaling misread.

### Testing

This file is itself a test; see Behavioral correctness above for the assessment of what it actually verifies.
No boundary/error-path gaps found for the specific "Additive saturation" scope this file claims — a 0-alpha or
255-alpha-exact case is not separately tested, but the two chosen points (mid-alpha non-saturating,
full-alpha saturating) are the two behaviorally distinct regimes that matter for this preset.

## Detailed Findings

None at HIGH/CRITICAL severity. No MEDIUM/LOW findings identified either — this is one of the cleaner, more
tightly-scoped test files in this batch.

## Cross-File Observations

- Shares its `colourMatch`/`check`/constructor-boilerplate pattern verbatim with the other blend-state test files
  in this batch (`sdlrenderer_blendstate_alphablend_test.cpp`, `..._opaque_test.cpp`,
  `..._nonpremultiplied_test.cpp`, `..._audit_test.cpp`, `..._custom_test.cpp`) — consistent, low-duplication-risk
  boilerplate (a `static bool colourMatch(...)` free function copy-pasted per file rather than a shared test
  header) that a shared `tests/` helper could deduplicate, but this is a maintainability observation, not a
  correctness defect, and consistent with this shard's overall structure of self-contained single-file example
  executables (each independently buildable/runnable as its own CTest target).
- The additive-specific SDL mapping path (`ToSdlBlendFactor`/`ApplyBlendState`) was already audited in depth in
  `audit/src/CNA/Internal/Backends/SdlRenderer/SdlGraphicsBackend.cpp.audit.md` — this test file exercises exactly
  that code path for the `Additive` preset and adds no new production-code risk beyond what that audit already
  covered.

## Missing or Weak Tests

None beyond the minor observation that a `srcA=0` (fully transparent) additive draw — which should leave the
background completely untouched — is not separately checked, though this is a low-value addition given `Additive`'s
formula makes that case a trivial degenerate of check 1's own math.

## Positive Findings

- Both expected constants were independently re-derived by this audit and found numerically correct, not just
  self-consistent with the file's own comments.
- Correctly requires `PresentationMode::NativeBackBuffer` with an accurate citation of the Task 915
  physical/logical coordinate mismatch.
- The two checks are well-chosen to isolate the specific "saturation" behavior the file's name promises, rather
  than testing generic additive blending only.

## Final Assessment

A small, correctly-scoped, evidence-backed pixel test. No defects found; production code (`BlendState::Additive`,
`ToSdlBlendFactor`/`ApplyBlendState`) independently confirmed correct against the FNA reference.
