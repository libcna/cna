# Audit: examples/sdlrenderer_texture2d_saveas_roundtrip_test.cpp

## Metadata

- Source file: `examples/sdlrenderer_texture2d_saveas_roundtrip_test.cpp` (175 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlrenderer` shard — Task 683, `Texture2D::SaveAsPng`/`SaveAsJpeg` round-trip
  from a texture actually created/updated through the real SDL_Renderer backend.
- CMake registration: `cna_sdl_test(cna_test_sdl_texture2d_saveas_roundtrip
  examples/sdlrenderer_texture2d_saveas_roundtrip_test.cpp)` / `SDL_Renderer_Texture2D_SaveAsRoundTrip` —
  confirmed at `cmake/Tests/SdlRendererTests.cmake:139-141`.
- XNA/FNA relevance: direct — `Texture2D.SaveAsPng`/`Texture2D.SaveAsJpeg` (FNA `Texture2D.cs`).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/Texture2D.cpp`
  (`SaveAsPng(Stream*, int, int)`, lines 630-686; `SaveAsJpeg(Stream*, int, int)`, lines 725-783;
  `GetJpegSaveQuality`, lines 714-723).

## Purpose

Confirms `SaveAsPng`/`SaveAsJpeg` produce correct output for a texture whose pixels were actually pushed
through the real SDL_Renderer backend (via `SetData`), by chaining Task 682's already-proven "draw + real
readback" method onto the save path: build a 2x2 (Red/Green/Blue/Yellow) source, `SetData` it, save to
both PNG and JPEG streams, decode each back via `FromStream`, draw both side by side via `SpriteBatch`, and
read back all 8 resulting pixels (4 quadrants × 2 formats) from the real framebuffer — PNG checked at a
tight ±10 tolerance (lossless), JPEG at ±40 (lossy, matching the backend-agnostic `Texture2DTests.cpp`
convention per the file's own comment).

## Executive Verdict

**Healthy** — correctly reasons that `SaveAsPng`/`SaveAsJpeg` are pure CPU-side operations independent of
which backend created the texture, and the test's own chained verification (SetData → Save → FromStream →
real draw → readback) is a genuinely strong, non-redundant end-to-end check, not a "trusts itself" loop.

## Checklist Results

### API / XNA / FNA parity
`SaveAsPng(Stream*, int targetWidth, int targetHeight)`/`SaveAsJpeg(Stream*, int, int)` (lines 89, 94)
match FNA's `Texture2D.SaveAsPng(Stream, int, int)`/`SaveAsJpeg(Stream, int, int)` signatures (resize-on-
save overloads). Both calls here pass `2, 2` — identical to the source dimensions — so no actual resizing
occurs; this is a deliberate, correct scoping choice (this test is about *format* round-trip fidelity, not
`SDL_ScaleSurface`'s resize path, which is exercised elsewhere).

### Behavioral correctness
Traced `SaveAsPng` (`Texture2D.cpp` lines 630-686): operates entirely on `cpuPixels_` via
`SDL_CreateSurfaceFrom` + `IMG_SavePNG_IO`, never touching `backend_`/the real GPU texture at all — matches
the file's own claim precisely (lines 5-13). `MaybeFreeCpuPixels()` (line 63-67) only frees `cpuPixels_`
when `graphicsDevice_->contextRecoveryEnabled_` is `false`; confirmed via
`include/Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp` line 818 that this flag **defaults to
`true`**, so `cpuPixels_` survives by default — validating the file's claim that "the CPU-side save path is
guaranteed correct by construction once SetData has correctly populated `cpuPixels_`" (which Task 678
already independently proved true for SDL_Renderer's real backend texture, per the sibling
`sdlrenderer_texture2d_setdata_getdata_test.cpp`). `GetJpegSaveQuality()` (lines 714-723) correctly mirrors
FNA's `FNA_GRAPHICS_JPEG_SAVE_QUALITY` env-var lookup with a documented fallback to `100`.

### Logic
The destination layout is correctly non-overlapping: `pngTex_` drawn into `(0,0)-(16,16)`, `jpegTex_` into
`(16,0)-(32,16)` (lines 124, 126), against a `32x16` backbuffer (lines 162-163) — the two draws exactly
tile the full width with no gap or overlap. Verified the 8 sample points (lines 130-141) each land inside
their respective quadrant's centre (`x∈{4,12}` for PNG's `[0,16)` half, `x∈{20,28}` for JPEG's `[16,32)`
half) — correct arithmetic, no off-by-one against the tile boundary at `x=16`.

