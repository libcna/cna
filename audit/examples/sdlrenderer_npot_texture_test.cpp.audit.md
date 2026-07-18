# Audit: examples/sdlrenderer_npot_texture_test.cpp

## Metadata

- Source file: `examples/sdlrenderer_npot_texture_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlrenderer` shard — Task 684, non-power-of-two texture upload+sample correctness.
  Extension of Task 268's `easygl_npot_texture_test.cpp`.
- File type: standalone `Game`-subclass executable, CTest-registered (`SDL_Renderer_NpotTexture` /
  `cna_test_sdl_npot_texture`, `cmake/Tests/SdlRendererTests.cmake:145-147`).
- XNA/FNA relevance: `Texture2D` upload/sample correctness for non-power-of-two dimensions (a real historical GL
  concern XNA/FNA also has to handle).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/Texture2D.cpp` (`CreateFromPixels`, lines
  811-828), `src/CNA/Internal/Backends/SdlRenderer/SdlGraphicsBackend.cpp`
  (`SdlTextureBackend::SdlTextureBackend`, lines 18-35; `SdlSpriteBatchBackend::SetSamplerFilter`, lines 91-121;
  `SdlSpriteBatchBackend::Draw` 4-arg overload, lines 143-183).

## Purpose

Renders a 3x5 and a 7x11 texture (deliberately non-multiple-of-4 and fully odd/NPOT in both dimensions), each row
a distinct solid colour, full-viewport via `SpriteBatch` with `PointClamp` sampling, then reads back each row's
vertical centre pixel from the real framebuffer and compares against the expected per-row colour. Explicitly
frames this as a genuinely backend-specific question (SDL_Renderer's `SDL_CreateTexture`/`SDL_UpdateTexture`
*could*, in principle, pad an NPOT texture on an older/limited GL profile) rather than assuming correctness.

## Executive Verdict

**Healthy** — the texture data layout, the sampling filter choice, and the row-centre sample-point math were all
independently re-derived and confirmed to exercise real upload/sample correctness rather than merely "compiles
and doesn't crash."

## Checklist Results

### Behavioral correctness

Traced the upload path: `RunCase()` builds `px` as a tightly-packed `texW*texH*4`-byte buffer (row stride exactly
`texW*4`, no padding) at lines 71-83, then calls `Texture2D::CreateFromPixels(dev, texW, texH, px)`
(`Texture2D.cpp:811-828`), which calls `device.GetBackend().CreateTexture(data)` →
`SdlTextureBackend::SdlTextureBackend` (`SdlGraphicsBackend.cpp:18-35`), which itself calls
`SDL_UpdateTexture(texture, nullptr, data.pixels.data(), width * 4)` — i.e. the *same* tightly-packed
`width*4` stride the test's own pixel buffer already uses. This means the test's pixel layout genuinely matches
what the production upload path expects; a row-alignment bug in `SDL_CreateTexture`'s internal handling of
`SDL_PIXELFORMAT_RGBA32` at width=3 or width=7 (not a multiple of 4) would have to originate inside SDL's own GL
backend, which is exactly the real risk this test is designed to catch (per its own framing) rather than being
neutralized by a test-side data-layout mismatch.

`PointClamp`'s `TextureFilter::Point` (`SamplerState.cpp:10`) maps in `SdlSpriteBatchBackend::SetSamplerFilter`
(`SdlGraphicsBackend.cpp:109-121`) to the `default:` branch → `SDL_SCALEMODE_NEAREST` — confirmed this is genuine
nearest-neighbor sampling, not accidentally falling through to `SDL_SCALEMODE_LINEAR`, which would blend adjacent
rows and could make a broken NPOT upload look deceptively correct (or a correct one look wrong) at the sampled
row-centre.

Re-derived the sample-point formula `sy = (2*y+1)*H/(2*texH)` (line 102) by hand for both cases (`H=22`, the
configured backbuffer height): for the 7x11 case, `22/11=2` exactly, giving `sy = 2y+1` — i.e. exact odd integers
1,3,5,...,21, landing precisely at the centre of each 2-pixel-tall destination row with zero rounding error. For
the 3x5 case, `sy` values are 2,6,11,15,19 — all distinct, roughly evenly spaced (≈4.4px apart, `22/5`), safely
away from any row boundary given `PointClamp`'s hard-edged nearest-neighbor scaling. Both derivations confirm the
sample points genuinely land inside their intended source row's screen-space band, not near a boundary where an
off-by-one scaling bug could accidentally still pass.

### API / XNA / FNA parity

`SpriteBatch::Draw(tex, destRect, sourceRect, color)` (test line 97) uses the 4-argument overload, correctly
routing to `SdlSpriteBatchBackend::Draw`'s matching 4-parameter override (`SdlGraphicsBackend.cpp:143-183`), not
the rotation/origin 8-argument overload — an appropriately minimal overload choice for a test that only needs a
full-viewport unrotated blit.

### Robustness

`GetBackBufferData(&reg, &px1, 0, 1)` per-row (line 105) correctly uses the rect-based overload (not the no-rect
whole-buffer one), addressing single pixels directly — appropriate for a per-row pixel-probe test.

### Testing

Genuinely exercises upload+sample correctness for 2 distinct NPOT shapes (3x5: not-multiple-of-4 in one
dimension; 7x11: odd/NPOT in both dimensions) rather than a single shape, and explicitly frames the SDL-specific
motivation (GL-profile-dependent internal padding) rather than copying the EasyGL test's rationale wholesale
despite structurally mirroring it (per the file's own header comment, Task 268's parity).

## Detailed Findings

No CRITICAL/HIGH/MEDIUM/LOW findings — texture layout, filter selection, and sample-point math all independently
verified consistent with genuine upload/sample-correctness testing, not incidental pass-by-luck.

## Cross-File Observations

- `SdlGraphicsBackend.cpp`'s own audit (F1) flagged `SdlTextureBackend::UpdatePixels`'s unconditional
  `stride` pass-through as a *potential* issue for callers passing `stride<=0` — this test does not exercise that
  code path at all, since `Texture2D::CreateFromPixels` goes through the *constructor*
  (`SdlTextureBackend::SdlTextureBackend`, which always computes `width * 4` itself, not through
  `UpdatePixels`), so this test provides no evidence either way on that separate, still-open finding.
- Mirrors `easygl_npot_texture_test.cpp` (Task 268) in structure/row-colour palette but correctly adapts the
  sampling filter rationale to this backend's own `SetSamplerFilter` implementation rather than assuming the
  EasyGL backend's GL-parameter equivalent applies unchanged.

## Missing or Weak Tests

None significant for the stated NPOT-upload purpose. A companion case at an even smaller NPOT size (e.g. 1xN or
Nx1) is not present, but the two chosen shapes (3x5, 7x11) already cover both "one NPOT dimension" and "both NPOT
dimensions," a reasonable minimal pair.

## Positive Findings

- Independently re-derived the row-centre sample-point formula and confirmed it lands exactly (7x11 case) or
  safely away from boundaries (3x5 case) inside each intended row, rather than merely trusting the formula's
  presence.
- Confirmed the texture's own pixel-buffer stride matches what the real upload path (`SDL_UpdateTexture`) expects,
  so this is a genuine test of SDL's internal NPOT handling, not inadvertently self-consistent with a test-side
  bug that would mask a real backend defect.
- Explicit, non-generic rationale for *why* this is a real per-backend risk (potential internal POT padding on
  older/limited GL profiles) rather than a boilerplate "just in case" test.

## Final Assessment

A well-constructed, genuinely discriminating pixel test. Both the data layout and the sampling math were
independently re-verified to actually exercise NPOT-specific upload/sample behavior rather than passing
incidentally.
