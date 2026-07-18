# Audit: examples/easygl_mrt_test.cpp

## Metadata

- Source file: `examples/easygl_mrt_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — Multi-Render-Target (`SetRenderTargets`) pixel test
- File type: `Game`-derived executable, CTest-registered as `cna_test_easygl_mrt` /
  `EasyGL_MRT_TwoAttachments` (`cmake/Tests/EasyGLTests.cmake:761-763`)
- XNA/FNA relevance: direct — `GraphicsDevice::SetRenderTargets(vector<RenderTargetBinding>)`,
  `RenderTarget2D`, `BasicEffect`
- Production sources cross-checked: `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp`
  (`SetRenderTargets`), `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp`
  (`EasyGLGraphicsBackend::SetRenderTargets`, lines 1742-1793; the BasicEffect-family GLSL fragment
  shaders, e.g. lines 2621/2681/2745/2829/2971/3048, all declaring exactly one `out vec4 FragColor`)

## Purpose

Verifies EasyGL's MRT path: clears `rt0` red and `rt1` blue independently, then binds both via
`SetRenderTargets({RenderTargetBinding(rt0), RenderTargetBinding(rt1)})` and draws a single green
quad, expecting the draw to land **only** on attachment 0 (`rt0` → green) while attachment 1 (`rt1`)
is left untouched (stays blue) — because the drawn shader (`BasicEffect`, vertex-colored) only writes
one fragment-shader output. It then blits both render targets back to the default framebuffer's left/
right halves and reads back a pixel from each to confirm the expected per-attachment result.

## Executive Verdict

**Healthy.** The test's central claim — that a single-output fragment shader drawn into an MRT FBO
only writes attachment 0 — was independently confirmed by reading every `BasicEffect`-family GLSL
fragment shader source embedded in `EasyGLGraphicsBackend.cpp`; none declares more than one `out
vec4` variable, so the expected behavior follows necessarily from the actual shader code, not merely
from the test's own assumption.

## Checklist Results

### API / XNA / FNA parity
Uses `GraphicsDevice::SetRenderTarget(RenderTarget2D*)` (single-RT convenience overload) for the
initial red/blue clears, then `GraphicsDevice::SetRenderTargets(const
std::vector<RenderTargetBinding>&)` for the actual MRT bind, and `SetRenderTargets({})` to return to
the backbuffer — matches FNA's `GraphicsDevice.SetRenderTarget`/`SetRenderTargets` API shape
(single-target convenience overload plus a `RenderTargetBinding[]`/vector overload for MRT, with an
empty array meaning "back to the backbuffer").

### Behavioral correctness
Traced `EasyGLGraphicsBackend::SetRenderTargets` (`EasyGLGraphicsBackend.cpp:1742-1793`): for
`count >= 2` it creates/reuses a dedicated `mrtFbo_`, attaches each `EasyGLRenderTargetBackend`'s
color texture to `GL_COLOR_ATTACHMENT0+i` via `attach_texture_2d`, and calls `set_draw_buffers` for
exactly `n` (here 2) draw buffers — so both `rt0` and `rt1`'s underlying GL textures are genuinely
bound as simultaneous MRT color attachments during the green-quad draw, not merely one of them.

Cross-checked that `BasicEffect`'s compiled fragment shader (the vertex-color variant used here,
`fxGreen.VertexColorEnabled = true`) declares only `out vec4 FragColor;` and writes to it once
(e.g. the vertex-color shader source embedded around `EasyGLGraphicsBackend.cpp:2716-2749`) — with no
`layout(location = 1) out …` companion variable, GLSL's default output-location assignment binds
`FragColor` to location 0 only, so `gl_COLOR_ATTACHMENT1` (`rt1`) genuinely receives no fragment data
from this draw call regardless of the FBO's draw-buffer configuration — the test's own header comment
("colored3D shader (stride=16) writes to layout(location=0) → only attachment 0 receives green")
is accurate.

`SetRenderTargets({})` (line 106) drives `GraphicsDevice::SetRenderTargets` down to
`backend_->SetRenderTargets(nullptr, 0)` (per `GraphicsDevice.cpp:1925`), which
`EasyGLGraphicsBackend::SetRenderTargets` maps to its `count <= 0` branch → `SetRenderTarget2D(nullptr)`
→ `BindDefaultFramebuffer()` (`EasyGLGraphicsBackend.cpp:1742-1747, 1723-1727`) — correctly returns to
the real backbuffer before the two blit draws and the final `Clear`.

### Logic
`RasterizerState::CullNone` (line 102) is applied before the green MRT draw only — the two later blit
draws (`VertexPositionTexture` full-screen quads) are drawn with whatever rasterizer state was
already active (`CullNone`, since it is never reset), so they are not silently culled either; this is
implicit rather than explicit but not a bug given no rasterizer-state change happens in between.

### Memory/resource lifetime
`rt0_`/`rt1_` are member `unique_ptr<RenderTarget2D>`s created once in `Initialize()` and read/written
across both `Initialize()` and `Draw()` — straightforward, no lifetime concerns. The three local
`BasicEffect` instances (`fxGreen`, and the two `blit` locals in their own nested scopes) are
correctly scoped to just the draw call that uses them.

### Robustness
`result_` defaults to `1` (fail) and is only set to `0` on the explicit `leftGreen && rightBlue`
success path (lines 41, 173) — a thrown exception or an early return would correctly leave the test
reporting failure rather than silently exiting 0, which is the right default-to-fail posture for a
pixel test.

### Testing
Uses generous but reasonable channel thresholds (`<=50`/`>=200` per channel, lines 161-166) rather
than exact equality, appropriate for a real rasterized-and-blitted-through-a-second-quad pixel
readback (unlike a direct single-draw `GetBackBufferData` sample, this value has gone through an
extra texture-sample + draw + blit round trip, so some tolerance is reasonable). Both attachments are
checked in the same test (not just "did we get some green somewhere"), so the assertion is
symmetric and would catch either a "wrote to both" or a "wrote to neither" regression.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings.

### F1 — MRT-mode render targets are not mip-regenerated on unbind (accepted, pre-existing gap)

- Severity: INFO
- Confidence: HIGH (documented directly in the source)
- Category: architecture (pre-existing, not introduced or exercised incorrectly by this test)
- Location/symbol: `EasyGLGraphicsBackend::SetRenderTargets` (`EasyGLGraphicsBackend.cpp:1755-1761`)
- Evidence: the backend's own comment states MRT targets are never tracked as
  `currentRt2D_`/`currentRtCube_`, so mip regeneration on switch-away is skipped for MRT-bound
  targets ("Task 336... an accepted, documented gap").
- Why it matters for this file: irrelevant to this specific test, since neither `rt0_` nor `rt1_` is
  constructed with mipmaps (`RenderTarget2D(device, W, H)`, the no-mipmap overload) — noted only as
  context for any future MRT+mipmap test in this shard.

## Cross-File Observations

- This test is a solid empirical confirmation that CNA's `BasicEffect`-derived GLSL shaders are all
  genuinely single-output — worth keeping in mind for any future MRT test that tries to prove the
  *opposite* (i.e. that a custom multi-output `ShaderEffect` correctly reaches every bound
  attachment), which would need a different (non-`BasicEffect`) shader to be a meaningful test.

## Missing or Weak Tests

- No test in this immediate scope exercises MRT with more than 2 attachments, or with a genuinely
  multi-output custom shader (i.e. proving the *positive* case — that a shader which explicitly
  writes multiple `out` variables reaches every bound attachment, not just that a single-output
  shader doesn't spuriously write extra ones).

## Positive Findings

- The test's central assumption (single-output shader → single-attachment write) was verified against
  real shader source rather than accepted at face value, and holds.
- Symmetric, two-sided pixel assertion (`leftGreen && rightBlue`) rather than a single one-sided
  check.

## Final Assessment

A correctly constructed, evidence-matching MRT regression test; its one real scope limitation (no
genuinely multi-output shader case) is a coverage gap in the shard, not a defect in this file.
