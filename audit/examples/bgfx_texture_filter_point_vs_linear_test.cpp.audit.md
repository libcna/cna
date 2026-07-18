# Audit: examples/bgfx_texture_filter_point_vs_linear_test.cpp

## Metadata

- Source file: `examples/bgfx_texture_filter_point_vs_linear_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — Task 747, `TextureFilter::Point` vs `Linear` (both
  magnification and minification) on a 3D stock effect (`DualTextureEffect`), Bgfx backend
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_bgfx_test(cna_test_bgfx_texture_filter_point_vs_linear …)` /
  `cna_register_backend_test(NAME Bgfx_TextureFilter_PointVsLinear …)`, `cmake/Tests/BgfxTests.cmake:88-91`).
- XNA/FNA relevance: direct — `SamplerState.Filter`, `TextureFilter::Point`/`Linear`,
  `DualTextureEffect.DiffuseColor`, `DualTextureEffect.Texture`/`Texture2`.
- FNA reference: `Graphics/States/TextureFilter.cs`,
  `Graphics/Effect/StockEffects/HLSL/DualTextureEffect.fx` (`color.rgb *= 2` doubling, lines 100/115).
- Related production code: `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp`
  (`ApplySamplerState`, lines 1890-1939); `src/Microsoft/Xna/Framework/Graphics/DualTextureEffect.cpp`
  (Task 383 fix referenced at lines 99-104).

## Purpose

Verifies both magnification (a 2-texel Red|Green texture stretched full-screen) and minification (a
256-texel alternating Red/Green texture compressed into 64 screen pixels) sample correctly for both
`TextureFilter::Point` (expect a pure, unblended channel at the texel-boundary sample point) and
`TextureFilter::Linear` (expect a genuine ~50/50 blend), 4 checks total. The file's own header comment
explains why magnification and minification are expected to behave identically here: CNA maps XNA's
flat `TextureFilter` enum to a single combined min+mag filter pair (matching FNA's own flat-enum
semantics — there is no separate min/mag XNA API), and `CreateFromPixels` textures have no mipmaps by
default, so this test exercises both scales explicitly mainly to confirm scale itself doesn't
interact badly with the Task 750 sampler-state fix, not because the two axes could plausibly diverge.

## Executive Verdict

**Healthy** — every numeric derivation (the `U=0.5` boundary point for both textures, the
`DualTextureEffect` `DiffuseColor=0.5` compensation for the `color.rgb *= 2` doubling, the
`IsPure`/`IsBlended` classifiers) was independently re-derived and confirmed correct against both FNA's
`DualTextureEffect.fx` source and the geometry of each texture.

## Checklist Results

### API / XNA / FNA parity
`DualTextureEffect.setDiffuseColorProperty`/`setTextureProperty`/`setTexture2Property` map correctly
to FNA's `DualTextureEffect` public surface. The `DiffuseColor=Vector3(0.5,0.5,0.5)` compensation
(lines 100-105) is independently confirmed necessary and correct: `DualTextureEffect.fx` literally
contains `color.rgb *= 2;` (confirmed at both line 100 and 115 of the vendored FNA HLSL source, one
per vertex-color/no-vertex-color shader variant) as compensation for the effect's second ("overlay")
texture sampling with an implicit ×2 range so a mid-gray overlay leaves the base texture unscaled;
with a plain *white* (identity, `(1,1,1,1)`) overlay texture as used here, that doubling would
otherwise push every sampled channel toward saturation (255), destroying the mid-range blend values
this test's `IsBlended` check depends on — setting `DiffuseColor=0.5` (`2 * 0.5 = 1`) exactly cancels
it, which this audit independently confirmed against the FNA shader source rather than trusting the
comment.

### Behavioral correctness
- Magnification (`tex2_`, 2 texels Red|Green stretched full-screen): sample point `W/2` maps to
  `U=0.5`, exactly the boundary between the two texels' centers (`u_0=0.25`, `u_1=0.75` — the true
  midpoint is `0.5`) — independently confirmed correct.
- Minification (`tex256_`, 256 texels compressed to 64 screen pixels): sample point `W/2` again maps
  to `U=0.5`; for a 256-texel texture, texel `i`'s center is at `(i+0.5)/256`; texel 127's center is
  `0.498047`, texel 128's center is `0.501953` — their exact midpoint is `0.5`, confirming the file's
  own claim ("guaranteed NOT to coincide with any texel center") is correct. Texel 127 is odd → Green,
  texel 128 is even → Red (per `pattern256`'s `i%2==0 ? red : green` rule, lines 169-173), so the two
  neighboring texels at this boundary genuinely differ in color, which `IsBlended`/`IsPure` need to be
  meaningful.
- `IsPure` (lines 75-80) accepts *either* texel winning the boundary tie (`(rHigh&&gLow)||(rLow&&gHigh)`)
  rather than asserting a specific one — a deliberately correct design choice given that exact
  texel-boundary point-filtering ties are implementation/rounding-direction-defined, not a fixed XNA
  guarantee.

### Logic
`RunCheck` (lines 129-152) correctly follows the shard's established per-check-fresh-frame retry
pattern for Bgfx's `GetBackBufferData` quirk (Task 406), redrawing the entire scene (`Clear` +
`DepthStencilState` + `BlendState::Opaque` + `DrawFullscreen`) on each iteration rather than reusing
stale state.