### C++ correctness
Two `static_cast<System::IO::intcs>(...)` narrowing casts (lines 98, 101) for `size_t → int32_t`, both
inert for a handful-of-bytes PNG/JPEG buffer — same pattern flagged as theoretical-only elsewhere in this
shard, consistent treatment.

### Memory/resource lifetime
`gdm_`/`sb_`/`pngTex_`/`jpegTex_` are `unique_ptr` members with standard RAII lifetime; `MemoryStream`s are
stack-local. `SaveAsPng`'s SDL-side surface/IO-stream cleanup (confirmed in `Texture2D.cpp`) correctly
destroys `surface`/`scaled`/`dst` on both the success and every early-throw path (lines 641-685) — not this
test's own responsibility, but worth confirming the underlying implementation is exception-safe since this
test is the one exercising it with real data.

### Performance / Thread safety
N/A — one-shot CTest executable, single-threaded.

### Architecture
Correctly XNA-facing — only public `Texture2D`/`SpriteBatch`/`MemoryStream`/`GraphicsDevice` API used.

### Maintainability
175 lines, clearly structured (`check()` helper for named pass/fail lines, consistent with the sibling
`setdata_getdata`/`miplevel_throws` files' own convention), no dead code.

### Portability
Correctly requires `PresentationMode::NativeBackBuffer` (line 164), same rationale as every other file in
this batch.

### Robustness
`check(!pngBytes.empty(), …)`/`check(!jpegBytes.empty(), …)` (lines 91, 96) are a reasonable minimal sanity
gate before attempting to decode — correctly catches a total encode failure early with a clear diagnostic
rather than letting a downstream `FromStream` throw an opaque error.

### Testing
This file is itself a test. See Missing or Weak Tests.

### Cross-file consistency
Explicitly and accurately built on Task 682's (`sdlrenderer_texture2d_fromstream_test.cpp`) proven "draw +
real readback" method, and correctly distinguishes its own added value (verifying the *save* path with
data that went through the real backend) from Task 682's own scope (verifying *load* via `FromStream`) —
the two files' stated relationship, cross-checked directly, is accurate.

## Detailed Findings

No HIGH/MEDIUM findings. One minor observation:

### F1 — JPEG quality is left at the default (100) rather than exercised at multiple quality levels

- Severity: LOW
- Confidence: HIGH
- Category: test-coverage
- Location/symbol: `GetJpegSaveQuality()` (`Texture2D.cpp` lines 714-723) is never overridden via
  `FNA_GRAPHICS_JPEG_SAVE_QUALITY` in this test, so it always saves at quality 100 (near-lossless) —
  the widest, most forgiving ±40 tolerance is therefore never really stress-tested against a genuinely
  lossy (e.g. quality 50) encode.
- Evidence: no `setenv("FNA_GRAPHICS_JPEG_SAVE_QUALITY", …)` call anywhere in this file.
- Why it matters: a regression that broke JPEG saving specifically at lower quality settings (e.g. an
  off-by-one in a quantization-table lookup) would not be caught by this test. Low severity since quality
  100 is also a legitimate, commonly-used setting worth verifying on its own.
- FNA/XNA comparison: N/A (test-scope observation).
- Related files: none.
- Suggested future action (not implemented by this audit): optionally add a second JPEG check at a lower
  quality setting if quality-dependent regressions become a concern.

## Cross-File Observations

- Reuses the exact same `colourMatch` helper shape as every other file in this batch (independently
  defined per file rather than shared via a common header) — consistent but duplicated; a shared test-
  utility header would reduce duplication across the shard without changing behavior (an architecture
  observation, not a defect).

## Missing or Weak Tests

See F1 — no lower-quality JPEG variant tested.

## Positive Findings

- Correctly reasons about which code path is/isn't backend-specific before writing the test (CPU-side save
  logic is backend-agnostic by construction; the value-add here is chaining onto data that went through the
  real backend) — a well-justified, non-redundant test.
- Differentiated tolerances (±10 PNG lossless vs ±40 JPEG lossy) are a correct, deliberate choice matching
  the format's actual fidelity characteristics, not an arbitrarily loose blanket tolerance.
- Clean non-overlapping destination-quadrant layout with correctly verified sample-point arithmetic.

## Final Assessment

A well-reasoned, correctly-implemented round-trip test with accurate tolerances and no correctness defects
found in either the test or the underlying `SaveAsPng`/`SaveAsJpeg` implementation it exercises.
