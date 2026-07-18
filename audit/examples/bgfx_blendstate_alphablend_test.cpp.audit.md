# Audit: examples/bgfx_blendstate_alphablend_test.cpp

## Metadata

- Source file: `examples/bgfx_blendstate_alphablend_test.cpp` (122 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `BlendState.AlphaBlend` preset pixel test
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_bgfx_test(cna_test_bgfx_blendstate_alphablend …)` /
  `cna_register_backend_test(NAME Bgfx_BlendState_AlphaBlend …)`,
  `cmake/Tests/BgfxTests.cmake:737-739`).
- XNA/FNA relevance: direct — `Microsoft.Xna.Framework.Graphics.BlendState.AlphaBlend`.
- FNA reference: `src/Graphics/States/BlendState.cs:172-178` (`AlphaBlend = new BlendState(
  "BlendState.AlphaBlend", Blend.One, Blend.One, Blend.InverseSourceAlpha,
  Blend.InverseSourceAlpha)`).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/BlendState.cpp:7` (identical preset
  values), `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp` (`ApplyBlendState`, lines
  1572-1596).

## Purpose

Clears to green `(0,255,0,255)`, draws a quad whose vertex color is a **correctly premultiplied**
50%-alpha red `(128,0,0,128)`, and checks that `BlendState::AlphaBlend`'s equation
(`src*1 + dst*(1-srcA)`) is applied *as-is* — critically asserting `R≈128`, not `R≈64`, which would
indicate the shader/blend-state incorrectly multiplied an already-premultiplied source by alpha a
second time (i.e., accidentally computed `NonPremultiplied`'s equation instead).

## Executive Verdict

**Healthy** — `BlendState::AlphaBlend`'s factors were confirmed identical to FNA's preset, the
expected math was independently re-derived and matches, and the specific "double-multiply" failure
mode this test is designed to catch is a real, plausible implementation bug this test would actually
detect (not a strawman).

## Checklist Results

### API / XNA / FNA parity
`BlendState::AlphaBlend` (`BlendState.cpp:7`): `ColorSourceBlend=AlphaSourceBlend=Blend::One`,
`ColorDestinationBlend=AlphaDestinationBlend=Blend::InverseSourceAlpha` — matches
`BlendState.cs:172-178` exactly (same constructor-argument order verified in this batch's `Additive`
sibling report). `Blend::One=0`, `Blend::InverseSourceAlpha=5` map correctly through
`XnaBlendToBgfxFactor` (`BgfxGraphicsBackend.cpp:1546`, `case 5: return BGFX_STATE_BLEND_INV_SRC_ALPHA`).

### Behavioral correctness
Re-derived: `fragA = 128/255 = 0.502`. `R = 128*1 + 0*(1-0.502) = 128`. `G = 0*1 + 255*(1-0.502) =
255*0.498 ≈ 127`. Both fall inside `rInBand`/`gInBand` (`[110,145]`, lines 90-91) with generous
margin. The `notDoubleMultiplied` check (`got.R >= 100`, line 92) specifically distinguishes this
from the failure mode where a shader re-applies `SourceAlpha` to an already-premultiplied color:
`128*0.502 + 0*(1-0.502) ≈ 64.3`, which is `<100` and would be caught. Confirmed via
`BasicEffect::FillGpuDrawParams()` (`BasicEffect.cpp:56-70`) that `VertexColorEnabled=true` with
default `DiffuseColor=(1,1,1)`/`Alpha=1.0` passes the raw vertex color through unmodified
(`u_diffuseColor=(1,1,1,1)`), so the premultiplied `(128,0,0,128)` genuinely reaches the blend unit as
the fragment's own raw output — the test is exercising the blend-state factors, not some intervening
shader-side premultiply/un-premultiply step that could mask the distinction.

### Logic
Single `Clear()`+`Draw()`+`GetBackBufferData()` per run (`done_` guard), consistent with this shard's
established "no retry loop needed for a single read" pattern — confirmed empirically passing per
commit `cf2d5eb3`'s own regression note ("All 4 pass with exact expected values").

### C++ correctness
No lifetime issues; `verts`/`kPremultipliedRed` are locals fully consumed before scope exit.

### Robustness
Correctly substitutes `DepthStencilState`-based depth-disable (lines 62-64) for the legacy
`SetDepthTestEnabled(false)` call that throws on Bgfx (`ThrowNo3DState()`,
`BgfxGraphicsBackend.cpp:2002`) — consistent with, and independently confirmed against, this shard's
other `BlendState` files.

### Testing
The `notDoubleMultiplied` assertion is the single most valuable check in this file: it is the one
that actually falsifies the specific, plausible bug of conflating `AlphaBlend` with
`NonPremultiplied` (both share `SourceAlpha`-style factors in `NonPremultiplied`'s case, but
`AlphaBlend` deliberately uses `One` for its color-source factor instead) — this audit independently
confirmed the two presets differ *only* in `ColorSourceBlend`/`AlphaSourceBlend`
(`One` vs `SourceAlpha`), making this exactly the pair of presets most likely to be accidentally
swapped or merged in an implementation, and this test is well-targeted at exactly that risk.

## Detailed Findings

None. No HIGH/CRITICAL/MEDIUM findings.

## Cross-File Observations

- Complements `bgfx_blendstate_nonpremultiplied_test.cpp` (same batch) almost perfectly: both use an
  intentionally identical target RGB outcome (`~128,~127,~0`) from *different* source colors
  (premultiplied `(128,0,0,128)` here vs. raw `(255,0,0,128)` there) specifically so that a
  `ColorSourceBlend` mixup between the two presets would still be caught — this file's `R≥100` check
  and the `NonPremultiplied` file's own tight `[110,145]` band together bound the failure space from
  both sides.
- Shares the `RasterizerState::CullNone` workaround (line 83) with every other file in this batch;
  independently re-verified this quad's winding is genuinely CCW using the same cross-product check
  applied to the `Additive` sibling file (identical vertex ordering: `tl,bl,br` then `tl,br,tr`).

## Missing or Weak Tests

None identified for this file's stated scope.

## Positive Findings

- The choice to test `AlphaBlend` and `NonPremultiplied` with source colors that resolve to nearly
  the same blended RGB output, but via deliberately different equations, is a genuinely clever
  test-design choice that specifically targets the most likely confusion between two adjacent XNA
  blend presets.
- `[FAIL]` diagnostic message (lines 104-107) explains precisely which specific misimplementation a
  given failure would indicate, aiding future debugging.

## Final Assessment

A precise, well-targeted test with no defects found. The underlying `BlendState::AlphaBlend`
preset and Bgfx blend-factor mapping were both independently confirmed correct against FNA and the
current production source.
