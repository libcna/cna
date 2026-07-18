# Audit: examples/bgfx_blendstate_opaque_test.cpp

## Metadata

- Source file: `examples/bgfx_blendstate_opaque_test.cpp` (110 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `BlendState.Opaque` preset pixel test
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_bgfx_test(cna_test_bgfx_blendstate_opaque …)` /
  `cna_register_backend_test(NAME Bgfx_BlendState_Opaque …)`,
  `cmake/Tests/BgfxTests.cmake:732-734`).
- XNA/FNA relevance: direct — `Microsoft.Xna.Framework.Graphics.BlendState.Opaque`.
- FNA reference: `src/Graphics/States/BlendState.cs:188-194` (`Opaque = new BlendState(
  "BlendState.Opaque", Blend.One, Blend.One, Blend.Zero, Blend.Zero)`).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/BlendState.cpp:9` (identical preset
  values), `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp:1576-1581` (the "Opaque fast path"
  short-circuit in `ApplyBlendState`).

## Purpose

Clears to green `(0,255,0,255)`, draws a *partially-transparent* red quad `(255,0,0,128)` with
`BlendState::Opaque`, and asserts the result is **pure red with zero green bleed-through** — proving
both that the destination is fully discarded (`ColorDestinationBlend=Zero`) and, critically, that the
source's own low alpha (128) has **no blending effect whatsoever** under this preset (unlike
`AlphaBlend`/`NonPremultiplied`, which would produce a visible red/green mix at the same alpha).

## Executive Verdict

**Healthy** — `BlendState::Opaque`'s factors were confirmed identical to FNA's preset, and this
audit independently verified the specific implementation risk this test targets (a blend-enable path
that keys off "is alpha < 255" rather than the state's own actual factors) is a real, plausible bug
class this test would catch, by tracing the exact fast-path check in current production code.

## Checklist Results

### API / XNA / FNA parity
`BlendState::Opaque` (`BlendState.cpp:9`): `ColorSourceBlend=AlphaSourceBlend=Blend::One`,
`ColorDestinationBlend=AlphaDestinationBlend=Blend::Zero` — matches `BlendState.cs:188-194` exactly.

### Behavioral correctness
Re-derived: output `= fragColor*1 + dest*0 = fragColor` unconditionally, regardless of `fragColor`'s
own alpha value — the destination (green) is discarded entirely and the source's alpha=128 must not
cause any blending. `isPureRed` (`R≥200, G≤50, B≤50`, line 83) has a wide margin from both the
"correct" outcome `(255,0,0)` and the "buggy blend" outcome that would occur if `Opaque` were
mistakenly treated as `AlphaBlend`-like (`R=255*1+0*(1-0.502)=255`, `G=0*1+255*(1-0.502)≈127` — this
*would* fail `G≤50`, correctly discriminating the exact bug this test targets). Confirmed
`ApplyBlendState`'s "Opaque fast path" (`BgfxGraphicsBackend.cpp:1576-1581`,
`if (colorSrcBlend==0 && colorDstBlend==1 && alphaSrcBlend==0 && alphaDstBlend==1) blendFlags_=0;`)
correctly keys off the *state's own 4 factors* (all four checked, matching Task 923's widening of
this check — see this shard's `bgfx_blendstate_separate_functions_test.cpp` report for detail on
that fix), not off any property of the drawn fragment's alpha value — i.e. this fast path is
correctly state-driven, not (as this test's own design implicitly guards against) accidentally
draw-data-driven.

### Logic
Single `Clear()`+`Draw()`+`GetBackBufferData()` per run (`done_` guard) — consistent with, and
empirically verified alongside, this shard's other 3 preset tests (`cf2d5eb3`'s regression note:
"All 4 pass with exact expected values").

### C++ correctness
No lifetime issues; locals fully consumed before scope exit.

### Robustness
Same `DepthStencilState`-based depth-disable substitution (lines 55-57) for the confirmed-throwing
`SetDepthTestEnabled` on Bgfx.

### Testing
The choice of a *partially transparent* source color (alpha=128, not 255) for an `Opaque`-preset test
is the single most important design decision in this file — a test using a fully-opaque source would
pass trivially even under a subtly wrong blend-enable implementation that only special-cased
`alpha==255`, whereas this alpha=128 source specifically forces any such alpha-value-driven shortcut
to reveal itself as a visible green blend if wrong.

## Detailed Findings

None. No HIGH/CRITICAL/MEDIUM findings.

## Cross-File Observations

- Complements `bgfx_blendstate_additive_test.cpp` (this batch) in verifying the same
  "Opaque fast path" boolean-flag short-circuit from the opposite direction: `Additive`'s report
  confirms the fast path does *not* misfire on a blending preset with `alphaSrcBlend≠One`, while this
  file confirms the reverse — that the fast path *does* correctly activate for the true zero-blend
  case, and that doing so genuinely discards a translucent fragment's blending (not just a coincidence
  of the specific alpha value chosen).
- Shares the `RasterizerState::CullNone` workaround (line 76) and quad vertex ordering with every
  other file in this batch; independently re-confirmed CCW winding via the same cross-product method.

## Missing or Weak Tests

None identified for this file's stated scope.

## Positive Findings

- The deliberate use of `alpha=128` (not `255`) for an `Opaque`-preset test is a genuinely
  discriminating choice this audit confirmed would catch a real, plausible implementation shortcut
  (alpha-value-driven blend-enable heuristics) that a naive `Opaque` test using a fully-opaque source
  would miss entirely.
- Header comment (lines 14-19) states the exact underlying blend equation and its implication
  precisely, matching this audit's own independent re-derivation.

## Final Assessment

A small, sharply-targeted test with no defects found. The specific alpha=128 source-color choice is a
real strength of this test's design, independently confirmed to catch a plausible and non-obvious
implementation bug class.
