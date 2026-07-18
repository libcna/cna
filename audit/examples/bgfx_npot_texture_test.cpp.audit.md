# Audit: examples/bgfx_npot_texture_test.cpp

## Metadata

- Source file: `examples/bgfx_npot_texture_test.cpp` (162 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — non-power-of-two (NPOT) texture upload/sampling pixel
  test (Task 821)
- File type: standalone `Game`-subclass executable, CTest-registered (Bgfx-specific adaptation of
  `examples/easygl_npot_texture_test.cpp`, Task 268, already reused verbatim on Vulkan).
- XNA/FNA relevance: direct — `Texture2D` NPOT construction/sampling, `SpriteBatch.Draw`,
  `SamplerState.PointClamp`.
- FNA reference: XNA/FNA's `Texture2D` has no power-of-two restriction on desktop targets (unlike
  legacy D3D9 mobile profiles); NPOT textures are ordinary, supported content.
- Related production code: `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp`
  (`BgfxTextureBackend::BgfxTextureBackend()`, lines 217-250; `bgfx::createTexture2D`/
  `updateTexture2D` calls), `src/Microsoft/Xna/Framework/Graphics/Texture2D.cpp`
  (`CreateFromPixels()`, lines 811-828).

## Purpose

Uploads a 3×5 `Texture2D` (3 and 5 are both non-power-of-two, and the file's header additionally
frames width=3 as "not a multiple of 4, exercising row-alignment handling") with a distinct solid
colour per row, draws it full-screen via `SpriteBatch` with `SamplerState::PointClamp`, and reads
back one pixel from each of the 5 screen-space row bands, asserting each recovers its own row's
exact colour.

## Executive Verdict

**Mostly healthy** — the core NPOT dimension/sampling assertion is genuine and well-targeted, but
the file's own "row-alignment" framing for why width=3 specifically matters is not correct for
this test's actual pixel format (see F1); this does not invalidate what the test verifies, only
its stated rationale for *why* 3 (vs. any other non-power-of-two width) was chosen.

## Checklist Results

### API / XNA / FNA parity
`Texture2D::CreateFromPixels` (used at line 114) is a `NOXNA` CNA convenience constructor (not a
real XNA API — genuine XNA content loads textures via `Texture2D.FromStream`/`SetData`), which is
an acceptable, correctly-scoped test-only shortcut; `SpriteBatch::Draw(tex_, destRect, srcRect,
Color::White)` (line 81) and `SamplerState::PointClamp` (line 79) both match real XNA/FNA
`SpriteBatch`/`SamplerState` surface exactly.

### Behavioral correctness
Confirmed `BgfxTextureBackend`'s constructor (`BgfxGraphicsBackend.cpp` lines 217-250) creates the
texture via `bgfx::createTexture2D(width, height, ..., bgfx::TextureFormat::RGBA8, ...)` then
`bgfx::updateTexture2D(...)` with the raw, tightly-packed `data.pixels` buffer (no manual pitch
override in the initial-upload path) — i.e. the 3×5×4-byte buffer this test builds (`px` in
`Initialize()`, lines 101-114) is uploaded byte-for-byte as bgfx's own `Memory` blob, and bgfx's
`updateTexture2D` computes pitch internally when none is explicitly supplied. No CNA-side
manual row-stride math exists in this path that could introduce an alignment bug for this test to
catch.

### Logic
`RunCheck`'s retry loop (lines 63-92) applies the same Task-406 (`GetBackBufferData` "first read
per rendered frame") workaround used throughout this shard; row sample points
(`sy = (2*row+1)*H/(2*kTexH)`, line 84) correctly land at each row band's vertical centre,
avoiding edge/seam sampling.