### Cross-file consistency
The file's own claim that "CNA maps XNA's single `TextureFilter` value to one GL/Vulkan/bgfx min+mag
filter pair... there is no separate min/mag control" is consistent with `TextureFilter.cs`'s actual
enum shape (a single flat value, not two independent min/mag XNA properties) — correctly distinguishes
this from the *6* split-filter enum *values* (`LinearMipPoint` etc., covered by the sibling
`bgfx_texturefilter_split_minmag_test.cpp` in this same batch) which *do* encode a min/mag distinction
within a single enum value, a distinction this file's comment does not conflate.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings.

### F1 — Minification and magnification checks cannot currently diverge given this project's no-default-mipmap convention, so this test cannot yet catch a min/mag-specific regression

- Severity: LOW
- Confidence: HIGH
- Category: test-coverage
- Location/symbol: whole file; `CreateFromPixels` (used for both `tex2_` and `tex256_`, lines 164,
  175) never requests mipmaps
- Evidence: the file's own header comment states this directly ("magnification and minification share
  identical underlying sampler math here"); independently confirmed via `ApplySamplerState`
  (`BgfxGraphicsBackend.cpp`) applying the same `filter`-derived flags regardless of which texture or
  on-screen scale is being drawn — there is no separate min-vs-mag code path to diverge in the first
  place at level 0 (no mip selection is even in play here, since both textures have exactly 1 level).
- Why it matters: this means the 4-check design, while not wrong, is not actually independent
  4-way coverage of 4 different sampler behaviors — checks 1&3 (`Point`) and 2&4 (`Linear`) are
  expected to (and do) produce the same class of result for a reason unrelated to whether Point/Linear
  truly differ across scale; a hypothetical future regression that broke *minification specifically*
  (e.g., once mipmapped `CreateFromPixels` textures are introduced) would not be caught by this file
  as currently written, since it deliberately avoids exercising that axis. This is honestly disclosed
  by the file's own comment, not a hidden gap.
- Related files: `bgfx_texture_mip_filter_effect_test.cpp` (this same batch) already covers the
  *mip-selection* half of minification with a real mipmapped texture — this file and that one are
  complementary, not overlapping, once this distinction is understood.
- Suggested future action: none required — correctly scoped and disclosed; noted for completeness
  per the checklist's test-coverage section.

## Cross-File Observations

- Complements `bgfx_texture_mip_filter_effect_test.cpp` (also in this batch): that file tests mip
  *level selection* with a real 8-level mipmapped texture, while this file tests point-vs-linear
  *within* a single level at both scales — together they cover the two independent axes of texture
  filtering behavior that this shard's individual files each only partially exercise.
- Shares the `DiffuseColor=0.5` Task-383 compensation idiom verbatim (same comment wording) with
  `bgfx_texturefilter_split_minmag_test.cpp` in this same batch — both correctly attribute the need to
  the same underlying `DualTextureEffect` shader behavior.
- Shares the `RasterizerState::CullNone` Task-896 winding-fix idiom with every other quad-drawing test
  in this batch.

## Missing or Weak Tests

See F1 — not a defect, but worth noting that "minification" here does not yet exercise a genuinely
distinct code path from "magnification" given the current no-default-mipmap convention.

## Positive Findings

- The `DiffuseColor=0.5` doubling-compensation math was independently verified against FNA's actual
  vendored HLSL shader source (`color.rgb *= 2`), not merely trusted from the comment.
- The exact texel-boundary sample-point derivation for both the 2-texel and 256-texel textures was
  independently re-computed and confirmed to land exactly at the claimed non-texel-center midpoint in
  both cases.
- `IsPure`'s either-side-of-the-tie acceptance is a correct, non-brittle way to test point filtering
  exactly at an ambiguous boundary.

## Final Assessment

A well-derived, numerically-verified test of `TextureFilter::Point`/`Linear` sampler-state plumbing on
Bgfx; its own honest disclosure that magnification and minification currently share identical
underlying behavior (F1) is a scope note, not a defect, and is corroborated by independently reading
the production sampler-application code.
