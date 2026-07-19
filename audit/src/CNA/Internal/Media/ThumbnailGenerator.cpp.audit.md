# Audit: src/CNA/Internal/Media/ThumbnailGenerator.cpp

## Metadata
- Source file: `src/CNA/Internal/Media/ThumbnailGenerator.cpp`
- Audit status: AUDITED (full read, 143 lines)
- Subsystem: `cna-internal-core` shard
- File type: C++ implementation
- XNA/FNA relevance: N/A -- NOXNA
- Main related tests: not independently located in this pass

## Purpose
Implements `Downscale()` (box-filter RGBA8 downscale, never upscales) and `CreatePngThumbnail()` (load via
`ImageLoader`, downscale, encode to PNG in-memory via SDL3_image's `IMG_SavePNG_IO`).

## Executive Verdict
Healthy -- independently verified correct, including bounds-safety of the box-filter accumulation.

## Checklist Results

### Bounds safety: confirmed
`Downscale()`'s inner accumulation loop (lines 52-65) guards every source-pixel read with
`idx + 3 >= source.pixels.size()) continue;` (line 61) before touching 4 channel bytes at `idx` -- correctly
prevents any out-of-bounds read even if `source.width`/`height` were ever inconsistent with
`source.pixels.size()`'s actual length. The destination-row/column sample ranges (`sy0/sy1`, `sx0/sx1`) are
correctly clamped against `source.height`/`source.width` via `std::min` in the loop bounds (lines 54, 56).

### Resource lifetime
`CreatePngThumbnail()` correctly destroys the SDL surface and closes the IO stream on every exit path
(including the `io == nullptr` early return, which destroys the surface first) -- no leak on any branch.

### Behavioral correctness
Never-upscale invariant is enforced twice (once in `Downscale()`, once redundantly-but-consistently in
`CreatePngThumbnail()` before calling `Downscale()`) -- consistent, not contradictory.

## Detailed Findings
None.

## Cross-File Observations
Reuses `Graphics::ImageLoader` rather than reimplementing image decoding, and encodes to memory (not a temp
file) so `GetThumbnail()` has no filesystem side effects -- both good architectural choices already reflected
correctly in the header's own documentation.

## Missing or Weak Tests
Not independently located in this pass; a pixel-level correctness test (known small input, known downscaled
output) would strengthen confidence in the box-filter averaging beyond code review.

## Positive Findings
Careful bounds-checking on the box-filter's source-pixel accumulation; correct resource cleanup on every
early-return path.

## Final Assessment
No issues found.
