# Audit: examples/easygl_texture_filter_point_vs_linear_test.cpp

## Metadata

- Source file: `examples/easygl_texture_filter_point_vs_linear_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — Task 297, `TextureFilter::Point` vs. `Linear` on a 3D
  stock effect, both magnification and minification
- File type: hand-rolled `Game`-subclass executable, CTest-registered as
  `cna_test_easygl_texture_filter_point_vs_linear` (`cmake/Tests/EasyGLTests.cmake:1304-1306`). The
  identical source is also reused verbatim as `cna_test_vulkan_texture_filter_point_vs_linear`
  (`cmake/Tests/VulkanTests.cmake:74-79`) — again, the `easygl_` prefix is a naming artifact, the
  file itself is backend-agnostic public-API code.
- XNA/FNA relevance: `TextureFilter::Point`/`Linear`, `DualTextureEffect`, `SamplerState` — real XNA
  4.0 API.
- Related production code: `EasyGLGraphicsBackend::ApplySamplerState`
  (`src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp:2055-2139`, TextureFilter→GL min/mag
  filter switch).

## Purpose

Draws four side-by-side columns on one frame — {magnification, minification} × {Point, Linear} —
each with its own `SamplerState`, samples one pixel from each column at its texture's own texel
boundary, and asserts Point columns are always a pure single-channel color while Linear columns are
always a genuine ~50/50 blend, regardless of on-screen scale.

## Executive Verdict

**Healthy.** All four column geometries, sample points, and predicate expectations were independently
re-derived and checked against the actual `ApplySamplerState` GL filter mapping; the test's own
central claim — that CNA's flat (non-mipmapped-by-default) filter mapping makes magnification and
minification share identical sampler math — was confirmed directly in the backend switch statement,
not just taken on faith from the header comment.

## Checklist Results

### Behavioral correctness
- Column 1/2 (magnification, `tex2_`, 2 texels stretched across 256px): texel boundary at UV=0.5 maps
  to screen pixel `128`/`384` respectively (midpoints of `[0,256)`/`[256,512)`) — correct arithmetic,
  independently reconstructed from `DrawColumn`'s NDC computation (lines 129-130) and the sample
  points chosen in `Draw()` (lines 173-174).
- Column 3/4 (minification, `tex256_`, 256 alternating-R/G texels compressed into a 128px-wide
  column): `IsPure` is expected for **any** point-sampled location in this texture, not just the
  specific boundary chosen — since every texel is already a pure R or G value, `GL_NEAREST` always
  returns a pure color regardless of where it lands. The chosen sample points (`576`/`704`, column
  midpoints) are non-special but sufficient; the predicate's correctness does not actually depend on
  hitting an exact texel boundary the way the magnification/Linear case does. This is a slightly
  weaker geometric justification than the file's own header comment implies ("sampled exactly at its
  texture's texel boundary... Point sampling always returns one pure texel colour there"), but it does
  not make the test wrong — see Finding F1.
- `IsBlended` for column 4 (minification, `Linear`, no mipmaps): this audit independently confirmed
  via `ApplySamplerState`'s `default:` branch (lines 2106-2109, `filter` values other than 1-8 map to
  plain `Linear`/`Linear`, i.e. **not** `LinearMipmapLinear`) that minification-`Linear` and
  magnification-`Linear` truly share identical GL filter state in this backend — GL's non-mipmapped
  `GL_LINEAR` only ever interpolates the texels nearest the exact continuous UV coordinate, regardless
  of how many texels the minified region spans. At the exact texel-boundary UV the test samples
  (576/704's implicit texel index, effectively any boundary in the 256-texel-to-128px column), this
  correctly still yields a genuine 2-texel 50/50 blend — the test's own architectural claim (header
  comment lines 5-9) is accurate and independently verified against the real switch statement, not
  merely asserted.

### Logic
`MakeSampler`/`DrawColumn` helpers correctly reset `SamplerState` per column (line 112,
`device.getSamplerStatesProperty()[0] = MakeSampler(filter)`) before each `DualTextureEffect::Apply()`
— no state leakage between columns was found; each `DrawColumn` call is self-contained.

The `DiffuseColor=(0.5,0.5,0.5)` compensation (line 123) for the shader's `color.rgb *= 2` doubling is
the same pattern independently verified in the sibling golden-image test's audit
(`easygl_texture_filter_linear_golden_test.cpp.audit.md`) against the real EasyGL fragment shader
source — correct here too, applied uniformly to all four columns via the shared `DrawColumn` helper.

### Robustness
`IsBlended`/`IsPure` (lines 58-71) use disjoint numeric ranges (`90-165` for blended, `<=20`/`>=235`
for pure) with a clear dead zone between them — a sample that's neither would correctly fail both
predicates rather than silently passing one, and the pass/fail aggregation (`passCount == 4`, line
195) requires all four checks to individually pass.

## Detailed Findings

No CRITICAL/HIGH findings.

### F1 — Minification/Point sample point isn't actually a distinguishing "texel boundary" the way the header comment implies

- Severity: LOW
- Confidence: HIGH
- Category: test-coverage / documentation-accuracy
- Location/symbol: header comment lines 17-20 vs. `Draw()` lines 175-176 (`sample(576.0f)` /
  `sample(704.0f)`)
- Evidence: the header comment describes all four sample points uniformly as "exactly at its
  texture's texel boundary (the midpoint between two texel centers)." For the minification columns
  this framing is imprecise: with 256 texels compressed into 128 screen pixels (2 texels/screen-pixel),
  *any* point-sampled location lands on some texel, and since every texel in `tex256_` is already a
  pure R or G value, `IsPure` would pass at essentially any sample location in that column — the
  specific "boundary" framing that matters for the magnification/Linear column (where hitting a
  non-boundary point would still show a valid, if different, blend/point result) doesn't carry the
  same discriminating weight here.
- Why it matters: this doesn't make the test wrong (a passing `IsPure` result genuinely does confirm
  Point filtering never blends), but it means the minification/Point check has less power to catch a
  regression than its magnification sibling — e.g. it would not detect a minification-specific
  filter-selection bug that only manifested away from this specific sample point (there isn't such a
  code path today per the `ApplySamplerState` switch, which selects filter mode independent of the
  sampled location, but the test's own documentation overstates the geometric precision of this
  particular check).
- Related files: none needed — this is a self-contained documentation-precision note.
- Suggested future action (not implemented by this audit): none required; noting for completeness
  since the anti-boilerplate rule for this audit calls for checking whether a test's stated rationale
  actually matches its assertions.

## Cross-File Observations

- Confirmed (`cmake/Tests/VulkanTests.cmake:74-79`) this exact source is reused for Vulkan, unlike
  the mip-filter test (Task 298) which needed a Vulkan-specific fork because Vulkan's `Point` mapping
  has always been mip-aware (see the audit of `easygl_texture_mip_filter_effect_test.cpp` in this same
  batch) — this file's simpler Point/Linear-only scope doesn't touch that divergence, so verbatim
  reuse across backends is legitimate here.
- Shares its `DiffuseColor` doubling-compensation idiom and `RasterizerState::CullNone`
  (Task 896) workaround with `easygl_texture_filter_linear_golden_test.cpp` and
  `easygl_texture_mip_filter_effect_test.cpp` — consistent, not duplicated-and-drifted.

## Missing or Weak Tests

See F1 — not a coverage gap so much as a documentation-precision note; no functional gap identified.

## Positive Findings

- All four column geometries and their sample-point arithmetic were independently reconstructed and
  found correct.
- Correctly attributes and re-applies two separate, previously-tracked findings (`DiffuseColor=0.5`
  doubling compensation, `RasterizerState::CullNone` winding fix) rather than silently re-discovering
  or omitting them.
- The `Check` struct + loop (lines 178-193) is a clean, low-duplication way to report all four
  sub-results with per-check pass/fail output, rather than four near-identical inline blocks.

## Final Assessment

A solid, well-targeted test whose core architectural claim (magnification and minification share
identical sampler math on this backend, absent mipmaps) was independently verified against the real
GL filter-mapping switch statement and found accurate; one minor documentation-precision nit (F1)
does not affect its correctness.
