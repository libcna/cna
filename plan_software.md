# Software (CPU) Rasterizer Graphics Backend — Implementation Plan

> **Status: planning, 2026-07-13.** No code written yet. This plan captures the design and a
> phased task breakdown for `CNA_GRAPHICS_BACKEND=SOFTWARE`, proposed by the project owner as a
> testing-oriented backend, not a sixth backend meant for real gameplay.
>
> **Status legend:** ✅ implemented *and verified against its stated acceptance criteria*;
> 🟨 code or documentation exists but has not met those criteria; ⬜ not implemented.

---

## Why this backend, in the owner's own words

> "Toto není backend pro běžné hraní, ale mohl by být překvapivě hodnotný... Pro kvalitu CNA by
> mohl mít větší hodnotu než šestý hardwarový backend."
> (This isn't a backend for regular gameplay, but could be surprisingly valuable... For CNA's
> quality, it could have more value than a sixth hardware backend.)

Stated use cases (verbatim, translated): headless CI; deterministic screenshot tests; testing
without a GPU; verifying basic XNA primitive operations; server environments; diagnosing
differences between backends; fallback when GPU initialization fails.

**How this differs fundamentally from the `HEADLESS` backend** (see `plan_headless.md`): Headless
proves "this code ran, with these arguments, this many times, and nothing leaked" but never
produces a real pixel — `ReadBackbuffer()` just reports the last `Clear()` color for every pixel.
Software is the opposite: it actually rasterizes real triangles into a real, CPU-owned RGBA8
framebuffer, so `GetBackBufferData()`/`ReadBackbuffer()` return genuinely correct pixels — this is
the entire point. It gives this project golden-image-style pixel tests (see
`docs/graphics-backend-feature-matrix.md`) that need **no GPU, no display server, and no driver**
at all, unlike the existing EasyGL/BGFX/Vulkan pixel tests which all need a real GPU context even
when run under Xvfb.

---

## Design decisions (recorded before implementation, not left implicit)

1. **Correctness and determinism over performance.** This is explicitly not a real-time gameplay
   backend — no SIMD, no multithreading, no tiling/binning in v1. Plain scalar loops are fine; a
   slow-but-obviously-correct rasterizer is more valuable here than a fast one that's subtly wrong,
   since the whole point is to be a trustworthy reference/diagnostic tool.
2. **Vertex format inference by stride, not a new common-interface change.** `IVertexBufferBackend`
   only ever receives raw bytes + a stride (`SetData(const void*, int vertex_count,
   std::size_t stride_in_bytes)`) — no `VertexDeclaration` is threaded through it. This project
   already has an established precedent for inferring vertex layout from stride alone (see the
   WebGPU backend's own stride-keyed pipeline selection: 16/20/24/32-byte strides map to
   `VertexPositionColor`/`VertexPositionTexture`/`VertexPositionColorTexture`/
   `VertexPositionNormalTexture` respectively). Software reuses that exact convention rather than
   inventing a new one or making a larger, riskier shared-interface change. v1 supports the 16/20/24
   strides (position+color, position+texture, position+color+texture); 32-byte
   (position+normal+texture) is deferred since lighting is out of scope for v1 anyway.
