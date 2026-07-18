# Audit: examples/bgfx_blendstate_nonpremultiplied_test.cpp

## Metadata

- Source file: `examples/bgfx_blendstate_nonpremultiplied_test.cpp` (113 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `BlendState.NonPremultiplied` preset pixel test
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_bgfx_test(cna_test_bgfx_blendstate_nonpremultiplied …)` /
  `cna_register_backend_test(NAME Bgfx_BlendState_NonPremultiplied …)`,
  `cmake/Tests/BgfxTests.cmake:742-744`).
- XNA/FNA relevance: direct — `Microsoft.Xna.Framework.Graphics.BlendState.NonPremultiplied`.
- FNA reference: `src/Graphics/States/BlendState.cs:180-186` (`NonPremultiplied = new BlendState(
  "BlendState.NonPremultiplied", Blend.SourceAlpha, Blend.SourceAlpha, Blend.InverseSourceAlpha,
  Blend.InverseSourceAlpha)`).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/BlendState.cpp:8` (identical preset
  values), `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp` (`ApplyBlendState`, lines
  1572-1596).

## Purpose

Clears to green `(0,255,0,255)`, draws a quad whose vertex color is a **raw, non-premultiplied**
50%-alpha red `(255,0,0,128)`, and checks `BlendState::NonPremultiplied`'s equation
(`src*srcA + dst*(1-srcA)`) produces the expected `(≈128,≈127,≈0)` — the complementary case to this
batch's `bgfx_blendstate_alphablend_test.cpp`, which uses an already-premultiplied source and a
different (`One`-based) color-source factor.

## Executive Verdict

**Healthy** — `BlendState::NonPremultiplied`'s factors were confirmed identical to FNA's preset, and
the expected math was independently re-derived and matches. This test is intentionally the mirror
image of the `AlphaBlend` sibling test (same target RGB output reached via a genuinely different
equation and a differently-encoded source color), which this audit confirms is a real, deliberate,
and effective design for catching a `ColorSourceBlend` mixup between the two presets.

## Checklist Results

### API / XNA / FNA parity
`BlendState::NonPremultiplied` (`BlendState.cpp:8`): `ColorSourceBlend=AlphaSourceBlend=
Blend::SourceAlpha`, `ColorDestinationBlend=AlphaDestinationBlend=Blend::InverseSourceAlpha` — matches
`BlendState.cs:180-186` exactly. Differs from `AlphaBlend` (this batch's sibling report) *only* in
`ColorSourceBlend`/`AlphaSourceBlend` (`SourceAlpha` here vs. `One` there) — independently confirmed
by diffing both FNA preset definitions directly.

### Behavioral correctness
Re-derived: `fragA = 128/255 = 0.502`. `R = 255*0.502 + 0*(1-0.502) ≈ 128`. `G = 0*0.502 + 255*
(1-0.502) ≈ 127`. Both fall well inside `rInBand`/`gInBand` (`[110,145]`, lines 85-86). Confirmed via
`BasicEffect::FillGpuDrawParams()` that `VertexColorEnabled=true` with default `DiffuseColor`/`Alpha`
passes the raw, un-premultiplied vertex color through unmodified to the blend unit — the
`SourceAlpha` factor genuinely does the alpha-scaling work here (unlike the `AlphaBlend` sibling,
where the vertex color is *already* scaled and the factor is a plain `One`).

### Logic
Single `Clear()`+`Draw()`+`GetBackBufferData()` per run (`done_` guard) — consistent with, and
empirically verified alongside, this shard's other 3 preset tests per commit `cf2d5eb3`'s regression
note.

### C++ correctness
No lifetime issues; locals fully consumed before scope exit.

### Robustness
Same `DepthStencilState`-based depth-disable substitution (lines 57-59) for the confirmed-throwing
`SetDepthTestEnabled` on Bgfx.

### Testing
Unlike the `AlphaBlend` sibling test, this file has **no explicit "rule out the opposite bug" check**
(no equivalent of that file's `notDoubleMultiplied` assertion) — it only asserts the output lands in
the expected band, without a companion assertion proving the output is *not* what `AlphaBlend`'s own
equation would produce from the *same* raw source color. Concretely: if this shader/blend path
accidentally used `AlphaBlend`'s equation (`src*1 + dst*(1-srcA)`) on this file's raw
`(255,0,0,128)` source instead of `NonPremultiplied`'s own (`src*srcA + dst*(1-srcA)`), the result
would be `R = 255*1 + 0*0.498 = 255`, which is clearly outside `[110,145]` and would still correctly
fail `rInBand` — so in practice this file's single-band check *does* still catch the specific
"wrong-preset-equation" swap for this input, just via a simple range check rather than a second,
explicitly-labeled discriminator like the `AlphaBlend` sibling's. Not a functional gap, just a slightly
less self-documenting test-design choice than its sibling.

## Detailed Findings

None. No HIGH/CRITICAL/MEDIUM findings.

## Cross-File Observations

- Forms a deliberate discriminating pair with `bgfx_blendstate_alphablend_test.cpp` (this batch):
  both target the same blended RGB output (`~128,~127,~0`) from differently-encoded source colors via
  genuinely different equations, specifically to catch a `ColorSourceBlend` mixup between the two
  adjacent presets — independently confirmed this is an effective design (see this file's own
  Testing section above for the specific arithmetic proving a swapped equation would fail here too).
- Shares the `RasterizerState::CullNone` workaround (line 78) and quad vertex ordering with every
  other file in this batch; independently re-confirmed CCW winding via the same cross-product method
  applied to this batch's other files.

## Missing or Weak Tests

A slightly stronger version of this test (not a defect, a possible future enhancement) would add an
explicit `notWrongEquation`-style named assertion mirroring the `AlphaBlend` sibling's
`notDoubleMultiplied` check, purely for self-documentation/diagnostic-message clarity on failure —
functionally the existing `rInBand` check already has adequate discriminating power for this specific
input, as shown above.

## Positive Findings

- Correctly distinguishes `NonPremultiplied` from `AlphaBlend` in its own header comment (lines
  11-13: *"multiplies a RAW (non-premultiplied) source colour by alpha itself, unlike
  BlendState::AlphaBlend, which expects an already-premultiplied source"*) — an accurate, verified
  statement of the real difference between the two presets.
- Test constants (`255,0,0,128` here vs. `128,0,0,128` in the `AlphaBlend` sibling) are deliberately
  chosen so both tests converge on the same expected output through different equations, which this
  audit independently confirmed is mathematically sound, not a coincidence.

## Final Assessment

A correct, well-derived test with no defects found. Slightly less self-documenting than its
`AlphaBlend` sibling (no explicitly-named "rule out the opposite equation" assertion) but no less
effective in practice for the specific input values chosen.