### C++ correctness
`kRowColors` array-to-pixel-buffer conversion (lines 101-113) correctly indexes
`px[(y*kTexW + x)*4]` — standard row-major RGBA8 layout, matches what
`BgfxTextureBackend`'s constructor expects (`data.pixels`, tightly packed, `width*height*4`
bytes total, verified against the constructor's own `bgfx::copy(data.pixels.data(),
data.pixels.size())` call).

### Robustness
Point filtering (`SamplerState::PointClamp`) is the correct choice to keep row boundaries crisp
(no bilinear blending softening the assertion at row seams) — deliberate and correct given the
per-row exact-colour assertion this test makes.

### Testing
5/5 rows individually asserted with exact-match colour comparison (`==`, not a tolerance-based
`colourMatch`, lines 131-133) — a stricter bar than most pixel tests in this shard, appropriate
since `SamplerState::PointClamp` should reproduce the source texel colour exactly with no
interpolation.

## Detailed Findings

### F1 — The file's "row-alignment" rationale for choosing width=3 does not actually apply to this test's RGBA8 pixel format

- Severity: LOW
- Confidence: HIGH (traced the actual byte math for this specific format/width combination)
- Category: test-authoring / stale-or-inaccurate-rationale
- Location/symbol: file header comment, line 13 ("3 is deliberately not a multiple of 4,
  exercising row-alignment handling for a non-power-of-two width")
- Evidence: the classic GL/GPU row-alignment concern (`GL_UNPACK_ALIGNMENT`, typically defaulting
  to 4 bytes) only produces observable padding when a row's byte count is *not* already a multiple
  of the alignment value. This test's texture format is RGBA8 (4 bytes/pixel), so a row of
  `width=3` texels is `3 × 4 = 12` bytes — already a multiple of 4 regardless of the pixel width
  chosen. Row-alignment padding would only become a genuine concern for a format with 1, 2, or 3
  bytes per pixel (e.g. a single-channel or RGB24 upload) with an odd/non-multiple-of-4 width;
  it cannot manifest for any width at all under 4-bytes/pixel RGBA8, since every row is
  automatically 4-byte-aligned by construction.
- Why it matters: this doesn't make the test wrong or ineffective — it still correctly exercises
  genuine non-power-of-two texture dimensions (3 and 5 are both non-power-of-two, which is the
  actually-meaningful property for "NPOT support"), and the underlying `BgfxTextureBackend`
  upload path was independently confirmed to have no manual alignment-sensitive stride logic in
  this code path regardless. But a future reader relying on the header comment's stated rationale
  to explain *why* width=3 (rather than, say, width=5 or width=7) was chosen would be misled into
  thinking a row-alignment bug class is being guarded against here, when it structurally cannot
  occur for this pixel format.
- FNA/XNA comparison: N/A — this is a test-authoring rationale accuracy issue, not an XNA/FNA
  behavior question.
- Related files: none — purely local to this file's own header comment.
- Suggested future action (not implemented by this audit): rephrase the header comment to frame
  width=3/height=5 as "non-power-of-two dimensions" (the property that actually matters and is
  actually tested) without the row-alignment claim, or — if row-alignment coverage is genuinely
  wanted — add a companion single-channel/RGB24 NPOT case where a 4-byte alignment boundary could
  actually be crossed.

## Cross-File Observations

- Same Bgfx-specific `SetDepthTestEnabled(false)` → `DepthStencilState`-based substitution and
  Task-406 retry-loop restructuring pattern used throughout this shard's adaptations of the
  EasyGL/Vulkan test family (`easygl_npot_texture_test.cpp`, Task 268).

## Missing or Weak Tests

None beyond the F1 rationale-accuracy note — the test's actual assertions (5 distinct rows,
exact-colour match, correct sample positioning) are sound and sufficient for what "NPOT texture
upload+sampling works" needs to demonstrate.

## Positive Findings

- Exact-equality colour assertions (rather than a loose tolerance) are an appropriately strict bar
  given point-filtered sampling, and this audit confirmed the production upload path has no
  alignment-sensitive logic that would make such strictness fragile.
- Clean per-row solid-colour fixture design makes failure diagnosis trivial (a wrong row index in
  the sample math, or a genuine sampling bug, would show up as a specific, identifiable row
  mismatch rather than an aggregate pass/fail).

## Final Assessment

A solid, genuinely NPOT-exercising test let down only by an inaccurate parenthetical rationale in
its own header comment (F1, LOW severity, no functional impact) about *why* the chosen width
matters. The actual assertions correctly validate NPOT texture upload and point-filtered sampling
on Bgfx.
