# Audit: examples/easygl_npot_texture_test.cpp

## Metadata

- Source file: `examples/easygl_npot_texture_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — non-power-of-two texture upload + GPU sampling
  pixel-readback test
- File type: `Game`-derived executable, CTest-registered as `cna_test_easygl_npot_texture` /
  `EasyGL_NpotTexture` (`cmake/Tests/EasyGLTests.cmake:1230-1232`)
- XNA/FNA relevance: direct — `Texture2D::CreateFromPixels`, `SpriteBatch::Draw`,
  `SamplerState::PointClamp`
- Production sources cross-checked: `src/Microsoft/Xna/Framework/Graphics/Texture2D.cpp`
  (`CreateFromPixels`), `include/Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp` (`Begin`
  overloads), `src/Microsoft/Xna/Framework/Graphics/SpriteBatch.cpp` (sampler-state handling),
  `include/Microsoft/Xna/Framework/Graphics/SamplerState.hpp`

## Purpose

Uploads a 3×5 texture (5 solid-colour horizontal rows) whose width (3) is not a multiple of 4 and
whose dimensions (3, 5) are not powers of two, draws it full-screen via `SpriteBatch` with point
filtering, and reads back one pixel per row's screen-space band to confirm each row's colour survived
GPU upload and point-sampled draw, distinct from the CPU-side `SetData`/`GetData` round trip already
covered elsewhere (per the file's own comment, `Texture2DTests.cpp`'s `LevelCount` tests).

## Executive Verdict

**Needs attention** — not for a rendering defect (the test's actual pixel assertions are sound), but
because its own stated rationale for *why* width=3 is significant is not correct for this codebase's
texture pixel format, and a second, unrelated API-ergonomics issue is directly evidenced by a
`const_cast` this file is forced to perform.

## Checklist Results

### API / XNA / FNA parity
`Texture2D::CreateFromPixels(device, w, h, rgba)` is a `NOXNA` CNA convenience factory (confirmed:
`Texture2D.cpp:811-828` builds an `ImageData` and forwards to `GraphicsDevice::GetBackend().CreateTexture`,
no XNA equivalent exists — real XNA/FNA constructs a `Texture2D` then calls `SetData`). Using it here
is a reasonable simplification for a test file, not a parity concern since the test isn't claiming to
validate the constructor itself. `SpriteBatch::Begin(SpriteSortMode, BlendState, SamplerState*,
DepthStencilState*, RasterizerState*)` (`SpriteBatch.hpp:116-120`) matches FNA's corresponding
5-argument `Begin` overload in shape (nullable sampler/depth-stencil/rasterizer state), modulo the
C++ pointer-vs-C#-nullable-reference translation — see F2 below for a concrete issue this translation
introduces.

### Behavioral correctness
Traced `SpriteBatch::Begin`'s sampler-state handling (`SpriteBatch.cpp:116-118`):
`const SamplerState& effectiveSampler = samplerState ? *samplerState : SamplerState::LinearClamp;` —
confirms the passed `SamplerState::PointClamp` is only ever read, never mutated, through the pointer
this file passes — so the `const_cast` this file performs (line 86) does not enable any real
mutation-through-pointer bug in practice, only forces an unnecessary but harmless cast at the call
site (see F2 for why the cast is needed at all).

Row-sampling math: `sy = (2*y+1)*H/(2*kTexH)` samples the vertical centre of each of the 5
screen-space bands the 3×5 texture is stretched into — correct integer-arithmetic centre-of-band
formula (avoids the two adjacent bands' boundary, where point-filtered texel selection near an exact
half-pixel boundary could sample the wrong row).

### Logic
`SamplerState::PointClamp` (point filtering, no bilinear blending) is the right choice for this test:
with `LinearClamp` (the `Begin()` default), colours from adjacent rows would blend at row boundaries
and the exact solid-colour assertions (`==`, not tolerance-based, lines 101-103) could fail even on a
correct implementation purely from interpolation — the test's author correctly anticipated this.

### Robustness
Uses **exact** equality (`==`) for each channel (lines 101-103), not a tolerance band, unlike most
other pixel tests in this shard — justified given point (nearest-neighbor) sampling of a
freshly-uploaded, unfiltered, unblended solid-colour texel should reproduce the exact input byte
value with no rounding in a correct implementation; a looser tolerance here would actually mask a
real off-by-one-row sampling bug, so the stricter check is the right choice for what this test is
specifically probing.

## Detailed Findings

No CRITICAL/HIGH findings.

### F1 — Header comment's stated rationale for width=3 does not match this codebase's actual pixel format

- Severity: MEDIUM
- Confidence: HIGH
- Category: test-coverage / documentation accuracy
- Location/symbol: file header comment (lines 6-8: "3 is deliberately not a multiple of 4,
  exercising GL row alignment for a non-power-of-two width"); pixel buffer construction
  (lines 55-67: `std::vector<std::uint8_t> px(kTexW * kTexH * 4)`, i.e. 4 bytes/pixel RGBA)
- Evidence: `GL_UNPACK_ALIGNMENT`'s default value is 4 bytes; a row-byte-count alignment hazard only
  arises when `width × bytesPerPixel` is *not* itself a multiple of that alignment. This project's
  texture pipeline is uniformly 4 bytes/pixel (RGBA8) — confirmed via `Texture2D.cpp`'s `ImageData`
  usage throughout (every `data.pixels.assign(w*h*4, …)` call site) and this file's own upload buffer
  (`kTexW*kTexH*4`). For a 4-bytes/pixel format, `width × 4` is a multiple of 4 for **every** integer
  `width`, including 3 — so a width of 3 cannot actually trigger a GL row-alignment padding case in
  this codebase; the alignment hazard the comment describes only exists for 1- or 3-bytes/pixel
  formats (e.g. plain `GL_RGB`), which this project does not use for `Texture2D`.
- Why it matters: the comment misdescribes what hazard this specific test exercises. The test remains
  a legitimate and useful NPOT (non-power-of-two dimension) upload/sampling test on its own merits —
  older/mobile GL implementations have historically restricted NPOT textures' mipmapping/wrap-mode
  support (irrelevant here, since no mipmaps and `Clamp` addressing are used) — but a reader trusting
  the comment to explain *why* this specific width was chosen would draw an incorrect conclusion
  about what code path is actually being stressed, and might wrongly assume a row-alignment bug class
  is covered here when it structurally cannot be, for this pixel format.
- FNA/XNA comparison: N/A (GL row-alignment is a backend implementation detail, not an XNA behavior).
- Suggested future action (not implemented by this audit): correct the comment to describe this as an
  NPOT-dimension test (dimensions not powers of two), and if a genuine row-alignment test is desired,
  it would need a 1- or 3-bytes/pixel upload path, which does not appear to exist in this codebase's
  `Texture2D`/`ImageData` pipeline.

### F2 — `SpriteBatch::Begin`'s `SamplerState*` parameter forces callers to `const_cast` away the class's own `static const` presets

- Severity: LOW
- Confidence: HIGH
- Category: C++ correctness / API design (const-correctness)
- Location/symbol: this file, line 86:
  `const_cast<SamplerState*>(&SamplerState::PointClamp)`; `SpriteBatch::Begin(SpriteSortMode,
  BlendState, SamplerState*, DepthStencilState*, RasterizerState*)`
  (`SpriteBatch.hpp:116-120`); `SamplerState::PointClamp` declared `static const SamplerState`
  (`SamplerState.hpp:24`)
- Evidence: `SpriteBatch::Begin`'s sampler-state parameter is a mutable `SamplerState*`, but every one
  of the class's own canonical presets (`PointClamp`, `LinearClamp`, `AnisotropicWrap`, …) is declared
  `static const`, per this project's own documented "C# `static readonly` → C++ `static const`"
  convention (`CLAUDE.md`). Passing a preset by pointer to `Begin` therefore requires exactly the
  `const_cast` this file performs — directly evidenced, not hypothetical.
- Why it matters: confirmed via `SpriteBatch.cpp:118` that `Begin` only ever reads through this
  pointer (binds to a `const SamplerState&` immediately), so no live mutation bug results from the
  cast in practice — but the signature itself invites it and offers no compile-time protection if a
  future change to `Begin`'s implementation ever did write through the pointer. A `const
  SamplerState*` parameter (or taking by value/const-reference, matching the "read-only configuration
  object" semantics `Begin` actually has) would let every caller pass a built-in preset directly with
  no cast.
- FNA/XNA comparison: N/A — in C#, `SamplerState` is a reference type with no `const`-pointer
  distinction, so FNA's own `Begin(..., SamplerState samplerState, ...)` signature has no equivalent
  wart; this is purely a C++-port ergonomics issue introduced by the `SamplerState*` translation
  choice, not a behavior difference from FNA.
- Suggested future action (not implemented by this audit): change `SpriteBatch::Begin`'s
  `samplerState`/`depthStencilState`/`rasterizerState` parameters to `const T*` (all three follow the
  same pattern per `SpriteBatch.hpp:116-155`), if this header is revisited for other reasons — a
  broader API-surface change outside a single test file's own scope to make unilaterally here.

## Cross-File Observations

- F2 is a `SpriteBatch.hpp` API-design observation surfaced *by* this test file (which needed the
  `const_cast`), not a defect in this file's own logic — flagged for whichever shard audits
  `SpriteBatch.hpp`/`.cpp` directly, since the fix (if made) belongs there.

## Missing or Weak Tests

- None specific to this file's own stated scope (GPU upload+sampling of an NPOT texture) — the row
  sampling, point-filter choice, and exact-equality assertions are all appropriate for what is
  actually being checked, independent of F1's documentation-accuracy issue.

## Positive Findings

- Correct, deliberate choice of `PointClamp` over the `Begin()` default (`LinearClamp`) to avoid
  cross-row colour bleeding invalidating an otherwise-correct implementation.
- Exact-equality pixel assertions are the right level of strictness for this specific claim (unfiltered
  nearest-neighbor sampling of freshly-uploaded, unblended solid colour), not an accidentally-too-loose
  tolerance that would mask an off-by-one-row bug.
- Well-reasoned centre-of-band sampling formula avoids row-boundary sampling ambiguity.

## Final Assessment

A functionally sound, well-targeted NPOT texture test whose pixel assertions and sampler-state choice
are both correct and deliberate; its own header comment misattributes the significance of its chosen
width to a GL row-alignment hazard that cannot actually occur for this codebase's uniformly
4-bytes/pixel `Texture2D` format, and it incidentally surfaces a real, if low-severity, const-
correctness wart in `SpriteBatch::Begin`'s signature.