3. **The CPU framebuffer/depth buffer are the backend's real state, not a fiction.** A color buffer
   (RGBA8) and a depth buffer (float32) sized to the current render target (bound `RenderTarget2D`,
   or the backbuffer's `PresentationParameters` size when none is bound) back every operation for
   real. `Present()` is a no-op by default (matches the headless/server/CI use cases) — see
   `SOFTWARE-70` for an optional, later, opt-in "blit to a real window for visual inspection" mode,
   which is explicitly NOT required for this backend's stated value proposition.
4. **No window by default**, mirroring `HEADLESS`'s own `GraphicsDevice`/`Game::Run()` integration
   (`plan_headless.md` design decision 2): `#ifdef CNA_BACKEND_HEADLESS` guards in
   `GraphicsDevice.cpp`'s constructor and `createOrAttachWindow()` are extended to also cover
   `CNA_BACKEND_SOFTWARE` (`#if defined(CNA_BACKEND_HEADLESS) || defined(CNA_BACKEND_SOFTWARE)`) —
   a small, mechanical extension of an existing pattern, not a new one.
5. **SpriteBatch reuses the same triangle rasterizer as 3D draws.** A `SpriteBatch::Draw()` call is
   just a textured quad (2 triangles) with a transform matrix; feeding it through the same
   rasterizer core used for `DrawPrimitivesEx` gives this backend a real, pixel-correct 2D sprite
   path essentially for free — a capability `HEADLESS` never had (its `SpriteBatch` only records
   call arguments, never renders anything).
6. **`BasicEffect` subset only, no lighting/fog in v1.** `GpuDrawParams`' `vertexColorEnabled`,
   `textureEnabled`/`texture0`, and `diffuseColor` (material color/alpha) drive v1 pixel shading.
   `lightingEnabled`, `fogEnabled`, `dualTexture`, `envMapping`, `skinned` are all explicitly out of
   scope for v1 (matches the owner's own stated minimal first-version list — see "Boundaries").
7. **Only two blend modes in v1**: `Opaque` (direct overwrite, no blend) and `AlphaBlend`
   (`SrcAlpha`/`InvSrcAlpha`, the two most common real XNA/FNA `BlendState` presets). Other
   `ApplyBlendState(...)` factor/op combinations fall back to `AlphaBlend` behavior in v1 rather
   than attempting a fully general blend-equation interpreter — a real scope limitation, recorded
   honestly rather than silently claimed complete (see `SOFTWARE-42`).
8. **Custom `Effect` (GLSL/HLSL/WGSL source) cannot execute on the CPU**, same limitation
   `HEADLESS-16` already documents for that backend: accepts any effect source without compiling
   it, but only actually renders correctly for effects whose `FillGpuDrawParams()` output maps onto
   what v1's fixed pixel-shading path understands (see design decision 6). A custom effect with
   genuinely custom shader logic will not visually match its GPU-backend rendering under Software —
   documented as a known, permanent-for-v1 limitation, not a bug.

---

## Active execution order — do this one phase at a time

1. Phase S1 (CMake integration + skeleton) unblocks everything else, exactly as `plan_headless.md`
   Phase N1 did for that backend.
2. Phase S2 (framebuffer/render targets) and Phase S3 (vertex/index storage + format inference) are
   independent of each other and can be done in either order, but both must land before Phase S4.
3. Phase S4 (rasterizer core: transform → clip → rasterize → depth test) is the heart of this
   backend — get a single flat-colored, non-textured, non-blended triangle rendering and
   pixel-verified correct (`SOFTWARE-60`'s first test) before adding Phase S5's pixel-shading
   features on top.
4. Phase S5 (blending/texture/vertex-color) builds directly on S4's per-pixel loop.
5. Phase S6 (effect integration, SpriteBatch reuse) is the actual point of this backend — like
   `plan_headless.md` Phase N6, it should be verified continuously against Phase S4/S5's already-
   proven core, not left to the end.
6. Phase S7 (tests) — per this project's own convention (`CLAUDE.md`), add test coverage in the
   same task that implements each capability, not bolted on afterward. `SOFTWARE-60`'s pixel tests
   in particular are this backend's actual reason to exist — do not skip them.
7. Phase S8 (docs) — write `docs/software-backend.md` as capabilities land, not all at the end.

For every task: build the affected target(s), run the relevant tests, and do not mark a task ✅
without both.

---

## Phase S1 — CMake integration and skeleton

| # | Task | Status | Notes |
|---|---|---|---|
| SOFTWARE-1 | Add `"SOFTWARE"` to `CNA_GRAPHICS_BACKEND`'s CMake `STRINGS` property and a matching `CNA_BACKEND_SOFTWARE` option flag, following the exact existing pattern for `SDL_RENDERER`/`EASYGL`/`BGFX`/`VULKAN`/`WEBGPU`/`HEADLESS` | ⬜ | |
| SOFTWARE-2 | `cna_backend_graphics_software` static library target (`elseif(CNA_GRAPHICS_BACKEND STREQUAL "SOFTWARE")` block, mirrors `HEADLESS`'s own) | ⬜ | No external deps needed beyond what every backend already requires (SDL3 for windowing types only, never SDL's rendering/GL/Vulkan surface). |
| SOFTWARE-3 | `include/CNA/Internal/Backends/Software/SoftwareGraphicsBackend.hpp` + `.cpp`: class implementing every `IGraphicsBackend` pure virtual — initially real where Phase S1 can make it real (Clear/Present/viewport), honest no-op/throwing stubs elsewhere until later phases replace them | ⬜ | |
| SOFTWARE-4 | Factory dispatch for `SOFTWARE`; extend the existing `#ifdef CNA_BACKEND_HEADLESS` guards in `GraphicsDevice`'s constructor and `createOrAttachWindow()` to also cover `CNA_BACKEND_SOFTWARE` (design decision 4) | ⬜ | |

---

## Phase S2 — Framebuffer and render targets

| # | Task | Status | Notes |
|---|---|---|---|
| SOFTWARE-10 | CPU color framebuffer (RGBA8, `std::vector<uint8_t>`) sized to the backbuffer (`PresentationParameters`); real `Clear(r,g,b,a)` fills every pixel | ⬜ | |
| SOFTWARE-11 | CPU depth buffer (`std::vector<float>`), real `ClearDepth`/depth-inclusive `Clear*` variants | ⬜ | |
| SOFTWARE-12 | `SoftwareRenderTargetBackend`: `CreateRenderTarget2D`/`SetRenderTarget2D` binds an alternate CPU color+depth buffer pair sized to that target, instead of the default backbuffer pair | ⬜ | |
| SOFTWARE-13 | `ReadBackbuffer()`/`GraphicsDevice::GetBackBufferData()`: real `memcpy` from the currently-bound CPU framebuffer — no faking, this is the backend's core value proposition | ⬜ | |
| SOFTWARE-14 | `Present()`: no-op by default, matching the headless/CI/server use cases | ⬜ | |

---

## Phase S3 — Vertex/index storage and format inference

| # | Task | Status | Notes |
|---|---|---|---|
| SOFTWARE-20 | `SoftwareVertexBufferBackend`: stores raw bytes + stride (real storage, not discarded, mirroring `HeadlessVertexBufferBackend`'s own `ShadowData()` pattern) | ⬜ | |
| SOFTWARE-21 | `SoftwareIndexBufferBackend`: 16- and 32-bit, real storage | ⬜ | |
| SOFTWARE-22 | Stride-based vertex format inference (design decision 2): 16→`VertexPositionColor`, 20→`VertexPositionTexture`, 24→`VertexPositionColorTexture`. 32-byte (`VertexPositionNormalTexture`) explicitly deferred (needs lighting, out of scope for v1) | ⬜ | |

---

## Phase S4 — Rasterizer core

| # | Task | Status | Notes |
|---|---|---|---|
| SOFTWARE-30 | Per-vertex CPU transform: `World * View * Projection` (matching CNA's existing `Matrix` row/column convention) → clip space | ⬜ | |
| SOFTWARE-31 | Perspective divide + viewport transform → screen-space X/Y/Z | ⬜ | |
| SOFTWARE-32 | Triangle rasterization core: edge-function/barycentric fill, per-pixel depth test (`LessEqual`, matching `DepthStencilState::Default`) against the bound depth buffer, write-on-pass | ⬜ | |
| SOFTWARE-33 | Perspective-correct attribute interpolation (vertex color, UV) across the triangle | ⬜ | |
| SOFTWARE-34 | Basic near-plane handling: at minimum, cull triangles entirely behind the near plane; full polygon near-plane clipping (splitting a triangle into 1-2 new triangles) is a known v1 limitation, flagged here for a likely follow-up task once basic rendering is proven correct | ⬜ | Near-plane clipping bugs are a classic rasterizer failure mode — do not skip verifying this with an actual test scene that has geometry crossing the near plane once Phase S4 is otherwise working. |

---

## Phase S5 — Pixel shading

| # | Task | Status | Notes |
|---|---|---|---|
| SOFTWARE-40 | Vertex-color modulation (`vertexColorEnabled` path) | ⬜ | |
| SOFTWARE-41 | Nearest-neighbor texture sampling (`textureEnabled`/`texture0`), reading the already-stored CPU-side pixel data every `ITextureBackend` implementation already keeps | ⬜ | Bilinear sampling is a reasonable v2 stretch goal, not required for v1. |
| SOFTWARE-42 | Basic blending: `Opaque` (direct write) and `AlphaBlend` (`SrcAlpha`/`InvSrcAlpha`) only in v1 (design decision 7); other `ApplyBlendState(...)` inputs fall back to `AlphaBlend` behavior rather than a general blend-equation interpreter | ⬜ | |
| SOFTWARE-43 | `diffuseColor`/alpha modulation from `GpuDrawParams` (BasicEffect's material color) | ⬜ | |

---

## Phase S6 — Effect integration

| # | Task | Status | Notes |
|---|---|---|---|
| SOFTWARE-50 | Wire `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx` to the Phase S4/S5 rasterizer using `GpuDrawParams`' `vertexColorEnabled`/`textureEnabled`/`texture0`/`diffuseColor`/`worldColMajor` fields; `lightingEnabled`/`fogEnabled`/`dualTexture`/`envMapping`/`skinned` explicitly out of scope for v1 | ⬜ | |
| SOFTWARE-51 | `SpriteBatch` reuses the same rasterizer for its quad draws (design decision 5) — a real, pixel-correct CPU `SpriteBatch` path | ⬜ | |
| SOFTWARE-52 | Custom `ShaderEffect` (arbitrary GLSL/HLSL/WGSL source): accept without compiling (mirrors `HEADLESS-16`), document that only effects whose `FillGpuDrawParams()` output matches v1's fixed pixel-shading path will render correctly (design decision 8) | ⬜ | |

---

## Phase S7 — Tests

| # | Task | Status | Notes |
|---|---|---|---|
| SOFTWARE-60 | Deterministic pixel tests via real `GetBackBufferData()` readback, no GPU/display needed: clear-color test, single flat-colored triangle test, single textured quad test | ⬜ | This is this backend's actual reason to exist — do not defer or skip. Existing `PixelTestGame` (`examples/common/PixelTestGame.hpp`) likely assumes a real window; check whether it needs a Software-specific variant, mirroring how `HEADLESS` needed its own test scaffolding rather than reusing windowed test harnesses as-is. |
| SOFTWARE-61 | (Stretch goal) Cross-backend diagnostic test: render an identical simple scene on `SOFTWARE` and a real GPU backend, compare within a tolerance — directly serves the owner's stated "diagnostika rozdílů mezi backendy" use case, but depends on a real GPU backend being available in the test environment; may not be runnable in every CI | ⬜ | |
| SOFTWARE-62 | CTest registration, mirroring `HEADLESS`'s own `cna_headless_test`-style CMake macro | ⬜ | |

---

## Phase S8 — Docs

| # | Task | Status | Notes |
|---|---|---|---|
| SOFTWARE-70 | `docs/software-backend.md`: what it's for/not for, current capability boundary, how to write a test, known limitations — mirrors `docs/headless-backend.md`/`docs/webgpu-backend.md`'s structure. Optionally document an opt-in "blit the CPU framebuffer to a real window" mode here if it ends up being worth adding (design decision 3) | ⬜ | |
| SOFTWARE-71 | `docs/graphics-backend-feature-matrix.md`: unlike `HEADLESS` (which was deliberately kept OUT of that matrix, since it never renders a real pixel), `SOFTWARE` actually could become a genuine pixel-parity column once mature enough — flag as a real future opportunity rather than deciding now, since v1's feature set is too narrow for a meaningful comparison yet | ⬜ | |

---

## Boundaries (stop and ask, don't improvise)

- **GPU-init-failure fallback** (one of the owner's stated use cases) is a `Game`/
  `GraphicsDeviceManager`-level runtime policy decision (auto-switching the active backend when GPU
  init fails) — a separate, larger, cross-cutting feature, not part of this backend's own scope.
  Flag it as a possible follow-up plan if wanted later; do not fold it into this one.
- Full `BasicEffect` lighting/fog, `DualTextureEffect`/`EnvironmentMapEffect`/`SkinnedEffect`,
  `Model.Draw()` with real skinning, MRT, MSAA, mipmapping, anisotropic filtering, cube/3D
  textures — all explicitly out of scope for v1, matching the owner's own minimal first-version
  list verbatim (clear; render target; triangle list; basic blending; depth buffer; simple
  textures; vertex colors; `BasicEffect` subset).
- Performance/SIMD/multithreading work is explicitly not a goal (design decision 1) — do not
  spend effort here unless a specific test becomes impractically slow.
- Do not let `SOFTWARE`-specific code leak into the shared `IGraphicsBackend`/`GpuDrawParams`
  interface layer beyond what a genuine common-interface need justifies — same backend-locality
  rule the other backends (`CLAUDE.md`, `plan_webgpu.md`/`plan_headless.md`'s own boundaries)
  already follow.
- If Phase S4's rasterizer core turns out to need real polygon near-plane clipping sooner than
  expected (visible artifacts in even simple test scenes), treat that as a legitimate scope
  addition to flag and discuss, not something to silently skip or silently half-implement.
