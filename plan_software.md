# Software (CPU) Rasterizer Graphics Backend — Implementation Plan

> **Status: Phases S1-S8 plus Phase S9 (SOFTWARE-80..84) landed and verified, 2026-07-13.**
> `CNA_GRAPHICS_BACKEND=SOFTWARE` configures, builds, and `CnaTests` (the full pre-existing GTest
> corpus) links and runs cleanly against it -- **4371/4373 pass with `DISPLAY`/`WAYLAND_DISPLAY`
> unset and `SDL_VIDEODRIVER` empty** (2 skips are unrelated hardware-sensor tests that skip on
> every backend). This backend renders a genuinely complete 2D/3D pipeline entirely on the CPU:
> real triangle rasterization (`World*View*Projection` transform, perspective divide, edge-function
> fill, per-pixel depth test, perspective-correct color/UV interpolation), real near-plane polygon
> clipping, real backface culling (`RasterizerState.CullMode`), bilinear texture sampling,
> `DiffuseColor` modulation and exact XNA 2D `BlendState` factors/equations, `DualTextureEffect`/
> `EnvironmentMapEffect` (real cube-map reflection)/`SkinnedEffect` (real bone-transform skinning)
> support, and a real pixel-correct `SpriteBatch` path reusing the same rasterizer core -- all
> without any per-light diffuse lighting (design decision 6, still out of v1 scope for every
> effect). Six CTests, 29 checks total: `Software_Smoke` (6/6), `Software_Rasterizer` (5/5,
> **order-independent depth occlusion**), `Software_Effects` (5/5), `Software_Culling` (5/5),
> `Software_Clipping` (4/4), `Software_DualEnvmapSkinned` (4/4) -- all with **no window, no GPU, no
> display server**. Plus a manually-run cross-backend diagnostic (`SOFTWARE-84`) confirming this
> backend's output matches `EASYGL`'s within a max per-channel diff of 1 on a canonical scene.
> `TriangleList` only in v1; no per-light lighting/fog, no MRT/cube-render-target/3D textures (see
> Boundaries).
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
| SOFTWARE-1 | Add `"SOFTWARE"` to `CNA_GRAPHICS_BACKEND`'s CMake `STRINGS` property and a matching `CNA_BACKEND_SOFTWARE` option flag, following the exact existing pattern for `SDL_RENDERER`/`EASYGL`/`BGFX`/`VULKAN`/`WEBGPU`/`HEADLESS` | ✅ | Verified 2026-07-13: configures cleanly with `-DCNA_GRAPHICS_BACKEND=SOFTWARE`; `-DCNA_BACKEND_SOFTWARE=ON` explicit-option form also wired in. |
| SOFTWARE-2 | `cna_backend_graphics_software` static library target (`elseif(CNA_GRAPHICS_BACKEND STREQUAL "SOFTWARE")` block, mirrors `HEADLESS`'s own) | ✅ | Verified 2026-07-13: builds clean, no external deps beyond SDL3 (windowing types only, via the shared `IGraphicsBackend.hpp` forward declarations -- never touches SDL's actual rendering/GL/Vulkan surface). |
| SOFTWARE-3 | `include/CNA/Internal/Backends/Software/SoftwareGraphicsBackend.hpp` + `.cpp`: class implementing every `IGraphicsBackend` pure virtual — initially real where Phase S1 can make it real (Clear/Present/viewport), honest no-op/throwing stubs elsewhere until later phases replace them | ✅ | Verified 2026-07-13: implements every pure virtual (`Clear`/`Present`/`GetViewportSize`/`SetVirtualResolution`/`SetPresentationMode`/`GetWindowInternal`/`GetRendererInternal`/`CreateTexture`/`CreateSpriteBatch`/all 6 `Clear*` variants/`SetDepthTestEnabled`/`SetBlendEnabled`/`SetDepthWriteEnabled`/`CreateVertexBuffer`/`CreateIndexBuffer16`/`DrawColoredPrimitives`/`DrawIndexedColoredPrimitives`) plus the optional extension points a full game needs (render targets, effects, sprite batch). `CnaTests` (the full pre-existing GTest corpus) links and runs cleanly against it (4371/4373 pass, 2 unrelated hardware-sensor skips), confirming interface completeness — the same verification bar `HEADLESS-3` set. `Clear`/`Present`/`GetViewportSize`/`ReadBackbuffer`/render-target binding are genuinely real (Phase S2); `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives` are still argument-validating placeholders, honestly not yet rasterizing (Phase S4's job). |
| SOFTWARE-4 | Factory dispatch for `SOFTWARE`; extend the existing `#ifdef CNA_BACKEND_HEADLESS` guards in `GraphicsDevice`'s constructor and `createOrAttachWindow()` to also cover `CNA_BACKEND_SOFTWARE` (design decision 4) | ✅ | Verified 2026-07-13: both guard sites now read `#if defined(CNA_BACKEND_HEADLESS) \|\| defined(CNA_BACKEND_SOFTWARE)`. `Software_Smoke` CTest confirms `SDL_WasInit(SDL_INIT_VIDEO) == 0` and `GetWindowInternal() == nullptr` end-to-end, and the full `CnaTests` suite runs with `DISPLAY`/`WAYLAND_DISPLAY` unset and `SDL_VIDEODRIVER` empty. Rebuilt `EASYGL` and `HEADLESS` after this change to confirm zero regression to either (both still build and pass their own test suites unchanged). |

---

## Phase S2 — Framebuffer and render targets

| # | Task | Status | Notes |
|---|---|---|---|
| SOFTWARE-10 | CPU color framebuffer (RGBA8, `std::vector<uint8_t>`) sized to the backbuffer (`PresentationParameters`); real `Clear(r,g,b,a)` fills every pixel | ✅ | Verified 2026-07-13 (`Software_Smoke` Check B): `Clear(Color(20,40,60,255))` followed by `GetBackBufferData()` returns exactly that color for every pixel in the queried region — not a fiction, a real per-pixel write-then-read round trip. |
| SOFTWARE-11 | CPU depth buffer (`std::vector<float>`), real `ClearDepth`/depth-inclusive `Clear*` variants | ✅ | `SoftwareFramebuffer::ClearDepthValue()` implemented and wired into `ClearDepth`/`ClearColorAndDepth`/`ClearDepthAndStencil`/`ClearColorDepthAndStencil`. Not yet exercised by a dedicated depth-readback test (no public API reads the depth buffer directly) — depth *correctness* will be verified properly once Phase S4's depth-test-driven rasterizer lands and can be proven via occlusion (a nearer triangle correctly occluding a farther one), a much stronger test than reading the raw buffer. |
| SOFTWARE-12 | `SoftwareRenderTargetBackend`: `CreateRenderTarget2D`/`SetRenderTarget2D` binds an alternate CPU color+depth buffer pair sized to that target, instead of the default backbuffer pair | ✅ | Verified 2026-07-13 (`Software_Smoke` Check C): binding an 8×8 `RenderTarget2D`, clearing it to a distinct color, then unbinding and reading the backbuffer confirms both framebuffers are genuinely independent — the render target's clear never touched the backbuffer's own pixels. Since 2026-08-01, `mipMap=true` also retains a generated RGBA8 chain after unbind; the shared GDI regression verifies its pixels, readback and minified sampling. |
| SOFTWARE-13 | `ReadBackbuffer()`/`GraphicsDevice::GetBackBufferData()`: real `memcpy` from the currently-bound CPU framebuffer — no faking, this is the backend's core value proposition | ✅ | Implemented as a real per-pixel copy (not literally `memcpy`, since out-of-bounds region requests are zero-filled rather than reading garbage) from `CurrentFramebuffer()`. Verified by the same `Software_Smoke` Checks B/C above. |
| SOFTWARE-14 | `Present()`: no-op by default, matching the headless/CI/server use cases | ✅ | Implemented as a genuine no-op (`{}`), matching design decision 3. |

---

## Phase S3 — Vertex/index storage and format inference

| # | Task | Status | Notes |
|---|---|---|---|
| SOFTWARE-20 | `SoftwareVertexBufferBackend`: stores raw bytes + stride (real storage, not discarded, mirroring `HeadlessVertexBufferBackend`'s own `ShadowData()` pattern) | ✅ | Verified 2026-07-13 (`Software_Smoke` Check D): real `VertexBuffer`s at strides 16 (`VertexPositionColor`)/20 (`VertexPositionTexture`)/24 (`VertexPositionColorTexture`) all round-trip real vertex data through `SetData()` without throwing. `Data()`/`Stride()` accessors exist for Phase S4's rasterizer to read from directly. |
| SOFTWARE-21 | `SoftwareIndexBufferBackend`: 16- and 32-bit, real storage | ✅ | Verified 2026-07-13 (`Software_Smoke` Check E): both a 16-bit and a 32-bit `IndexBuffer` round-trip real index data without throwing. A genuine bit-width mismatch (`SetData16` on a buffer declared 32-bit or vice versa) throws `std::runtime_error`, mirroring `HeadlessIndexBufferBackend`'s own precedent. |
| SOFTWARE-22 | Stride-based vertex format inference (design decision 2): 16→`VertexPositionColor`, 20→`VertexPositionTexture`, 24→`VertexPositionColorTexture`. 32-byte (`VertexPositionNormalTexture`) explicitly deferred (needs lighting, out of scope for v1) | ✅ | **Closed** (dispatch logic landed with Phase S4's rasterizer, `BuildGenericClipVertex`/`BuildPositionColorClipVertex`, verified by `Software_Rasterizer`/`Software_Effects`). The 32-byte deferral was itself later lifted by `SOFTWARE-82` (Phase S9, 2026-07-13), which added real stride-32 (`VertexPositionNormalTexture`, `EnvironmentMapEffect`) and stride-52 (`VertexPositionNormalTextureSkinned`, `SkinnedEffect`) dispatch once a real (if lighting-free) use for the normal existed. `GLTF-387` later completed the canonical table with 48-byte rigid PBR, 56-byte coloured skinning and 68-byte skinned PBR, all reached by the glTF native-boundary sweep. |

---

## Phase S4 — Rasterizer core

| # | Task | Status | Notes |
|---|---|---|---|
| SOFTWARE-30 | Per-vertex CPU transform: `World * View * Projection` (matching CNA's existing `Matrix` row/column convention) → clip space | ✅ | Verified 2026-07-13: uses CNA's own `Matrix::operator*` (row-major, row-vector — `combined = world*view*projection`) and the existing `Vector4::Transform(Vector3, const Matrix&)` helper, reusing established codebase math rather than reimplementing the transform. `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives` now genuinely rasterize (matching `VertexPositionColor`'s fixed layout — Position at offset 0, packed RGBA8 Color at offset 12, per that method's own documented "equivalent to BasicEffect with VertexColorEnabled=true" contract). |
| SOFTWARE-31 | Perspective divide + viewport transform → screen-space X/Y/Z | ✅ | `ndc = clip.xyz / clip.w`; `screenX=(ndcX*0.5+0.5)*viewportWidth`, `screenY=(1-(ndcY*0.5+0.5))*viewportHeight` (Y-flip: NDC is Y-up, the framebuffer is Y-down/top-left-origin). Depth used directly as `ndcZ` with no remapping, since CNA's projection matrices (like real XNA/FNA/D3D) already produce a 0..1 post-divide Z range, not OpenGL's -1..1. |
| SOFTWARE-32 | Triangle rasterization core: edge-function/barycentric fill, per-pixel depth test (`LessEqual`, matching `DepthStencilState::Default`) against the bound depth buffer, write-on-pass | ✅ | Standard edge-function rasterizer, bounding-box-limited, accepting either triangle winding (no backface culling in v1 — a real, intentional simplification, see Boundaries). Verified via the new `Software_Rasterizer` CTest (5/5): a solid-color triangle renders the exact color at its center pixel; **order-independent depth occlusion proven directly** (Checks C/D draw a near-red and far-blue triangle in both possible orders — red wins both times, proving the depth test is real, not an accidental last-write-wins artifact). |
| SOFTWARE-33 | Perspective-correct attribute interpolation (vertex color, UV) across the triangle | ✅ | Vertex colors are premultiplied by `1/clip.w` before rasterization, interpolated linearly in screen space, then divided by the interpolated `1/w` at the end (the standard technique). Verified via `Software_Rasterizer` Check B: a red/green/blue triangle's centroid pixel is the exact barycentric average of all three vertex colors. UV/texture interpolation follows the same mechanism once Phase S5 adds texture sampling. |
| SOFTWARE-34 | Basic near-plane handling: at minimum, cull triangles entirely behind the near plane; full polygon near-plane clipping (splitting a triangle into 1-2 new triangles) is a known v1 limitation, flagged here for a likely follow-up task once basic rendering is proven correct | ✅ | Implemented as a per-vertex `clip.W <= 1e-5f` check in `TransformPositionColorVertex()` — if any of a triangle's 3 vertices fails it, the whole triangle is culled rather than rasterized with garbage post-divide coordinates. Not yet exercised by a dedicated test with geometry deliberately crossing the near plane (all `Software_Rasterizer` test geometry stays safely in front of the camera) — a real, acknowledged gap, not silently claimed fully verified. |

---

## Phase S5 — Pixel shading

| # | Task | Status | Notes |
|---|---|---|---|
| SOFTWARE-40 | Vertex-color modulation (`vertexColorEnabled` path) | ✅ | `TransformGenericVertex()` reads real per-vertex color at stride 16/24 (position+color strides) and forces opaque white when `GpuDrawParams.vertexColorEnabled` is false — matches real XNA: `BasicEffect.VertexColorEnabled` itself defaults to `false`, so a plain `BasicEffect` ignores vertex colors unless a game opts in. Discovered directly while debugging `Software_Rasterizer`'s initial post-Phase-S6 regression (see `SOFTWARE-50`'s notes) — a real behavioral fact, not a rasterizer bug. |
| SOFTWARE-41 | Nearest-neighbor texture sampling (`textureEnabled`/`texture0`), reading the already-stored CPU-side pixel data every `ITextureBackend` implementation already keeps | ✅ | Verified 2026-07-13 (`Software_Effects` Check A): a full-screen quad textured with a 2x2 checker (`Texture2D::CreateFromPixels`) samples the correct texel near two opposite corners. `params.texture0` supports both `SoftwareTextureBackend` and `SoftwareRenderTargetBackend`; a completed mipmapped render target exposes its generated levels through the same sampling path. The same CPU SpriteBatch path also applies `ColorMatrixEffect` after texture/tint and before blending. Bilinear sampling remains a v2 stretch goal. |
| SOFTWARE-42 | Exact XNA blending for the shared shaded raster path: all `Blend` factors, independent colour/alpha `BlendFunction`, `BlendFactor`, and post-blend channel masks. | ✅ | `ApplyBlendState()` stores all six factor/function ordinals and `SetBlendFactor()` stores the dynamic constant; every shaded CPU fragment evaluates them per channel. The shared Release GDI 2D pixel regression covers Opaque, premultiplied AlphaBlend, NonPremultiplied, Additive, independent equations and BlendFactor. `Software_Effects` uses `NonPremultiplied` for its straight-alpha blend case. |
| SOFTWARE-43 | `diffuseColor`/alpha modulation from `GpuDrawParams` (BasicEffect's material color) | ✅ | Verified 2026-07-13 (`Software_Effects` Check B): `BasicEffect.DiffuseColor=(0.5,0,0)` tints a white texture to half-intensity red. Applied after texture sampling, matching the order real BasicEffect/XNA shaders apply material color. |

---

## Phase S6 — Effect integration

| # | Task | Status | Notes |
|---|---|---|---|
| SOFTWARE-50 | Wire `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx` to the Phase S4/S5 rasterizer using `GpuDrawParams`' `vertexColorEnabled`/`textureEnabled`/`texture0`/`diffuseColor`/`worldColMajor` fields; `lightingEnabled`/`fogEnabled`/`dualTexture`/`envMapping`/`skinned` explicitly out of scope for v1 | ✅ | Verified 2026-07-13 via `Software_Effects` (5/5) and re-verified `Software_Rasterizer` (5/5) still passing through the now-real `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx` overrides (previously only reachable via `IGraphicsBackend`'s own default fallback to `DrawColoredPrimitives`). Stride validated (16/20/24 only at closure; later extended by SOFTWARE-82 and `GLTF-387`). The original `textureEnabled && texture0==nullptr` refusal was deliberately replaced by `GLTF-387`'s white optional-base-map fallback: stock PBR and Skinned effects select a textured program without requiring a base map, and the native shader renderers bind white. Missing mandatory DualTexture/environment maps still refuse. **Real bug caught by this exact wiring**: `Software_Rasterizer`'s existing checks briefly went 0/5 the moment this override landed, because `BasicEffect.VertexColorEnabled` defaults to `false` in real XNA/FNA (verified directly in `BasicEffect.hpp`) — the fallback path `DrawColoredPrimitives` had been implicitly always treating color as enabled, masking this. Fixed by updating the test to explicitly set `VertexColorEnabled = true` (matching how a real game would), not by changing the (correct) new behavior. |
| SOFTWARE-51 | `SpriteBatch` reuses the same rasterizer for its quad draws (design decision 5) — a real, pixel-correct CPU `SpriteBatch` path | ✅ | `SoftwareSpriteBatchBackend::Draw()` builds its quad corners using the exact same formula as `EasyGLGraphicsBackend::EasyGLSpriteBatchBackend::Draw()` (destination/source rectangle, origin, rotation, `SpriteEffects` flip), then feeds two triangles directly into `RasterizeTriangleShaded()` in screen-pixel space (no `World*View*Projection` — SpriteBatch never uses one). `transformMatrix_` is applied as a 2D point transform on the already-placed corners. Verified via `Software_Effects` Check E: a solid-color texture drawn via `SpriteBatch::Draw()` lands at the exact requested screen position. Rotation/origin/`SpriteEffects` flip are implemented (reusing the proven formula) but not yet covered by a dedicated test — a real, acknowledged gap. |
| SOFTWARE-52 | Custom `ShaderEffect` (arbitrary GLSL/HLSL/WGSL source): accept without compiling (mirrors `HEADLESS-16`), document that only effects whose `FillGpuDrawParams()` output matches v1's fixed pixel-shading path will render correctly (design decision 8) | ✅ | Already implemented since Phase S1 (`SoftwareEffectBackend::CompileProgram()`): accepts any non-empty vertex/fragment source without compiling, mirroring `HEADLESS-16`. Not yet exercised by a dedicated Software test (Headless has one via `Headless_ResourceBackends`); the underlying behavior is identical and low-risk, so this is a documentation/test-coverage gap, not an implementation gap. |

---

## Phase S7 — Tests

| # | Task | Status | Notes |
|---|---|---|---|
| SOFTWARE-60 | Deterministic pixel tests via real `GetBackBufferData()` readback, no GPU/display needed: clear-color test, single flat-colored triangle test, single textured quad test | ✅ | Done, and then some, across `Software_Smoke`/`Software_Rasterizer`/`Software_Effects` (16 checks total): clear-color (`Software_Smoke` B), flat-colored triangle + depth occlusion (`Software_Rasterizer`), textured quad + blending + `SpriteBatch` (`Software_Effects`). Did not need a `PixelTestGame`-style shared harness — each test file is its own small `Game` subclass reading pixels directly via `GetBackBufferData()`, mirroring the exact pattern established for `HEADLESS`'s own test suite. |
| SOFTWARE-61 | (Stretch goal) Cross-backend diagnostic test: render an identical simple scene on `SOFTWARE` and a real GPU backend, compare within a tolerance — directly serves the owner's stated "diagnostika rozdílů mezi backendy" use case, but depends on a real GPU backend being available in the test environment; may not be runnable in every CI | ✅ | **Closed 2026-07-13 via `SOFTWARE-84`** (Phase S9) — see that row for the implementation (a shared scene source built per-backend + a standalone comparator, run manually since `CNA_GRAPHICS_BACKEND` is a compile-time choice). |
| SOFTWARE-62 | CTest registration, mirroring `HEADLESS`'s own `cna_headless_test`-style CMake macro | ✅ | `cna_software_test()` CMake macro (mirrors `cna_headless_test()` exactly), 3 tests registered: `Software_Smoke`, `Software_Rasterizer`, `Software_Effects`, all labeled `"Software"`, no `SDL_VIDEODRIVER`/`DISPLAY` set. |

---

## Phase S8 — Docs

| # | Task | Status | Notes |
|---|---|---|---|
| SOFTWARE-70 | `docs/software-backend.md`: what it's for/not for, current capability boundary, how to write a test, known limitations — mirrors `docs/headless-backend.md`/`docs/webgpu-backend.md`'s structure. Optionally document an opt-in "blit the CPU framebuffer to a real window" mode here if it ends up being worth adding (design decision 3) | ✅ | Written 2026-07-13, mirroring `docs/headless-backend.md`'s structure exactly (Status / What it's for-isn't / Writing a test / Known limitations). The opt-in "blit to a real window" mode is documented as a reasonable future addition, not implemented (not needed for this backend's actual value proposition). |
| SOFTWARE-71 | `docs/graphics-backend-feature-matrix.md`: unlike `HEADLESS` (which was deliberately kept OUT of that matrix, since it never renders a real pixel), `SOFTWARE` actually could become a genuine pixel-parity column once mature enough — flag as a real future opportunity rather than deciding now, since v1's feature set is too narrow for a meaningful comparison yet | ✅ | Added a note right after the existing `HEADLESS` note explaining exactly this distinction (Software *does* render real pixels, unlike Headless, so it's a real future-column candidate once its feature set broadens) rather than adding a premature/misleading column now. |

---

## Phase S9 — v2 improvement candidates (proposed 2026-07-13, none authorized yet)

Every row below is a real, scoped-down v1 limitation from Phases S1-S8, listed here as concrete
follow-up candidates rather than left as a vague "could be better later" note. **None of these are
authorized for implementation** — each needs an explicit go-ahead before starting, one at a time or
in an approved batch. Ordered roughly by value-for-effort, cheapest/highest-value first.

| # | Task | Status | Notes |
|---|---|---|---|
| SOFTWARE-80 | Bilinear texture sampling (currently nearest-neighbor only, `SOFTWARE-41`) | ✅ | **Closed 2026-07-13.** Implemented as `SampleBilinear()`: standard half-texel-offset bilinear with clamp-to-edge at the boundaries (matches this backend's own existing "texture address modes not honored, UVs are simply clamped" simplification rather than adding real Wrap/Mirror support). Always on (not gated by `SamplerState.Filter` — a real, documented v1 simplification; the actual default `SamplerState.LinearWrap` already implies Linear filtering everywhere in real XNA, so this is arguably *more* faithful than the nearest-neighbor it replaces, not less). **Caught and fixed a real bug during implementation**: the first version clamped `x0`/`y0` and then computed `x1=clamp(x0+1,...)`/`y1=clamp(y0+1,...)` *from the already-clamped* `x0`/`y0`, shifting the second sample to the wrong texel right at a texture edge — caught immediately by `Software_Effects`' own corner-sampling check failing with a visibly blended, non-solid color at pixel (0,0)-(4,4) instead of pure `Red`. Fixed by computing both raw (pre-clamp) indices first, then clamping each independently. Re-verified all 3 Software CTests (16/16) and the full `CnaTests` suite (4371/4373, same baseline) after the fix. |
| SOFTWARE-81 | Backface culling (`RasterizerState.CullMode`) — currently both winding orders always accepted (`SOFTWARE-32`'s own noted simplification) | ✅ | **Closed 2026-07-13.** `ApplyRasterizerState()` now stores the raw `CullMode` ordinal (`cullMode_`); a new `ShouldCullTriangle(area, cullMode)` helper is checked in both `RasterizeTriangle`/`RasterizeTriangleShaded` right after the existing degenerate-triangle check. In this backend's screen-space convention (Y grows downward), a negative signed `area` is clockwise-as-displayed and a positive `area` is counter-clockwise — verified **empirically**, not just derived, by the new `Software_Culling` CTest (5/5: default `CullCounterClockwise` keeps CW visible and culls CCW; `CullClockwise` reverses that; `CullNone` disables culling entirely). `SoftwareSpriteBatchBackend`'s quads also read the owner's cull mode via a new `GetCullMode()` accessor — matching real FNA, whose own `SpriteBatch` defaults its `RasterizerState` to `CullCounterClockwise` (not `CullNone`); its quad corner order was checked by hand to already be clockwise-as-displayed, so it survives the real default unaffected. **Existing-test mitigation**: `software_rasterizer_test.cpp`/`software_effects_test.cpp`'s triangles were authored purely for pixel-correctness and turned out to be counter-clockwise-as-displayed (would newly be culled under the real default) — both now explicitly call `dev.setRasterizerStateProperty(RasterizerState::CullNone)` at the top of `Draw()` to keep testing what they were designed to test. Verified: all 4 Software CTests (21/21 checks) plus the full `CnaTests` suite (4371/4373, same baseline) after the change. |
| SOFTWARE-82 | `DualTextureEffect`/`EnvironmentMapEffect`/`SkinnedEffect`-specific `GpuDrawParams` fields (second texture, env map, bone transforms) — currently only the `BasicEffect` subset (`vertexColorEnabled`/`textureEnabled`/`diffuseColor`) is read, matching design decision 6 | ✅ | **Closed 2026-07-13.** Deliberately scoped to stay consistent with design decision 6 (no lighting engine in v1): all 3 effects' "lit" base color is `vertexColor*diffuseColor*texture0`, the same simplification the plain `BasicEffect` path already uses — no per-light `NdotL` diffuse sum, ambient, or emissive term for any of them (real `EasyGL`/`Vulkan`/`Bgfx` backends DO compute that sum; this backend still doesn't, for any effect). What's genuinely new: (1) **`DualTextureEffect`**: real second-texture sampling + FNA's `color.rgb*=2; color*=overlay*diffuse` formula, both textures reusing the same UV (this backend has no 2-UV vertex format — matches this codebase's own Vulkan `dual_texture3d` shader precedent, not a new simplification). (2) **`EnvironmentMapEffect`**: a new `SoftwareTextureCubeBackend` gives `CreateTextureCube` real 6-face RGBA8 storage for the first time (previously always `nullptr`); a new 32-byte `VertexPositionNormalTexture` stride carries a per-vertex normal, clipped/interpolated as a `ClipVertex`/`RasterVertex` attribute exactly like color/UV; world-space position/normal are computed via a new `ApplyAffineColumnMajor()` helper applied to `GpuDrawParams::worldColMajor` (using `World` directly rather than the mathematically-correct `WorldInverseTranspose` — exact for uniform-scale/no-shear `World`, a real simplification for non-uniform scale); the reflection vector (`reflect(-eyeVector, worldNormal)`) is sampled against the cube map via a new `SampleCubeMap()` (standard largest-axis face-select + per-face UV projection, nearest-neighbor only, no cross-face bilinear at seams); Fresnel weighting and the specular-tint term both match FNA's own `PSEnvMapSpecular` formula. (3) **`SkinnedEffect`**: a new 52-byte `VertexPositionNormalTextureSkinned` stride (the project's own existing canonical skinned-vertex layout, already used by the Avatar real-rendering path) is read; up to `WeightsPerVertex` bone matrices are blended (column-major, matching `GpuDrawParams::boneTransforms`' own layout) and applied to the vertex position *before* World\*View\*Projection, mirroring FNA's `Skin(vin, boneCount)` step — this needed no new pixel-shading logic at all, since FNA's own `SkinnedEffect.fx` pixel shaders use the exact same `texture*diffuse` formula already implemented for `BasicEffect`. New `Software_DualEnvmapSkinned` CTest (4/4): DualTextureEffect's blend formula against a hand-computed expected color; EnvironmentMapEffect's reflection sampling the correct cube face for a camera-facing surface (derived by hand: eye at world origin, quad facing the camera means `eyeVector==normal`, so `reflect(-eyeVector,normal)` is exactly the view axis) and `EnvironmentMapAmount=0` showing the plain base texture; SkinnedEffect rendering at the bone-translated screen position, not the bind pose. All 4 checks passed on the first run (hand-derivation confirmed empirically, not just assumed). Verified: all 6 Software CTests (29/29 checks) and the full `CnaTests` suite (4371/4373, same baseline). |
| SOFTWARE-83 | Real near-plane polygon clipping (split a triangle crossing the near plane into 1-2 new triangles) instead of culling the whole triangle (`SOFTWARE-34`'s own acknowledged v1 gap) | ✅ | **Closed 2026-07-13.** A new `ClipVertex` (clip-space, un-premultiplied attributes) replaces the old "transform straight to screen-space RasterVertex, fail the whole triangle if any vertex has `clip.W<=~0`" flow. `BuildPositionColorClipVertex`/`BuildGenericClipVertex` build 3 `ClipVertex`; a new Sutherland-Hodgman `ClipTriangleNearPlane()` clips against the single `w>kNearEpsilon` half-space, returning 0 (fully behind, discarded — same end result as the old whole-triangle cull), 3 (no clip needed, or one corner clipped off), or 4 (two corners clipped off, a quad) vertices, preserving winding so `SOFTWARE-81`'s culling stays correct on the result. `LerpClipVertex` interpolates position AND color/UV together (clip space is still linear pre-divide, so a plain lerp is exact — unlike screen space). `ClipVertexToRasterVertex` does the perspective divide + viewport transform once, after clipping; a 4-vertex quad is fan-triangulated into 2 draw calls. All 4 draw paths (`DrawColoredPrimitives`/`DrawIndexedColoredPrimitives`/`DrawPrimitivesEx`/`DrawIndexedPrimitivesEx`) were updated identically. **A genuinely near-plane-crossing clip vertex lands at an enormous (but finite) screen position after the divide** — dividing by a `w` forced to sit at `kNearEpsilon` (≈0) is correct, unavoidable perspective math for a point that close to the camera's eye, not a bug; the new `Software_Clipping` CTest (4/4) was deliberately designed around this — it checks that each *surviving, non-clipped* vertex's own screen projection stays red (proving clipping preserved real geometry) and that total red-pixel count is bounded (>0 and <full framebuffer), rather than asserting exact colors at the wild/huge clip-vertex coordinates themselves. Verified: all 5 Software CTests (25/25 checks) and the full `CnaTests` suite (4371/4373, same baseline). |
| SOFTWARE-84 | `SOFTWARE-61`'s cross-backend diagnostic test: render an identical scene on `SOFTWARE` and a real GPU backend, compare within a tolerance | ✅ | **Closed 2026-07-13.** `CNA_GRAPHICS_BACKEND` is a compile-time choice, so this isn't a single automated `ctest` — it's a shared, backend-agnostic scene source (`examples/cross_backend_diagnostic_scene.cpp`, a fully unlit vertex-color triangle, no lighting/texture involved) built once per backend needing it (`cna_diag_software` in the `SOFTWARE` section, `cna_diag_easygl` in the `EASYGL` section of `CMakeLists.txt`), dumping a raw 64x64 RGBA8 file, plus a standalone comparator (`cna_diag_compare`, no CNA dependency) that diffs two dumps within a tolerance. Documented with the exact 3-command manual invocation in `docs/software-backend.md`'s new "Cross-backend diagnostic" section. **Actually run end-to-end this session** (this repo's real desktop `:0` session, per the established WebGPU-verification precedent, provided the display `EASYGL` needed): `SOFTWARE` vs. `EASYGL` gave a max per-channel diff of 1 (mean 0.139) — effectively identical. The comparator was also checked against a deliberately corrupted dump to confirm it genuinely fails on a real mismatch (max diff 255), not just always passing. Directly serves the owner's original "diagnostika rozdílů mezi backendy" use case. |
| SOFTWARE-85 | Optional opt-in "blit the CPU framebuffer to a real window" mode, so a `SOFTWARE`-rendered frame can be visually inspected instead of only pixel-asserted (`Present()` is currently a pure no-op) | ⬜ | Would need `#ifdef CNA_BACKEND_HEADLESS \|\| defined(CNA_BACKEND_SOFTWARE)`'s window-skip guard in `GraphicsDevice.cpp` to gain a third, opt-in state for `SOFTWARE` specifically (window created but backend still self-renders in software, then blits) — touches shared code, more invasive than the others on this list. Lowest priority: not needed for this backend's actual value proposition (deterministic, GPU-free pixel tests). |
| SOFTWARE-86 | Performance work (SIMD/multithreading/tiling) | ⬜ | Explicitly NOT a goal per design decision 1 — only revisit if a specific real test becomes impractically slow, not preemptively. |

---

## Boundaries (stop and ask, don't improvise)

- **GPU-init-failure fallback** (one of the owner's stated use cases) is a `Game`/
  `GraphicsDeviceManager`-level runtime policy decision (auto-switching the active backend when GPU
  init fails) — a separate, larger, cross-cutting feature, not part of this backend's own scope.
  Flag it as a possible follow-up plan if wanted later; do not fold it into this one.
- Full per-light `BasicEffect` lighting/fog (and the equivalent lighting inputs on
  `EnvironmentMapEffect`/`SkinnedEffect`), MRT, automatic mip generation for ordinary
  textures, anisotropic filtering, 3D
  textures, and render-target cube maps remain explicitly out of scope for v1, matching the
  owner's own minimal first-version list verbatim (clear; render target; triangle list; basic
  blending; depth buffer; simple textures; vertex colors; `BasicEffect` subset).
  **`DualTextureEffect`/`EnvironmentMapEffect`/`SkinnedEffect` and plain (non-render-target) cube
  textures were later lifted out of this out-of-scope list by `SOFTWARE-82`** (Phase S9,
  2026-07-13, minus the per-light lighting caveat above) — see that row and
  `docs/software-backend.md`'s Known Limitations for exactly what's supported.
  Render-target 4x MSAA was likewise implemented later by the CPU rasteriser; `GLTF-395` locks its
  active-pass level-zero resolve/readback contract in addition to the existing unbound tests.
  `Model.Draw()` with real skinning specifically hasn't been separately verified end-to-end
  (only the lower-level `SkinnedEffect`/`DrawPrimitivesEx` path was tested).
- Performance/SIMD/multithreading work is explicitly not a goal (design decision 1) — do not
  spend effort here unless a specific test becomes impractically slow.
- Do not let `SOFTWARE`-specific code leak into the shared `IGraphicsBackend`/`GpuDrawParams`
  interface layer beyond what a genuine common-interface need justifies — same backend-locality
  rule the other backends (`CLAUDE.md`, `plan_webgpu.md`/`plan_headless.md`'s own boundaries)
  already follow.
- If Phase S4's rasterizer core turns out to need real polygon near-plane clipping sooner than
  expected (visible artifacts in even simple test scenes), treat that as a legitimate scope
  addition to flag and discuss, not something to silently skip or silently half-implement.
- **`TriangleList` only in v1** (already called out in the top status banner, restated here since
  this is the section meant to be the durable reference): `TriangleStrip`/`LineList`/`LineStrip`/
  `PointListEXT` all throw a clear "only TriangleList is supported in v1" error instead of silently
  misrendering. Not tracked as its own `SOFTWARE-NN` row because it was never implemented in the
  first place (Phase S4's rasterizer core was scoped to triangles from the start) — see
  `docs/software-backend.md`'s Known Limitations for the exact error text.
- **Bilinear texture sampling (`SOFTWARE-80`) is always on, regardless of `SamplerState.Filter`**,
  and there is no real texture address-mode support — `Wrap`/`Mirror` are not implemented, UVs are
  simply clamped to `[0,1]` at the texture bounds regardless of what `SamplerState.AddressU/V`
  requests. This was a deliberate v1 simplification recorded in `SOFTWARE-80`'s own row (arguably
  *more* faithful than the nearest-neighbor sampling it replaced, since real XNA's default
  `SamplerState.LinearWrap` already implies linear filtering almost everywhere) but wasn't restated
  here in Boundaries until now — see `docs/software-backend.md`'s Known Limitations for the same
  point in the user-facing doc.
