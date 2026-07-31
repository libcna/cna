# sokol_gfx Graphics Backend — Implementation Plan

> **Status legend** (matches this repo's own convention): ✅ implemented *and verified against its
> stated acceptance criteria*; 🟨 code or documentation exists but has not met those criteria;
> ⬜ not implemented.
>
> **Status (2026-07-31): the 2D baseline is implemented and pixel-verified.**
> `CNA_GRAPHICS_BACKEND=SOKOL` configures, fetches sokol at a pinned commit, builds
> `cna_backend_graphics_sokol`, and produces a real SDL window with a real OpenGL 4.1 core
> context driving `sokol_gfx`. `Sokol_Smoke` (13/13) proves the device/context/pass lifecycle, a
> 60-frame `Clear()`/`Present()` loop, vertex/index buffer round-trips, and that
> `GetBackBufferData()` returns the exact colour `Clear()` was given. `Sokol_2D` (15/15) proves
> `Texture2D` upload and `SpriteBatch` rendering with **every draw checked against real read-back
> pixels** — quadrant placement, both flips, colour tint (a real per-channel multiply, not an
> ignored tint), `NonPremultiplied` vs `Opaque` blending producing genuinely different results,
> and a 90-degree rotation. Verified on this dev machine under Xvfb with Mesa's llvmpipe software
> GL — a real GL 4.1 driver, but not discrete-GPU hardware (see `SOKOL-30`).
>
> **Update (2026-07-31, later the same day): Phase 4's first task `SOKOL-20` has landed** -- real
> vertex-coloured 3D geometry through `VertexBuffer`/`IndexBuffer`/`BasicEffect`/`DrawPrimitives`,
> with depth testing and face culling both driven by the real `DepthStencilState`/`RasterizerState`.
> `Sokol_3D` (10/10) verifies every result against read-back pixels, including a depth-occlusion
> proof and a culling proof. Two real bugs were caught by that test and fixed: the cull-mode
> mapping was inverted against sokol's `SG_FACEWINDING_CW` default (every triangle rendered as
> background), and a UINT16-index pipeline cannot serve a non-indexed draw, so the index type is
> now part of the pipeline key. `GraphicsCapability::ThreeD` now reports true.
>
> **Update (2026-07-31, later still): `SOKOL-21` (textured + lit `BasicEffect` draws) has
> landed.** `textured3d.glsl` (texture + vertex colour + alpha test + fog) and `lit3d.glsl`
> (ambient + up to 3 real per-pixel Blinn-Phong directional lights + specular + emissive + alpha
> test + fog, always sampling a texture -- a real one, or a 1x1 white fallback when
> `TextureEnabled` is false, mirroring EasyGL's own default-white-texture convention) join
> `colored3d.glsl`. `Sokol_Lit3D` (10/10, all real pixel read-backs) proves texture placement,
> `DiffuseColor`/vertex-colour multiplies, real alpha blending, ambient-only lighting, a
> head-on-vs-facing-away directional light (proving the `N.L` clamp, not negative light),
> `EmissiveColor`, and a fully-fogged draw. Two real bugs were caught along the way: the test's
> own first draft used `BlendState::AlphaBlend` (premultiplied factors) against straight-alpha
> texture data, and the backend's default white fallback texture was destroyed after
> `sg_shutdown()` had already invalidated the sokol context (member destruction order), aborting
> on `sg_destroy_view`'s own internal assert.
>
> **Still not implemented: skinned/dual-texture/environment-mapped/PBR shading, custom effects,
> render targets, cube/volume textures and occlusion queries** -- all fail loudly rather than
> silently no-opping. Do not describe this backend as having EasyGL/Vulkan-level parity. See
> `docs/sokol-backend.md` for the capability boundary and the complete list of known gaps.

---

## Why this backend

CNA already has fourteen graphics backends, several of which are themselves abstraction layers
(`BGFX`, `WEBGPU`, `SDL_GPU`). `sokol_gfx` earns a fifteenth slot for a different reason than any
of them: it is the smallest credible modern GPU abstraction in existence — two single-file headers,
no build system, no runtime, no code generation required at build time — while still dispatching
onto GL 4.1 / GLES3 / D3D11 / Metal / WebGPU behind one API.

That makes it valuable to CNA in two specific ways:

1. **A near-zero-dependency portable GPU path.** `BGFX` needs a full CMake sub-build and a
   `shaderc` toolchain; `WEBGPU` needs a pinned `wgpu-native` binary package; `SDL_GPU` needs
   `libshaderc` linked into the backend for custom effects. `SOKOL` needs a header on the include
   path and `-lGL`.
2. **An independent implementation of the same contract.** Every bug this backend's own pixel tests
   catch is a bug in `IGraphicsBackend`'s contract or in CNA's shared 2D layer, not in one vendor's
   driver — the same value the `SOFTWARE` backend provides, at GPU speed.

---

## Design decisions

1. **CNA keeps the window and the game loop; sokol_app is not used.** `sokol_app.h` wants to own
   window creation, the event loop and the frame callback. CNA already owns all three through
   `Game`/`GraphicsDeviceManager` and SDL3, and `Microsoft::Xna::Framework::Input` reads SDL3
   events directly. `SokolGraphicsBackend` therefore creates only the GPU *context*
   (`SDL_GL_CreateContext` on the window CNA already made) and drives `sokol_gfx` inside it. This
   is the same relationship `EASYGL` has with SDL3.
   *Consequence:* `GraphicsDevice::getBackendWindowFlags()` must add `SDL_WINDOW_OPENGL` for this
   backend — SDL refuses `SDL_GL_CreateContext` on a window created without it. This was found
   empirically, by the smoke test failing with "The specified window isn't an OpenGL window".

2. **The native API is a second compile-time axis, `CNA_SOKOL_API`.** `sokol_gfx` is itself a
   multi-API abstraction, so `CNA_GRAPHICS_BACKEND=SOKOL` alone does not determine what actually
   runs. `CNA_SOKOL_API` (`GLCORE` | `GLES3` | `D3D11` | `METAL` | `WGPU`) resolves to the single
   `SOKOL_*` define `sokol_gfx.h` dispatches on. **`GLCORE` is the default and the only value
   verified**; the others configure and warn, and their context-creation path is not written
   (`SOKOL-31`). The C++ never branches on the API except where it genuinely must (context
   creation, GL read-back), so adding one is a bounded change.

3. **sokol is fetched at configure time at a pinned commit, not vendored.** sokol publishes no
   release tags (its only tags mark historical API breaks), so `cmake/ThirdPartySokol.cmake` pins
   a plain commit SHA. Offline builds use CMake's own `-DFETCHCONTENT_SOURCE_DIR_SOKOL=<path>`
   override rather than a CNA-specific variable. Bump the pin deliberately: sokol changes its C
   API without deprecation periods, and `sokol_shaders.hpp` must be regenerated with a matching
   `sokol-shdc` whenever it moves.

4. **Shaders are compiled offline by `sokol-shdc` and the generated header is checked in.** Same
   convention as the Bgfx backend's `bgfx_shaders.hpp`. An ordinary CNA build needs no `sokol-shdc`
   binary and no network. `src/CNA/Internal/Backends/Sokol/shaders/compile_shaders.py` regenerates
   `sokol_shaders.hpp` for all five target shader languages at once (`--ifdef`-guarded, so only the
   selected API's code compiles), and `--check` fails if the checked-in header is stale.

5. **The sprite batcher streams into one appended buffer against a static quad index buffer.**
   `sokol_gfx` allows at most one `sg_update_buffer()`/`sg_update_image()` per resource per frame,
   which a `SpriteBatch` flushing per texture change would violate immediately. `sg_append_buffer()`
   is the API designed for exactly this: each flush appends its own vertex run and draws from the
   returned byte offset, so the number of flushes per frame is unbounded. The index pattern never
   changes, so it is an immutable buffer built once.
   *Consequence:* the streaming buffer is sized at construction, giving a hard per-frame cap of
   **16384 sprite quads** (65536 vertices — exactly the largest run a uint16 index buffer can
   address). Exceeding it throws a named error rather than silently dropping sprites.

6. **User `VertexBuffer`/`IndexBuffer` uploads recreate an immutable buffer instead of updating
   one.** Same one-update-per-frame rule as above, and immutable creation with initial data is
   unrestricted. This trades an allocation per upload for correctness; `SOKOL-24` covers making it
   cheaper once the 3D path exists to measure it.

7. **Back-buffer read-back goes through `glReadPixels`, not sokol.** `sokol_gfx` has no read-back
   API at all. On the GL APIs sokol renders into the window's default framebuffer, which
   `glReadPixels` reads directly. This is the one genuinely API-specific piece of the backend and
   it is guarded as such: any other `CNA_SOKOL_API` refuses `ReadBackbuffer` rather than pretending.

8. **Passes are begun lazily and ended eagerly.** A `sokol_gfx` pass fixes its load actions when it
   begins, so `Clear()` records a *pending* action and closes any open pass; the next draw (or
   `Present()`, or `ReadBackbuffer()`) begins a pass that applies it. Every restart after the first
   in a frame loads rather than discards, so a mid-frame clear cannot wipe earlier draws.
   *Consequence found by the smoke test:* `ReadBackbuffer` must begin the pass too. Without that, a
   `Clear()`-then-read-back sequence with nothing drawn in between read the previous frame's
   post-swap garbage — the first version of this backend returned `(0,0,0,0)` for a clear to
   `(32,64,128)`.

---

## Phases and tasks

### Phase 1 — Build integration

| ID | Task | Status | Notes |
|---|---|---|---|
| SOKOL-1 | Add `SOKOL` to `CNA_GRAPHICS_BACKEND`, `CNA_BACKEND_SOKOL` option, backend dir/target/define | ✅ | `cmake/BackendSelection.cmake` |
| SOKOL-2 | `cmake/ThirdPartySokol.cmake`: pinned FetchContent, `cna_sokol_headers` interface target, `OpenGL::GL` for the GL APIs | ✅ | Pin `27b49604b19be8cee0dcc6b2bbfe803dd9517585` |
| SOKOL-3 | `SokolImpl.cpp`: the one TU carrying `SOKOL_IMPL` + `SOKOL_SHDC_IMPL` | ✅ | Keeps sokol's ~20k-line C body out of the backend's incremental rebuilds |
| SOKOL-4 | `SDL_WINDOW_OPENGL` in `GraphicsDevice::getBackendWindowFlags()` | ✅ | Found empirically — see design decision 1 |
| SOKOL-5 | `CNA_SOKOL_API` option, `SOKOL_*` define resolution, unverified-API warning | ✅ | See design decision 2 |
| SOKOL-6 | `GraphicsBackendType::Sokol` + `"SOKOL"` name | ✅ | `include/CNA/GraphicsBackendType.hpp` |
| SOKOL-7 | `ExactlyOneGraphicsBackendIsSelected` covers `CNA_BACKEND_SOKOL`; backend-identity test | ✅ | `tests/.../GraphicsBackendCompileDefinitionTests.cpp` |

### Phase 2 — Device, context and presentation

| ID | Task | Status | Notes |
|---|---|---|---|
| SOKOL-8 | GL 4.1 core context via SDL3, depth24+stencil8, MSAA negotiation, swap interval | ✅ | Reports the driver's *granted* sample count, not the request |
| SOKOL-9 | `sg_setup()` with the swapchain environment defaults; transactional construction | ✅ | A partially built backend never reaches the window registry |
| SOKOL-10 | Lazy pass management, the whole `Clear*` family through pass load actions | ✅ | See design decision 8 |
| SOKOL-11 | `Present()`, virtual resolution, presentation mode, window↔logical transforms | ✅ | Mirrors EasyGL's `FixedHeightDynamicWidth` geometry |
| SOKOL-12 | `ReadBackbuffer()` via `glReadPixels`, with the row flip and logical→physical scaling | ✅ | GL APIs only — see design decision 7 |

### Phase 3 — 2D resources and rendering

| ID | Task | Status | Notes |
|---|---|---|---|
| SOKOL-13 | `SokolTextureBackend`: `sg_image` + `sg_view`, per-level CPU shadow, `GetData()` | ✅ | Full-image re-upload; sokol has no sub-image update |
| SOKOL-14 | `sprite.glsl` + `compile_shaders.py` + checked-in `sokol_shaders.hpp` | ✅ | Five shader languages, `--ifdef`-guarded |
| SOKOL-15 | `SokolSpriteBatchBackend` + streamed draw path, blend/sampler pipeline caches | ✅ | See design decision 5 |
| SOKOL-16 | `Sokol_Smoke` CTest — lifecycle, buffers, clear read-back, loud 3D failure | ✅ | 13/13 |
| SOKOL-17 | `Sokol_2D` CTest — `Texture2D` + `SpriteBatch`, all 15 checks pixel-verified | ✅ | 15/15 |
| SOKOL-18 | `SokolVertexBufferBackend`/`SokolIndexBufferBackend` (16- and 32-bit) | ✅ | See design decision 6 |
| SOKOL-19 | `docs/sokol-backend.md` capability boundary | ✅ | |

### Phase 4 — 3D pipeline (SOKOL-20/21 landed)

| ID | Task | Status | Notes |
|---|---|---|---|
| SOKOL-20 | `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives` + a colored-3D shader | ✅ | `Sokol_3D` 10/10, incl. depth-occlusion and culling proofs |
| SOKOL-21 | `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx`: `BasicEffect` variants (textured, lit, fog) | ✅ | `Sokol_Lit3D` 10/10; dual-texture/env-map/skinned/PBR still throw |
| SOKOL-22 | Honour every `SetVertexDeclaration()` element, not just Position/Color usage index 0 | 🟨 | The colored-3D pipeline is keyed on the real declaration; other usages still ignored |
| SOKOL-23 | Full `ApplyRasterizerState` (fill mode, depth bias) and stencil in the pipeline key | 🟨 | Stencil and depth bias done. Stencil: `ApplyDepthStencilState` wires the real stencil state (front/back ops, compare, read/write masks, reference) into `Pipeline3DKey`/`Get3DPipeline` -- `sg_stencil_state.ref` is baked into the pipeline object itself (sokol_gfx has no dynamic stencil-ref call, unlike most APIs), so it is part of the key too. Depth bias: `RasterizerState.DepthBias`/`SlopeScaleDepthBias` map straight onto `sg_depth_state.bias`/`bias_slope_scale` -- the same values EasyGL/Vulkan pass to `glPolygonOffset(slopeScaleDepthBias, depthBias)` -- also baked into the pipeline, also part of the key; verified with Task 767's own EasyGL-authored (but backend-agnostic) coplanar-redraw test, reused as `Sokol_RasterizerState_DepthBias`. Neither reaches the sprite pipeline (XNA's SpriteBatch never uses stencil or depth bias). Cull mode landed with `SOKOL-20`. Fill mode (`WireFrame`) remains a genuine, permanent gap: sokol_gfx exposes no polygon fill mode API at all, unlike EasyGL's CPU-side triangle-to-`GL_LINES` re-expansion at draw time, which this backend does not implement. |
| SOKOL-24 | Cheaper `VertexBuffer`/`IndexBuffer` re-upload than recreate-per-`SetData` | ⬜ | Measure once a 3D path exists to measure with |

### Phase 5 — Render targets and remaining resources (SOKOL-25/26/27/29 landed)

| ID | Task | Status | Notes |
|---|---|---|---|
| SOKOL-25 | `RenderTarget2D` via `sg_view` colour/depth-stencil attachments | ✅ | `SetRenderTarget2D`/`SetRenderTargets` bind a real colour(+depth-stencil) attachment pass; `GetData()` on a RenderTarget2D is not implemented (throws `NotSupportedException`, matching the base `IGraphicsBackend::GetData` default) — sampling a render target as a texture (SpriteBatch and 3D alike) and `GetBackBufferData()` while none is bound both work. 8 shared cross-backend oracles registered and passing: `Sokol_RenderTarget_ViewportScissorReset`, `Sokol_RenderTarget2D_Depth`, `Sokol_RenderTarget_DepthStencilUsage`, `Sokol_RenderTarget_PassBoundary`, `Sokol_RenderTarget_ProducerConsumer`, `Sokol_RenderTarget_BackbufferConsumer`, `Sokol_RenderTarget_FirstUse`, `Sokol_RenderTarget_SamplingOrientation`. `multiSampleCount` is real as of `SOKOL-26` (see its own row); `mipMap=true` throws `NotYetImplemented`. |
| SOKOL-26 | `RenderTargetCube`, MRT, render-target MSAA resolve | 🟨 | `RenderTargetCube` done: `SokolRenderTargetCubeBackend` is a real `sg_image` (`SG_IMAGETYPE_CUBE`, `usage.color_attachment`) with one colour-attachment view per face (`sg_view_desc.color_attachment.slice`) plus a single shared 2D depth-stencil attachment (matching FNA3D's one-buffer-per-cube convention, not per-face) — bind, draw, unbind and cross-face content isolation all pixel-verified via the same shared oracles SOKOL-25 reused (`Sokol_RenderTarget_PassBoundary`'s C1/C2 legs, `Sokol_RenderTarget_DepthStencilUsage`'s U1-U4, plus the cube legs in `Sokol_RenderTarget_FirstUse`/`BackbufferConsumer`). `RenderTargetCube::GetData` is not implemented (same `NotSupportedException` boundary as `RenderTarget2D`); mipMap and multiSampleCount follow `CreateRenderTarget2D`'s own conventions (throw / silently clamp to 1). `RenderTarget2D` MSAA + resolve done: `SokolRenderTargetBackend` clamps the requested `multiSampleCount` to the driver's real `GL_MAX_SAMPLES` (mirroring EasyGL's identical clamp) and, when it stays above 1, allocates a multisample-only colour image plus a separate single-sample resolve image and (if a depth format was requested) a matching-sample-count depth-stencil image, following sokol_gfx.h's own documented offscreen-MSAA workflow -- naming a resolve-attachment view in the pass triggers sokol_gfx's automatic resolve at `sg_end_pass()`. The sprite and 3D pipeline caches both gained a `sampleCount` key field (`CurrentPassSampleCountEXT()`) since sokol_gfx bakes `sample_count` into the pipeline and rejects a mismatch against the pass it draws into -- a real, independently-discovered gap this task fixed (every pipeline previously used the *window's* sample count unconditionally, which would have mismatched the first time a target's own MSAA count differed from it). Verified with Task 337's own differential real-anti-aliasing proof (renders a diagonal-edged triangle at `MultiSampleCount=0` and `8`, asserts the 8x row has genuinely blended pixels the 0x row does not) -- backend-agnostic despite its "EasyGL" filename, reused as `Sokol_RenderTarget2D_Msaa`. `RenderTargetCube` MSAA was attempted the same way and reverted: sokol_gfx's own validation layer hard-rejects a `SG_IMAGETYPE_CUBE` image with `sample_count > 1` (`VALIDATE_IMAGEDESC_ATTACHMENT_MSAA_CUBE_IMAGE`), confirmed empirically (a real `[sg][panic]` abort) before landing the revert -- this is a **permanent sokol_gfx API boundary**, not a "not implemented yet" gap, the same kind of declared boundary `WebGPUGraphicsBackend`/`D3D9RenderTargetCubeBackend` report for their own reasons in REMED-GFX-141's own oracle (reused here as `Sokol_RenderTargetCube_MsaaFace`, 9/9 checks passing with `msaaEngages=false` declared). MRT (`SetRenderTargets` with more than one binding) remains `NotYetImplemented`. |
| SOKOL-27 | `TextureCube` and `Texture3D` | ✅ | `TextureCube`: `SokolTextureCubeBackend` is a pure CPU-shadow store (no `sg_image` — nothing on this backend samples a cube yet), with real per-mip-level, per-face `SetData`/`GetData` round-trips. `CnaTests`' `TextureCubeTest` suite (49 tests) plus the XNB/CNJ cube content-loading tests all flipped from the "no cube storage" group to the "supported" group and pass. `Texture3D`: `SokolTexture3DBackend` is the same pure CPU-shadow-store shape (no `sg_image` — nothing on this backend samples a volume texture yet), storing one flat voxel buffer per mip level with real box-region `SetData`/`GetData` round-trips (`CalculateVolumeMipLevels` mirrors `Texture3D.cpp`'s own width/height-only mip-count algorithm — depth halves per level but never participates in level *count*, matching FNA). `SupportsCapability(Texture3D)` flipped from `false` to `true`; `Texture3DTest` (39 real cases + `Texture3DUnsupportedBackendTest` correctly skipped), `CnjTexture3DTest`, and `Texture3DTextureCubeContentTypeReaderTest` all needed zero per-file contract edits since they already gate purely on the runtime capability flag. Along the way, fixed a real, non-SOKOL-specific `VertexBuffer`/`DrawColored3D` bug found via the occlusion-query quad oracles (see SOKOL-29) that also affects every other typed-vertex 3D draw path. |
| SOKOL-28 | Custom `ShaderEffect` — needs a runtime GLSL path or a shdc-at-build-time contract | ⬜ | `CreateEffectBackend` returns null today |
| SOKOL-29 | Occlusion queries | ✅ | sokol_gfx exposes no query API, so `SokolOcclusionQueryBackend` issues raw `glGenQueries`/`glBeginQuery(GL_SAMPLES_PASSED)`/`glEndQuery`/`glGetQueryObjectuiv` directly against the same GL context sokol renders through -- a query records whatever gets rasterized between Begin/End regardless of which layer issued the draw calls. GL-only, matching `ReadBackbuffer`'s own boundary (`SupportsCapability(OcclusionQuery)` is false on any non-GL `CNA_SOKOL_API`). Tracks `active_`/`hasResult_` so an invalid call sequence (double `Begin()`, an `End()` with no active `Begin()`, or reading `IsComplete()`/`PixelCount()` before any completed cycle) never reaches `glBeginQuery`/`glEndQuery`/`glGetQueryObjectuiv` at all -- OpenGL raises `GL_INVALID_OPERATION` for every one of those, which stays pending until sokol_gfx's own next GL call trips its internal `glGetError()==0` assertions at a totally unrelated call site (found and fixed via `Sokol_OcclusionQuery_Cycle`'s Task 442-444 invalid-sequence checks, which reproduced the crash before this guard existed). Verified with three shared, backend-agnostic oracles: `Sokol_OcclusionQuery_Cycle` (Begin/End/IsComplete/PixelCount plus every invalid-sequence and dispose-while-active case), `Sokol_OcclusionQuery_VisibleQuad` and `Sokol_OcclusionQuery_OccludedQuad` (real BasicEffect + depth-tested geometry, pixel-verified). Fixed a real, non-SOKOL-specific bug along the way: `VertexBuffer`'s NOXNA auto-detect constructor (`VertexBuffer(device, count)`) stores an element-less `VertexDeclaration`, which `UploadValidatedData` still forwards to the backend before every upload -- `DrawColored3D` was treating that as a real (but useless) declaration and failing "no usable Position element" instead of falling back to its stride-based heuristic; it now falls back whenever the declaration has zero elements, the same as no declaration at all. |

### Phase 6 — Portability and hardware (not started)

| ID | Task | Status | Notes |
|---|---|---|---|
| SOKOL-30 | Verify on real discrete-GPU hardware, not just llvmpipe | ⬜ | Everything verified so far is Mesa software GL under Xvfb |
| SOKOL-31 | Implement the non-GL context paths so `CNA_SOKOL_API` other than `GLCORE` is real | ⬜ | Warns at configure time today |
| SOKOL-32 | Emscripten/WebGL2 build via `CNA_SOKOL_API=GLES3` | ⬜ | Shaders already build for `glsl300es` |

---

## Regenerating the shaders

```bash
# sokol-shdc is not vendored; grab the prebuilt binary for your platform
git clone --depth 1 https://github.com/floooh/sokol-tools-bin.git
python3 src/CNA/Internal/Backends/Sokol/shaders/compile_shaders.py \
    --shdc sokol-tools-bin/bin/linux/sokol-shdc
git diff src/CNA/Internal/Backends/Sokol/shaders/sokol_shaders.hpp   # review, then commit
```

`--check` instead of a plain run verifies the checked-in header is current without writing it.

## Building and testing

```bash
cmake -S . -B cmake-build-sokol -G Ninja -DCMAKE_BUILD_TYPE=Debug \
      -DCNA_GRAPHICS_BACKEND=SOKOL -DCNA_TEST_DISPLAY=:99
cmake --build cmake-build-sokol --target cna_test_sokol_smoke cna_test_sokol_2d -j4
ctest --test-dir cmake-build-sokol -R Sokol --output-on-failure
```

Both tests need a real (or Xvfb) X display and a GL 4.1 driver; they exit 77 (SKIP) when none is
available. `mesa-common-dev`/`libgl1-mesa-dev` must be installed — `find_package(OpenGL REQUIRED)`
fails the configure otherwise, deliberately, rather than letting the build reach a confusing
"GL/gl.h: No such file".
