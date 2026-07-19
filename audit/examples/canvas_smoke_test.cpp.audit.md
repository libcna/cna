# Audit: examples/canvas_smoke_test.cpp

## Metadata
- Source file: `examples/canvas_smoke_test.cpp` (107 lines)
- Audit status: AUDITED (full read)
- Subsystem: `examples-tests-canvas` shard
- File type: standalone backend integration-test executable (`Game` subclass) — NOT registered as
  a CTest (see below)
- XNA/FNA relevance: exercises `GraphicsDevice::Clear`/`SpriteBatch::Draw`/`Texture2D` (public XNA
  API) against the Canvas (Emscripten-only, HTML Canvas 2D) backend

## Purpose
Structural smoke test for the Canvas backend: constructs a real `Game`, clears, draws a
`Texture2D` via `SpriteBatch` with rotation/origin/tint, over 2 frames.

## Executive Verdict
Correct, and its own header comment (lines 5-11) is an honest, load-bearing disclosure: this
backend is Emscripten-only, and `SDL_Init(SDL_INIT_VIDEO)` itself throws under this repo's `node
CnaTests.js` runner (no real browser DOM) — so this executable is deliberately NOT registered as a
CTest, mirroring `cna_diag_software`'s own precedent. CI only proves it configures/links; a
meaningful PASS/FAIL requires actually running it in a browser (e.g. via `emrun`) against a page
with a real `<canvas>` element.

## Checklist Results
- Frame-1 checks (`GetWindowInternal() != nullptr`, `GetRendererInternal() == nullptr`,
  `GetViewportSize()` positive) correctly assert this backend has a real `SDL_Window` but no
  `SDL_Renderer` — an architecturally distinct claim from SDL_RENDERER's own backend, consistent
  with Canvas drawing directly via HTML Canvas 2D `putImageData()`/canvas draw calls rather than
  through SDL's renderer abstraction.
- The 2x2 RGBA8 test texture (one distinct color per pixel) is explicitly chosen to exercise a
  real `putImageData()` call with genuinely distinguishable per-pixel content, not a
  degenerate solid-color texture that could pass even with transposed/scrambled pixel data.

## Detailed Findings
None.

## Cross-File Observations
Shares the "not registered as a CTest, needs a real runtime environment this repo's CI can't
provide" pattern with `cna_diag_software`/`d3d12_swapchain_diag.cpp` (also audited in this batch) —
each backend with a real-environment-only requirement documents it explicitly rather than either
silently registering a test that would always fail in CI, or silently omitting verification
entirely.

## Missing or Weak Tests
The file's own header comment already discloses the real limitation (CI can't meaningfully execute
this); no additional gap beyond what's disclosed. There does not appear to be a companion CI job
(e.g. an `emrun`-based headless-browser CTest) that would give this file real, automated coverage —
worth flagging as a genuine, disclosed gap rather than a defect in this file itself.

## Positive Findings
The header comment's precise technical explanation of exactly why CI can't run this
(`SDL_Init(SDL_INIT_VIDEO)` throwing "ReferenceError: window is not defined" under Node, not a
vague "needs a browser") is a valuable, specific piece of engineering documentation.

## Final Assessment
No findings in this file. Notes (not a defect in this file) that the Canvas backend's real
smoke-test coverage depends entirely on manual/out-of-CI execution, per its own disclosed
limitation.
