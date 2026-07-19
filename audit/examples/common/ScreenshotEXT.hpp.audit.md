# Audit: examples/common/ScreenshotEXT.hpp

## Metadata
- Source file: `examples/common/ScreenshotEXT.hpp` (47 lines)
- Audit status: AUDITED
- Subsystem: `examples-common` shard
- File type: shared header-only helper, used by multiple example demos
- XNA/FNA relevance: wraps `GraphicsDevice::GetBackBufferData`/`Texture2D::SaveAsPng` — not itself
  an XNA type
- Related production code: `GraphicsDevice.hpp`/`.cpp`, `Texture2D.hpp`/`.cpp` (audited this
  session as part of the `xna-graphics` shard)

## Purpose
A single-function helper (`SaveBackBufferScreenshotEXT`) that captures the current backbuffer to a
PNG file, reused by several demos' `--screenshot` flags (e.g. `demo_net_avatar_sync`, audited this
session) for baseline/after documentation.

## Executive Verdict
Correct, minimal, and appropriately defensive: it early-returns on a zero/negative viewport
dimension (lines 26-29) rather than allocating a zero-or-negative-sized buffer or calling into
`GetBackBufferData` with a degenerate region.

## Checklist Results
- Correctly converts `GraphicsDevice::GetBackBufferData`'s `Color` output to raw RGBA bytes before
  constructing the `Texture2D` used for `SaveAsPng` — reuses `Texture2D::CreateFromPixels`/
  `SaveAsPng`, per its own comment, rather than adding new image-encoding code.
- No resource leaks: `pixels`/`rgba` are stack-local `std::vector`s; `shot` (the `Texture2D`) is a
  stack-local value, not a `new`-allocated pointer.

## Detailed Findings
None.

## Cross-File Observations
Consumed by `demo_net_avatar_sync/src/SyncGame.cpp` (audited this session) via its
`--screenshot <path>` flag — confirmed used correctly there (called only on the final smoke frame,
guarded by `!screenshotPathEXT_.empty()`).

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
The zero/negative-dimension guard is a small but genuinely useful defensive check that prevents a
degenerate call into `GetBackBufferData`/`CreateFromPixels` if invoked before a window/viewport is
fully established.

## Final Assessment
No findings.
