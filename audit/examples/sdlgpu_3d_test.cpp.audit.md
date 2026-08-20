# Audit: examples/sdlgpu_3d_test.cpp

## Metadata

- Source file: `examples/sdlgpu_3d_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlgpu` shard — SDL_GPU backend core 3D vertex-format / `BasicEffect`
  smoke test
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_sdlgpu_test(cna_test_sdlgpu_3d …)` / `cna_register_backend_test(NAME SdlGpu_3D …)`,
  `cmake/Tests/SdlGpuTests.cmake:31-33`, `TIMEOUT 60`).
- XNA/FNA relevance: direct — `VertexBuffer`/`VertexPositionColor`/`VertexPositionTexture`/
  `VertexPositionColorTexture`/`VertexPositionNormalTexture`, `BasicEffect` (`VertexColorEnabled`,
  `TextureEnabled`, `EnableDefaultLighting()`), `GraphicsDevice.DrawPrimitives`,
  `SetDepthTestEnabled`/`SetDepthWriteEnabled` (`NOXNA` extension over FNA's `DepthStencilState`).
- Related production code: `src/CNA/Internal/Backends/SdlGpu/SdlGpuGraphicsBackend.cpp`
  (`DrawPrimitivesEx` dispatch-by-stride, `GetOrCreatePipelineColored3D`/`Textured3D`/
  `LitTextured3D`), `src/CNA/Internal/Backends/SdlGpu/shaders/{colored3d,textured3d,
  colored_textured3d,lit_textured3d}.{vert,frag}.glsl`, `src/Microsoft/Xna/Framework/Graphics/
  BasicEffect.cpp`.

## Purpose

Six-check proof that the SDL_GPU backend's core 3D vertex-format dispatch (`DrawPrimitivesEx`'s
stride-based `GpuDrawParams` routing) is real: (A) a `VertexPositionColor` triangle with
`VertexColorEnabled=true`; (B) a real depth-test pair — a nearer blue quad drawn *before* a
farther red quad, with `SetDepthTestEnabled(true)`/`SetDepthWriteEnabled(true)`, proving the depth
buffer genuinely occludes rather than merely "didn't throw"; (C) a `VertexPositionTexture` quad
through `BasicEffect.Texture`; (D) a `VertexPositionColorTexture` quad (vertex-color tint over a
texture); (E) a `VertexPositionNormalTexture` quad through `BasicEffect.EnableDefaultLighting()`;
(F) the whole five-draw scene surviving 120 frames. Placement and namespace usage are correct.

## Executive Verdict

**Needs attention** — the six checks are individually well-chosen (each targets a genuinely
distinct vertex-stride/shader-family dispatch path, and check B is a real depth-test proof, not
just a draw-call proof), but the frame-1 code path (lines 249-330) is a hand-duplicated copy of
`DrawScene()` (lines 171-241) rather than a call to it, creating a drift risk between what is
exception-checked and what actually renders on every other frame (F1). No pixel-level assertion
exists for any of the six checks (same documented swapchain-readback limitation as
`sdlgpu_2d_test.cpp`'s own report), so the depth-test claim in check B is unverified by the
automated CTest itself, resting on a manual screenshot.

## Checklist Results

### API / XNA / FNA parity
`BasicEffect.VertexColorEnabled` is set as a bare public field (`coloredFx.VertexColorEnabled =
true;`), consistent with `AUDIT_CROSS_CUTTING_FINDINGS.md`'s already-confirmed observation
(via `bgfx_basiceffect_texture_vertexcolor_enabled_test.cpp` /
`vulkan_basiceffect_vertexcolor_enabled_test.cpp`) that `BasicEffect::VertexColorEnabled` is the
one property on the class with no `getXProperty()`/`setXProperty()` wrapper — this file simply
uses the API as it currently exists; the defect (if it is one, per this project's own `CLAUDE.md`
convention) lives in `BasicEffect.hpp`, not here. `SetDepthTestEnabled`/`SetDepthWriteEnabled` are
not part of the FNA `GraphicsDevice` surface (FNA exposes this via `DepthStencilState`) — correctly
not used inside the `Microsoft::Xna` namespace's own API surface question, since this is a CNA
example file calling whatever public API `GraphicsDevice` actually exposes; whether these methods
themselves should be `NOXNA`-tagged is a finding for `GraphicsDevice.hpp`'s own audit, not this
file's.

### Behavioral correctness
Independently re-derived check B's discriminating claim: `nearQuad` (blue, world Z=+0.5 via
`CreateTranslation(-1.8f, 0.0f, 0.5f)`) is drawn *first*, `farQuad` (red, world Z=-0.5) *second*,
into the *same* screen-space rectangle (both use `makeColorQuad`'s identical XY footprint), with
the camera at `(0,0,10)` looking at the origin — so `nearQuad`'s vertices (Z=+0.5, closer to the
camera) are genuinely nearer than `farQuad`'s (Z=-0.5). With `SetDepthTestEnabled(true)`, a correct
depth buffer must keep the near (blue) quad visible even though the far (red) quad is drawn on
top afterward — this is a real, unambiguous depth-test discriminator by construction (last-drawn
would win with depth testing off, first-drawn must win with it genuinely on), not merely "did it
throw." However, the CTest itself never reads back the resulting pixel to confirm this — see F1
and the cross-reference to `sdlgpu_draworder_test.cpp`'s report, which shows this exact
"read back the centre pixel" technique is both known to this project and readily available via
`RenderTarget2D::GetData()` (already proven working for this backend per `plans/plan_sdlgpu.md`
`SDLGPU-39`), just not applied here.

### Logic
The frame-1 branch (lines 249-330) is a line-by-line duplicate of `DrawScene()`'s five draw calls
(lines 171-241), re-typed with individual `check(true, …)` calls interleaved and a `stage` string
tracking which draw was in flight when an exception is caught — see F1 for the maintainability/
test-integrity risk this creates. Confirmed via direct comparison: both blocks construct the same
five `BasicEffect` instances with the same `World`/`View`/`Projection`/`VertexColorEnabled`/
`TextureEnabled`/`EnableDefaultLighting()` calls and the same `DrawPrimitives` counts, in the same
order.

### C++ correctness
All `VertexBuffer`s are constructed with `BufferUsage::None` (not `WriteOnly`), correct for
`SetData()`-populated, CPU-shadowed static geometry. `dev.SetVertexBuffer(nullptr)` at the end of
both `DrawScene()` and the frame-1 branch correctly clears the bound vertex buffer before the next
`Draw()` call — consistent hygiene, avoids a stale-binding surprise on the next frame.

### Robustness
The `try`/`catch` around the frame-1 scene tracks a `stage` string updated before each draw so a
thrown exception's `check(false, …)` message names which specific draw failed — good diagnostic
design, matches this shard's convention (`sdlgpu_effects_test.cpp` uses the identical pattern).

### Testing
Covers all five core 3D vertex-format/shader-family dispatch paths this backend currently
implements for `BasicEffect`, plus a real (if unverified-by-readback) depth-test proof. Missing:
any assertion on the *content* of what was rendered (F1); a case where `VertexColorEnabled` and
`TextureEnabled` are *both* true is exercised by check D, but no check exercises `TextureEnabled`
false + `VertexColorEnabled` false together with a non-white color to confirm the flat/untextured
path — minor gap, low priority since that combination is presumably covered by the shared
`xna-graphics`/other-backend test suites already.

## Detailed Findings

### F1 — Frame-1 scene is a hand-duplicated copy of `DrawScene()`, not a call to it — a change to one path silently stops matching the other

- Severity: MEDIUM
- Confidence: HIGH (direct line-by-line comparison of the two blocks)
- Category: maintainability / test-integrity
- Location/symbol: `Draw()`'s frame-1 branch (lines 249-330) vs. `DrawScene()` (lines 171-241)
- Evidence: both blocks construct five separately-named `BasicEffect` instances
  (`coloredFx`/`depthFx`/`texFx`/`coloredTexFx`/`litFx`) with identical property setters and
  `DrawPrimitives` calls, but the frame-1 copy additionally wraps each stage in a `stage =` string
  update and a `check(true, …)` call, meaning it cannot simply delegate to `DrawScene()` without
  losing that per-stage diagnostic granularity — this is presumably *why* the duplication exists,
  but it was not factored to avoid the drift risk (e.g., a shared parameterized `DrawStage(int
  stageIndex)` helper, or having `DrawScene()` itself return/report per-stage status).
- Why it matters: if `DrawScene()` (the function that actually renders every frame after frame 1)
  is edited — a new draw added, an existing one's `World`/effect settings changed — and the
  frame-1 copy is not updated in lockstep, the CTest's "N/6 checks passed" result would no longer
  correspond to what the steady-state 120-frame render loop actually does. A regression introduced
  only in `DrawScene()`'s copy (e.g., a typo'd translation, a forgotten `EnableDefaultLighting()`)
  would render incorrectly for 119 of 120 frames while the CTest still reports 6/6 PASS, since the
  frame-1 checks only observe the *separately-maintained* copy's own exception behavior, not
  `DrawScene()`'s.
- FNA/XNA comparison: N/A — test-authoring risk, not an XNA/FNA behavior question.
- Related files: `examples/sdlgpu_effects_test.cpp` (this batch's other file with the identical
  duplication pattern — see that report's own F1).
- Suggested future action (not implemented by this audit): factor the five draw stages into
  a small `void DrawStage(int index)` (or five named private methods) called from both
  `DrawScene()` and the frame-1 `try` block with per-stage `check()` calls wrapping each
  individual call from the *shared* implementation, eliminating the duplicate-source-of-truth risk
  while preserving the current per-stage diagnostic granularity.

## Cross-File Observations

- Shares `MakeQuadrantTexturePixels()` (copy-pasted, not shared) with `sdlgpu_2d_test.cpp` and
  `sdlgpu_effects_test.cpp` — see that file's Cross-File Observations for the same note.
- `SetDepthTestEnabled`/`SetDepthWriteEnabled` are called directly on `GraphicsDevice`, not via a
  `DepthStencilState` object — worth cross-checking during the `xna-graphics`/`backend-sdlgpu`
  shard audits whether this is this project's own established `NOXNA` convention for 3D depth
  control across all backends (consistent with prior batches' note that `SdlRenderer`/`Dx3`
  backends follow an explicit-throw-when-unsupported discipline) or a SdlGpu-specific shortcut.
- Check B's real depth-test proof technique (draw near-then-far into the same screen rectangle
  with depth testing on, expect the near object to still win) is architecturally identical to
  `sdlgpu_draworder_test.cpp`'s "last write wins" technique for chronological order (draw two
  opaque things into the same rectangle, read back the centre pixel) — that file actually reads
  back the pixel via `RenderTarget2D::GetData()`; this file does not, despite the mechanism being
  proven to work on this exact backend (see F1's cross-reference).

## Missing or Weak Tests

- No pixel-level assertion for any of the six checks (same swapchain-readback limitation
  documented in `sdlgpu_2d_test.cpp`'s report) — check B's depth-test claim in particular is
  currently unverified by CI beyond "did not throw," even though this backend has a working
  `RenderTarget2D::GetData()` readback path (proven by `sdlgpu_draworder_test.cpp` and
  `sdlgpu_envmap_test.cpp` in this same batch) that could be applied here by rendering into an
  offscreen `RenderTarget2D` instead of the swapchain.
- No boundary/invalid-argument case (e.g., `DrawPrimitives` with a zero primitive count, or a
  vertex buffer stride the backend doesn't recognize) — acceptable for a smoke test, but worth
  noting as a gap relative to the full per-file checklist's "boundary, invalid-argument" section.

## Positive Findings

- Check B's near/far depth-test construction is a genuinely well-designed discriminator: drawing
  the nearer object first and the farther object second, into the identical screen footprint, is
  exactly the right technique to prove a depth buffer is real (a depth-test-off implementation
  would show the farther, later-drawn object winning; this test's geometry makes that failure mode
  detectable if it were ever pixel-checked).
- Exercises all five distinct vertex-stride/shader-family combinations this backend implements
  for `BasicEffect` in one coherent scene, giving good breadth for a single file.
- The `stage`-tracking `try`/`catch` pattern gives genuinely useful failure diagnostics (which of
  the five draws failed) beyond a bare pass/fail.

## Final Assessment

A well-conceived smoke test whose main defect is structural, not behavioral: hand-duplicating the
scene between the checked frame-1 path and the steady-state `DrawScene()` creates a real,
concrete drift risk (F1) that this project's own established pattern (`RenderTarget2D::GetData()`
readback, already proven elsewhere in this exact shard) could additionally strengthen by replacing
some "did it throw" checks with real pixel assertions, particularly for check B's depth-test claim.
