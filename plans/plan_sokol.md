# sokol_gfx Graphics Backend — Implementation Plan

> **Status legend** (matches this repo's own convention): ✅ implemented *and verified against its
> stated acceptance criteria*; 🟨 code or documentation exists but has not met those criteria;
> ⬜ not implemented.
>
> **Update (2026-08-04): `SOKOL-22`, `SOKOL-23` and `SOKOL-24` all closed.** `SOKOL-22`'s
> remaining scope (Position/Color/TextureCoordinate/Normal/BlendWeight/BlendIndices vertex
> declaration elements) was already complete; closing it mainly corrected stale plan/doc claims
> that no CNA backend had PBR yet (`PbrEffect`/`SkinnedPbrEffect` are real everywhere except
> Sokol -- newly tracked as `SOKOL-49`). `SOKOL-23` turned out not to be the permanent gap this
> plan previously claimed: `RasterizerState.FillMode == WireFrame` is now implemented via the same
> CPU-side triangle-to-`GL_LINES` re-expansion `EasyGLGraphicsBackend::DrawWireframe()` already
> uses (`Sokol_WireFrame`). `SOKOL-24` replaced the always-recreate-per-`SetData()` `VertexBuffer`/
> `IndexBuffer` path with a `dynamic_update` sokol_gfx buffer reused via `sg_update_buffer()`
> across same-shape uploads (`Sokol_VertexBuffer_Reupload`). All three have their own detailed
> closing notes in the task table below. All registered Sokol CTest cases now pass (34 -> 35 with `Sokol_RenderTargetCube_Mip`
> -> 36 with `Sokol_StateLifetimeRegressionMatrix` -> 37 with `Sokol_WireFrame` and
> `Sokol_VertexBuffer_Reupload`; the total is **37**, re-derived at integration from
> `cmake/Tests/SokolTests.cmake` itself, which is one fewer than the 38 this note first claimed).
>
> **Audit update (2026-08-03, commit `63a308d4`): the GLCORE backend is broadly functional but
> the plan is not complete and several previously-green tasks need corrective follow-up.** All 28
> registered Sokol GPU integration tests passed under Xvfb `:99`; `CnaTests` built successfully and
> 109 targeted Texture3D/TextureCube/MRT/backend-identity tests passed (one known intermittent
> SDL/Xvfb initialization failure passed on immediate retry). These happy-path results do not cover
> the state-transition, cross-resource and device-disposal defects recorded in Phase 8 below.
> The task table currently contains 33 ✅, 2 🟨 and 4 ⬜ entries even before the audit findings, so
> this plan must not be described as fully implemented. Phase 8 is the authoritative status override
> for affected older tasks (`SOKOL-9`, `SOKOL-15`, `SOKOL-28`, `SOKOL-29`, `SOKOL-39`) until their
> corrective tasks and regression tests are complete.
>
> **Remediation update (2026-08-03, same day): Phase 8 CLOSED, all nine tasks (SOKOL-40..48).**
> Every corrective-follow-up task from the audit above landed with committed regression tests
> (commits `fbf91773`, `38220666`, `929abaf8`, `374d676f`, `7a49419d`) -- blend-factor pipeline
> caching, full raw-GL graphics state in both custom-effect draw paths, `OcclusionQuery`/
> `ShaderEffect` backend release on `Dispose()`, cross-object occlusion-query coordination,
> custom-effect texture-rebind survival across `SetData()` (with one documented residual boundary
> for `Texture2D.SetData(Color[], int)` specifically -- see SOKOL-44's own row), transactional
> GL-context construction, a doc/comment sync against the landed implementation, committed
> `RenderTargetCube` mip coverage, and a compact state-transition/lifetime regression matrix. All
> 36 registered Sokol CTest cases pass, plus the `CnaTests` `OcclusionQuery`/`ShaderEffect`/
> `Effect` suites.
>
> **Historical status (2026-07-31): the 2D baseline was implemented and pixel-verified.**
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
> **Historical limitation note:** the original statement here listed skinned/dual-texture/
> environment-mapped shading, custom effects, render targets, cube/volume textures and occlusion
> queries as absent. Those features landed later in `SOKOL-25..39`; PBR remains absent. The current
> capability boundary and newly-discovered defects are recorded below, especially in Phase 8.

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

6. **User `VertexBuffer`/`IndexBuffer` uploads reuse a `dynamic_update` buffer via
   `sg_update_buffer()`, sized once to the owning buffer's own declared capacity.** Closed by
   `SOKOL-24`: a per-backend frame counter (`GetFrameIndexEXT()`, incremented once per
   `Present()`/`sg_commit()`) lets each buffer detect whether it has already been updated this
   frame -- sokol_gfx's own one-update-per-buffer-per-frame rule -- and falls back to the original
   destroy-and-recreate behaviour only for a same-frame repeat upload or one that outgrows what is
   allocated.

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
| SOKOL-22 | Honour every `SetVertexDeclaration()` element, not just Position/Color usage index 0 | ✅ | **Closed 2026-08-04.** Position/Color/TextureCoordinate/Normal (every stock effect through `EnvironmentMapEffect`) and BlendWeight/BlendIndices (`SkinnedEffect`, `SOKOL-35`) are all read from a real `VertexDeclaration` at usage index 0, verified by `DrawColored3D`'s own `Pipeline3DKey` construction (`SokolGraphicsBackend.cpp`). The one remaining usage pair, Tangent/Binormal, only has meaning for a normal-mapping/PBR shader -- see `SOKOL-49`, a newly-scoped separate task (not folded back into this one, matching the size difference documented there). |
| SOKOL-23 | Full `ApplyRasterizerState` (fill mode, depth bias) and stencil in the pipeline key | ✅ | Stencil and depth bias done. Stencil: `ApplyDepthStencilState` wires the real stencil state (front/back ops, compare, read/write masks, reference) into `Pipeline3DKey`/`Get3DPipeline` -- `sg_stencil_state.ref` is baked into the pipeline object itself (sokol_gfx has no dynamic stencil-ref call, unlike most APIs), so it is part of the key too. Depth bias: `RasterizerState.DepthBias`/`SlopeScaleDepthBias` map straight onto `sg_depth_state.bias`/`bias_slope_scale` -- the same values EasyGL/Vulkan pass to `glPolygonOffset(slopeScaleDepthBias, depthBias)` -- also baked into the pipeline, also part of the key; verified with Task 767's own EasyGL-authored (but backend-agnostic) coplanar-redraw test, reused as `Sokol_RasterizerState_DepthBias`. Neither reaches the sprite pipeline (XNA's SpriteBatch never uses stencil or depth bias). Cull mode landed with `SOKOL-20`. **Fill mode (`WireFrame`) closed 2026-08-04**: this plan's own prior claim that sokol_gfx's lack of a native polygon-fill-mode API made this a "genuine, permanent gap" was wrong -- it only means sokol_gfx cannot toggle fill mode on an existing pipeline, not that wireframe rendering is unreachable. `BuildWireframeLineIndicesEXT()` ports the same technique `EasyGLGraphicsBackend::DrawWireframe()` already uses: when `RasterizerState.FillMode == WireFrame` and the primitive is triangle-based, `DrawColored3D` builds a doubled-edge line-index list (a-b, b-c, c-a per triangle) -- reading the source indices back from the GPU via `glGetBufferSubData` (`sg_gl_query_buffer_info()` exposes the raw GL buffer name; Sokol keeps no CPU shadow) for indexed draws, or generating them sequentially for non-indexed ones -- uploads it into a scratch immutable `sg_buffer` recreated per draw, and overrides the pipeline key's `primitiveType`/`indexType` plus the draw call's index buffer/count to `SG_PRIMITIVETYPE_LINES`. `GraphicsCapability::WireFrame` reported `false` when this row was written, on the reading that the flag means *native* rasterizer polygon-mode support; **corrected to `true` at post-audit integration** under REMED-GFX-209, which defines the query by what a caller observes rather than by which mechanism delivers it -- see the integration section below for the pixel oracle's own reading. New `Sokol_WireFrame` test draws a two-triangle quad both indexed and non-indexed, confirming `Solid` fills the interior, `WireFrame` leaves the interior black (only edges draw) and the left edge itself rasterizes red -- the edge check scans a small pixel strip rather than one exact column, since a 1px GL line at an exact integer boundary coordinate can rasterize into the column on either side depending on the driver's fill-rule tie-break. |
| SOKOL-24 | Cheaper `VertexBuffer`/`IndexBuffer` re-upload than recreate-per-`SetData` | ✅ | **Closed 2026-08-04.** `SokolVertexBufferBackend`/`SokolIndexBufferBackend` now allocate a `dynamic_update` sokol_gfx buffer once -- sized to the owning `VertexBuffer`/`IndexBuffer`'s own declared capacity, which `VertexBuffer::ValidateSetDataRange()`'s XNA-layer bounds check guarantees no `SetData()` call can exceed -- and reuse it in place via `sg_update_buffer()` for same-shape re-uploads, instead of destroying and recreating an immutable buffer every call. `SokolGraphicsBackend::GetFrameIndexEXT()` (a counter incremented once per `Present()`/`sg_commit()`) lets each buffer tell whether it has already been updated this frame; sokol_gfx permits only one `sg_update_buffer()` per buffer per frame (a second call trips a hard `SOKOL_ASSERT`, not just a validation warning), so a same-frame repeat upload, or one whose data outgrows what is currently allocated, still falls back to destroy-and-recreate -- the same behaviour this class used unconditionally before this task, never a regression. New `Sokol_VertexBuffer_Reupload` test proves both halves: three consecutive frames of same-shape `SetData()` calls each show the newly-uploaded colour on read-back *and* report the identical `sg_buffer` id all three times (the actual optimisation -- a pixel-only test would pass identically against the old always-recreate code), then a fourth check calls `SetData()` twice within one frame and confirms the final draw shows the last upload's colour with a genuinely new buffer id (the forced-recreate fallback firing correctly, not silently skipping the second update or corrupting data). |

### Phase 5 — Render targets and remaining resources (SOKOL-25/26/27/28/29 landed)

| ID | Task | Status | Notes |
|---|---|---|---|
| SOKOL-25 | `RenderTarget2D` via `sg_view` colour/depth-stencil attachments | ✅ | `SetRenderTarget2D`/`SetRenderTargets` bind a real colour(+depth-stencil) attachment pass; sampling a render target as a texture (SpriteBatch and 3D alike) and `GetBackBufferData()` both work whether or not a target is bound. 8 shared cross-backend oracles registered and passing: `Sokol_RenderTarget_ViewportScissorReset`, `Sokol_RenderTarget2D_Depth`, `Sokol_RenderTarget_DepthStencilUsage`, `Sokol_RenderTarget_PassBoundary`, `Sokol_RenderTarget_ProducerConsumer`, `Sokol_RenderTarget_BackbufferConsumer`, `Sokol_RenderTarget_FirstUse`, `Sokol_RenderTarget_SamplingOrientation`. `multiSampleCount` is real as of `SOKOL-26` (see its own row); direct `GetData()` readback is real as of `SOKOL-38`; `mipMap=true` is real as of `SOKOL-39`. |
| SOKOL-26 | `RenderTargetCube`, MRT, render-target MSAA resolve | ✅ | `RenderTargetCube` done: `SokolRenderTargetCubeBackend` is a real `sg_image` (`SG_IMAGETYPE_CUBE`, `usage.color_attachment`) with one colour-attachment view per face (`sg_view_desc.color_attachment.slice`) plus a single shared 2D depth-stencil attachment (matching FNA3D's one-buffer-per-cube convention, not per-face) — bind, draw, unbind and cross-face content isolation all pixel-verified via the same shared oracles SOKOL-25 reused (`Sokol_RenderTarget_PassBoundary`'s C1/C2 legs, `Sokol_RenderTarget_DepthStencilUsage`'s U1-U4, plus the cube legs in `Sokol_RenderTarget_FirstUse`/`BackbufferConsumer`). `RenderTargetCube::GetData` is not implemented (same `NotSupportedException` boundary as `RenderTarget2D`); mipMap and multiSampleCount follow `CreateRenderTarget2D`'s own conventions (throw / silently clamp to 1). `RenderTarget2D` MSAA + resolve done: `SokolRenderTargetBackend` clamps the requested `multiSampleCount` to the driver's real `GL_MAX_SAMPLES` (mirroring EasyGL's identical clamp) and, when it stays above 1, allocates a multisample-only colour image plus a separate single-sample resolve image and (if a depth format was requested) a matching-sample-count depth-stencil image, following sokol_gfx.h's own documented offscreen-MSAA workflow -- naming a resolve-attachment view in the pass triggers sokol_gfx's automatic resolve at `sg_end_pass()`. The sprite and 3D pipeline caches both gained a `sampleCount` key field (`CurrentPassSampleCountEXT()`) since sokol_gfx bakes `sample_count` into the pipeline and rejects a mismatch against the pass it draws into -- a real, independently-discovered gap this task fixed (every pipeline previously used the *window's* sample count unconditionally, which would have mismatched the first time a target's own MSAA count differed from it). Verified with Task 337's own differential real-anti-aliasing proof (renders a diagonal-edged triangle at `MultiSampleCount=0` and `8`, asserts the 8x row has genuinely blended pixels the 0x row does not) -- backend-agnostic despite its "EasyGL" filename, reused as `Sokol_RenderTarget2D_Msaa`. `RenderTargetCube` MSAA was attempted the same way and reverted: sokol_gfx's own validation layer hard-rejects a `SG_IMAGETYPE_CUBE` image with `sample_count > 1` (`VALIDATE_IMAGEDESC_ATTACHMENT_MSAA_CUBE_IMAGE`), confirmed empirically (a real `[sg][panic]` abort) before landing the revert -- this is a **permanent sokol_gfx API boundary**, not a "not implemented yet" gap, the same kind of declared boundary `WebGPUGraphicsBackend`/`D3D9RenderTargetCubeBackend` report for their own reasons in REMED-GFX-141's own oracle (reused here as `Sokol_RenderTargetCube_MsaaFace`, 9/9 checks passing with `msaaEngages=false` declared). **MRT done (landed as a follow-up once `SOKOL-28` closed the custom-effect blocker noted below):** 2-4 `RenderTarget2D` targets can now be bound together via `SetRenderTargets`, backed by a real multi-attachment `sg_pass` -- `currentRenderTarget_` (slot 0) stays the single-target tracking field every existing depth/sample-count/size/mip-regen path already reads (matching `EasyGLGraphicsBackend`'s own "slot 0 owns depth, size and sample count" MRT convention exactly), with a new `mrtExtraTargets_` vector holding slots 1..N-1; `BeginPassIfNeeded()` loops it to populate `pass.attachments.colors[1..N-1]`/`resolves[1..N-1]`/`pass.action.colors[1..N-1]`, all sharing slot 0's clear colour (XNA's `ClearOptions` has no per-attachment granularity). A `RenderTargetCube` face combined with any other target in one set is not implemented (matches `EasyGLGraphicsBackend`'s own choice to reject that combination outright). Both the raw-GL custom-effect draw path (`DrawCustomEffect3D`, `SOKOL-28`) and every stock `sg_pipeline` (sprite/3D/instanced) now work correctly while MRT is bound: `sg_pipeline_desc.color_count`/`colors[]` are looped up to a new `colorAttachmentCount` pipeline-key field (`CurrentPassColorAttachmentCountEXT()`) since sokol_gfx requires a pipeline's `color_count` to exactly match the active pass's real attachment count -- every stock fragment shader (colored3d/textured3d/lit3d/dualtextured3d/skinned3d/envmap3d/sprite.glsl) has only ever declared output location 0, so a stock draw during MRT writes attachment 0 only and leaves slots 1..N-1 untouched, matching `EasyGLGraphicsBackend`'s identical documented behaviour (`rendertarget_pass_boundary_test.cpp`'s own M2 check: *"the 2D sprite pipeline writes colour attachment 0 only"*). `DrawCustomEffect3D` additionally gained two general, MRT-adjacent fixes exposed by testing this for the first time: real `glColorMaski` per attachment slot from `ColorWriteChannels0..3` (this raw-GL draw bypasses `sg_pipeline` entirely, so `BlendState`'s write masks were never applied to it before, single-target or not), and real `glEnable(GL_DEPTH_TEST)`/`glDepthFunc`/`glDepthMask` from `DepthStencilState` (same reasoning -- this path had never applied any depth state of its own, relying entirely on whatever a prior stock-pipeline draw happened to leave in the raw GL context). Verified with REMED-GFX-016's own backend-agnostic four-output-`ShaderEffect` MRT oracle (no `CNA_BACKEND_*` conditionals, already registered for EasyGL), reused unmodified as `Sokol_MRT` -- 20/20 checks pass on the first real run: 1-4 ordered targets, distinct per-slot outputs, immediate producer-to-consumer sampling, per-slot `ColorWriteChannels0..3`, first-target depth ownership (including the "depthless first target does not borrow a later slot's depth buffer" case), attachment-set transitions/replacement, `RenderTargetUsage` Discard/Preserve across an MRT set, and true MRT MSAA with independent per-slot resolve. Also re-verified: `rendertarget_pass_boundary_test.cpp`'s own M1/M2 MRT checks (previously skipped -- `TryBindMrt` reported "this backend refuses two simultaneous render targets" -- now bind for real and pass, 41/41 total), and `GraphicsDeviceValidationTest.SetRenderTargets_FourTargets_DoesNotThrow` (Sokol removed from that test's single-target-only backend list). |
| SOKOL-27 | `TextureCube` and `Texture3D` | ✅ | `TextureCube`: `SokolTextureCubeBackend` has real per-mip-level, per-face `SetData`/`GetData` round-trips (promoted to a real `sg_image`/`sg_view` cube texture by `SOKOL-34`, see its own row). `CnaTests`' `TextureCubeTest` suite (49 tests) plus the XNB/CNJ cube content-loading tests all flipped from the "no cube storage" group to the "supported" group and pass. `Texture3D`: `SokolTexture3DBackend` is the same pure CPU-shadow-store shape (no `sg_image` — nothing on this backend samples a volume texture yet), storing one flat voxel buffer per mip level with real box-region `SetData`/`GetData` round-trips (`CalculateVolumeMipLevels` mirrors `Texture3D.cpp`'s own width/height-only mip-count algorithm — depth halves per level but never participates in level *count*, matching FNA). `SupportsCapability(Texture3D)` flipped from `false` to `true`; `Texture3DTest` (39 real cases + `Texture3DUnsupportedBackendTest` correctly skipped), `CnjTexture3DTest`, and `Texture3DTextureCubeContentTypeReaderTest` all needed zero per-file contract edits since they already gate purely on the runtime capability flag. Along the way, fixed a real, non-SOKOL-specific `VertexBuffer`/`DrawColored3D` bug found via the occlusion-query quad oracles (see SOKOL-29) that also affects every other typed-vertex 3D draw path. |
| SOKOL-28 | Custom `ShaderEffect` — needs a runtime GLSL path or a shdc-at-build-time contract | ✅ | New `SokolEffectBackend` (`IEffectBackend`) compiles real GLSL at runtime via raw `glCreateShader`/`glCompileShader`/`glLinkProgram` — sokol-shdc's own build-time reflection has no runtime equivalent, and `sg_apply_uniforms`' block-oriented, slot-numbered model is incompatible with `IEffectBackend`'s per-name, call-anytime `SetUniformXxx()` contract without a GLSL-uniform-block parser this codebase has no other use for, so a custom-effect draw bypasses `sg_shader`/`sg_pipeline` entirely instead (a genuinely new architecture for this backend, not a new static-shader variant like SOKOL-33/34/35/36) -- raw GL calls bracketed by `sg_reset_state_cache()` on both sides, sokol_gfx's own documented pattern for interleaving native GL calls with its own resource lifecycle (`sokol_gfx.h`: "call `sg_reset_state_cache()` after calling native 3D-API functions, and before calling any sokol_gfx function"). GL-only (`CNA_SOKOL_HAS_GL_READBACK`), the same boundary `OcclusionQuery`/`ReadBackbuffer` already declare; `SupportsCapability(CustomEffects)` flipped from `false` to `true` accordingly. Ported from `EasyGLEffectBackend`'s own shape: no upfront reflection, each `SetUniformXxx()` does its own `glGetUniformLocation()` lookup and silently no-ops for an unknown name; vertex attributes use the same "`layout(location=N)` == Nth field of the `VertexDeclaration`" fixed-position convention `EasyGLGraphicsBackend::ApplyLayout()` already established (a new `BuildCustomEffectAttribLayoutEXT` helper resolves this from a real declaration or the same 16/20/24/32/52-byte fallback strides the stock-effect dispatch already recognises). Two consumers wired: (1) `DrawColored3D`'s new `DrawCustomEffect3D` path for `GraphicsDevice.Draw*` calls with a bound custom `Effect`, and (2) `SokolSpriteBatchBackend::SetCustomEffect`/`DrawSpriteRunEXT`'s own raw-GL branch for `SpriteBatch.Begin(..., effect)` -- both real, not just one half of the gap-table's "SpriteBatch.Begin(effect) / arbitrary ShaderEffect" row. `SetUniformXxx()` uses `glProgramUniform*` (GL 4.1 core, `ARB_separate_shader_objects`) rather than plain `glUniform*`: real `ShaderEffect` usage calls `Effect::Apply()` (which does **not** bind this backend's GL program at all, only `Effect::OnApply()`/`SetCurrentEffect()`) then `SetTexture()`/`SetUniformXxx()`, all *before* the eventual draw call -- plain `glUniform*` would silently write to whatever program `glUseProgram` last bound (not necessarily this one, or none), while `glProgramUniform*` writes directly to the named program object regardless of current binding, immune to that ordering by construction. A second, independent real bug surfaced and fixed the same way: `BindTexture()`/`BindTextureCube()` initially applied their raw `glActiveTexture`/`glBindTexture` calls immediately, which worked when called from inside `DrawSpriteRunEXT`'s own bracket but silently failed for the same "`SetTexture()` called long before the eventual draw" pattern -- `BeginPassIfNeeded()` (called much later, inside the actual draw) was observed to clear/reassign the GL texture-unit binding in between (`GL_TEXTURE_BINDING_2D` read back 0 at draw time despite a successful earlier bind, no GL error either side), discovered via a real 3D-draw test failing with pure black output despite every uniform/vertex/program diagnostic checking out. Fixed by recording pending texture binds (`SokolEffectBackend::pendingTextureBinds_`) and only realizing them via a new `ApplyPendingTextureBindsEXT()` call from inside `DrawCustomEffect3D`, after `BeginPassIfNeeded()`/`sg_reset_state_cache()` have already run -- mirroring sokol_gfx's own record-now/apply-at-`sg_apply_bindings()`-time binding model. Verified with two pre-existing backend-agnostic oracles reused unmodified: Task 132's `easygl_shader_effect_test.cpp` (SpriteBatch custom effect, registered as `Sokol_ShaderEffect_SpriteBatch`) -- exact `(255,0,0)`/`(0,255,0)` centre/background; and Task 1079's `easygl_shadereffect_3d_test.cpp` (a real `GraphicsDevice.DrawIndexedPrimitives()` call with a `.cnj`-loaded custom effect, registered as `Sokol_ShaderEffect_3D`) -- both World-facing (exact `(200,100,50)`) and World-rotated-away (exact `(0,0,0)`, N·L clamped) checks pass, the second one being the discriminating proof the World matrix genuinely reaches the vertex shader and affects world-space lighting. |
| SOKOL-29 | Occlusion queries | ✅ | sokol_gfx exposes no query API, so `SokolOcclusionQueryBackend` issues raw `glGenQueries`/`glBeginQuery(GL_SAMPLES_PASSED)`/`glEndQuery`/`glGetQueryObjectuiv` directly against the same GL context sokol renders through -- a query records whatever gets rasterized between Begin/End regardless of which layer issued the draw calls. GL-only, matching `ReadBackbuffer`'s own boundary (`SupportsCapability(OcclusionQuery)` is false on any non-GL `CNA_SOKOL_API`). Tracks `active_`/`hasResult_` so an invalid call sequence (double `Begin()`, an `End()` with no active `Begin()`, or reading `IsComplete()`/`PixelCount()` before any completed cycle) never reaches `glBeginQuery`/`glEndQuery`/`glGetQueryObjectuiv` at all -- OpenGL raises `GL_INVALID_OPERATION` for every one of those, which stays pending until sokol_gfx's own next GL call trips its internal `glGetError()==0` assertions at a totally unrelated call site (found and fixed via `Sokol_OcclusionQuery_Cycle`'s Task 442-444 invalid-sequence checks, which reproduced the crash before this guard existed). Verified with three shared, backend-agnostic oracles: `Sokol_OcclusionQuery_Cycle` (Begin/End/IsComplete/PixelCount plus every invalid-sequence and dispose-while-active case), `Sokol_OcclusionQuery_VisibleQuad` and `Sokol_OcclusionQuery_OccludedQuad` (real BasicEffect + depth-tested geometry, pixel-verified). Fixed a real, non-SOKOL-specific bug along the way: `VertexBuffer`'s NOXNA auto-detect constructor (`VertexBuffer(device, count)`) stores an element-less `VertexDeclaration`, which `UploadValidatedData` still forwards to the backend before every upload -- `DrawColored3D` was treating that as a real (but useless) declaration and failing "no usable Position element" instead of falling back to its stride-based heuristic; it now falls back whenever the declaration has zero elements, the same as no declaration at all. |

### Phase 6 — Portability and hardware (not started)

| ID | Task | Status | Notes |
|---|---|---|---|
| SOKOL-30 | Verify on real discrete-GPU hardware, not just llvmpipe | ⬜ | Everything verified so far is Mesa software GL under Xvfb |
| SOKOL-31 | Implement the non-GL context paths so `CNA_SOKOL_API` other than `GLCORE` is real | ⬜ | Warns at configure time today |
| SOKOL-32 | Emscripten/WebGL2 build via `CNA_SOKOL_API=GLES3` | ⬜ | Shaders already build for `glsl300es` |

### Phase 7 — Closing the EasyGL feature gap (SOKOL-33/34/35/36/37/38/39 landed)

Derived directly from the "Feature gap vs. EasyGL" table below: every gap item that is genuinely
implementable (not permanently blocked by a missing sokol_gfx API, and not already covered by an
open task above) gets its own task here.

| ID | Task | Status | Notes |
|---|---|---|---|
| SOKOL-33 | `DualTextureEffect` (base + overlay texture multiply) | ✅ | New `dualtextured3d.glsl` shader variant (`Shader3DKind::DualTextured`), the same shape as `lit3d.glsl`/`textured3d.glsl` -- a stock XNA effect, not a user-authored one, so this did **not** need `SOKOL-28`'s general custom-shader infrastructure. Implements FNA's exact `PSDualTexture` formula (`base=SAMPLE(Texture); base.rgb*=2; color=base*overlay*Diffuse*Alpha`, no alpha test -- FNA's `DualTextureEffect.fx` has none). Both texture slots reuse the SAME `texcoord0` (this codebase has no second-UV-set concept in `GpuDrawParams` or anywhere else -- `EasyGLGraphicsBackend::EnsureDualTextured3DProgram()` does the identical thing), fall back to the shared 1x1 opaque-white texture when left null (`docs/dualtextureeffect-support.md` Tasks 386/387's established convention), and are sampled with the same `SamplerStates[0]` (matching EasyGL's own no-distinct-second-sampler simplification, not real XNA's separate s0/s1 registers). `rtFlipV` (see `SOKOL-38`) is `vec2`-per-slot since either texture can independently be a render target. Verified with Task 889's own backend-agnostic `VertexColorEnabled` oracle (`dualtextureeffect_vertexcolor_test.cpp`, no `CNA_BACKEND_*` conditionals), reused as `Sokol_DualTextureEffect_VertexColor` -- both cases pass with exact expected RGB (`(96,32,32)` enabled, `(123,82,164)` disabled), which also exercises the base doubling+overlay+diffuse+alpha formula both cases share. Fixed a related, narrowly-scoped gap the stride-based undeclared-VertexBuffer fallback switch had: strides 20/24 (`VertexPositionTexture`/`VertexPositionColorTexture`) only accepted `Shader3DKind::Textured` before this task, rejecting the identical layout under `DualTextured`. `rendertarget_sampling_orientation_test.cpp`'s CD5/CD6 legs (DualTextureEffect against a RenderTarget2D source in each texture slot) now also pass for real instead of degrading to an `NotYetImplemented`-caught INFO skip -- 55/55 checks. |
| SOKOL-34 | `EnvironmentMapEffect` (reflection cube mapping) | ✅ | Promoted `SokolTextureCubeBackend` from a pure CPU-shadow store to a real `sg_image`/`sg_view` pair (`SG_IMAGETYPE_CUBE`, `usage.immutable=true`, recreated whole on every `SetData()` -- the same "immutable-with-fresh-data has no per-frame update-count limit" reasoning `SokolTextureBackend::RecreateImage()` already uses), the first Sokol consumer of cube sampling. sokol_gfx's own `sg_image_data.mip_levels[level]` is one CONTIGUOUS range per mip level for a cube image (all 6 faces concatenated `[0]=+X,[1]=-X,[2]=+Y,[3]=-Y,[4]=+Z,[5]=-Z`, per `sokol_gfx.h`'s own doc comment), which happens to be byte-identical to this class's own per-face storage order (matching `CubeMapFace`), so no reordering was needed. New `envmap3d.glsl` shader, ported from `EasyGLGraphicsBackend::EnsureEnvMapped3DProgram()`'s own (already FNA-formula-fixed: lerp not additive, alpha-scaled specular, real Fresnel -- see `docs/environmentmapeffect-support.md`'s bug history) shader: always textured and lit, **no vertex-colour support at all** (real XNA's `EnvironmentMapEffect` has no `VertexColorEnabled` property; `EnvironmentMapEffect::FillGpuDrawParams` always sets it `false`, and `EasyGLGraphicsBackend`'s own vertex layout has no `color0` attribute either) -- `Shader3DKind::EnvMapped` therefore needs no new `Pipeline3DKey` attribute fields at all, reusing the existing position/normal/texCoord fields with no optional trailing attribute. The reflection vector and Fresnel blend factor are both computed per-VERTEX from each vertex's own un-interpolated world-space normal/eye vector then Gouraud-interpolated (matching real XNA's vertex-stage `ComputeFresnelFactor`/`EnvCoord` -- not equivalent to a per-fragment recompute once vertices carry different normals), while the diffuse-lighting sum (`NdotL` against up to 3 directional lights) is computed per-fragment from the interpolated normal, mirroring EasyGL's identical split. `GpuDrawParams::diffuseColor`/`emissiveColor` both arrive pre-multiplied by alpha for this effect specifically (`EnvironmentMapEffect::FillGpuDrawParams`'s own "emissive + ambient*diffuse, pre-combined and pre-multiplied by alpha" comment) -- unlike `BasicEffect`/`lit3d.glsl`, which keep `diffuse.rgb` unmultiplied. A missing cube map (`params.envMap == nullptr`) throws a clear error rather than sampling an unbound resource, since (unlike the base texture) no established null-fallback convention exists for it anywhere in this codebase. A new `ResolveSampledCubeViewId` helper (the cube-map analog of `ResolveSampledTextureViewId`) accepts either a plain `SokolTextureCubeBackend` or a `SokolRenderTargetCubeBackend` sampled as a dynamic-reflection environment map -- a genuine, real-XNA-legal pattern, both already implement `ITextureCubeBackend`. Verified with Task 891's own backend-agnostic oracle (no `CNA_BACKEND_*` conditionals, already registered for EasyGL/Vulkan/Bgfx), reused as `Sokol_EnvironmentMapEffect_AlphaScaledLerp` -- 2/2 checks pass on the first real run, including the discriminating translucent-effect case (`(100,50,25)`, the FNA-correct alpha-scaled-cube-sample value, not the old-bug unscaled `(200,100,50)`). The pre-existing `TextureCubeTest` suite (49 tests, SOKOL-27) and the XNB/CNJ cube content-loading tests all re-verified passing after the `SokolTextureCubeBackend` promotion. |
| SOKOL-35 | Skinned vertex support (`SkinnedEffect`, `BlendWeight`/`BlendIndices`) | ✅ | New `skinned3d.glsl` shader variant (`Shader3DKind::Skinned`) -- always textured and lit, matching FNA's `SkinnedEffect.fx` (no unlit/untextured permutation exists there), sharing `lit3d.glsl`'s exact fragment-stage lighting math verbatim (FNA's `SkinnedEffect.fx` `#include`s the same `Lighting.fxh` helpers `BasicEffect.fx` does). The vertex stage blends up to 4 bone matrices per vertex from a `mat4 bones[72]` uniform array (`SkinnedEffect::MaxBones`), gated by `GpuDrawParams::weightsPerVertex` (1/2/4, matching FNA's real `Skin(vin, boneCount)` shader behavior of only summing the first N weight/index pairs -- Task 895), then transforms Position (affine) and Normal (via the blended matrix's rotation/scale part only) before the usual world/view/projection and normal-matrix steps -- the same shape `EasyGLGraphicsBackend::EnsureSkinnedProgram()` already established (stride-52 attribute layout, same bone-array size, same weight-count gating, same post-skin fog dot). `blendindices0` is the shader's one `uvec4` (rather than `vec4`) input: sokol_gfx's own vertex-format reflection (`_sg_vertexformat_basetype`) requires an unsigned-integer GLSL attribute for `SG_VERTEXFORMAT_UBYTE4` (`VertexElementFormat::Byte4`, this codebase's `BlendIndices` format), delivered via a genuine integer vertex fetch rather than every other attribute's float conversion -- discovered as a real `VALIDATE_PIPELINEDESC_ATTR_BASETYPE_MISMATCH` panic on the first run and fixed by declaring `in uvec4 blendindices0` instead of `in vec4`. `color0` is declared LAST in the shader (after `blendweight0`/`blendindices0`, unlike `lit3d.glsl`'s position/normal/texcoord0/color0 order) so it lands in the trailing attribute slot: sokol_gfx requires every valid attribute slot to be a continuous prefix with no gap, and `color0` is the only one of this kind's five attributes that may legitimately be absent (`VertexPositionNormalTextureSkinned` has no Color element at all). Verified with two of Task 123's/REMED-GFX-008's own backend-agnostic oracles (no `CNA_BACKEND_*` conditionals), reused as `Sokol_SkinnedEffect_BoneDeformation` (a real 2-bone GPU transform + mesh deformation: `left=(0,255,0) centre=(174,0,0) right=(0,255,0)`) and `Sokol_SkinnedEffect_LightingConformance` (9/9 analytic ambient/emissive/diffuse/specular checks, each asserting the FNA-correct pixel against a historically-wrong "old bug" pixel it was written to discriminate against -- every check landed on the correct value, not the bug). Closes the skinning slice of `SOKOL-22`'s "vertex elements other than Position/Color/TextureCoordinate/Normal" gap; did not need `SOKOL-28` since `SkinnedEffect` is a stock XNA effect. |
| SOKOL-36 | Instanced draws (`GraphicsDevice.DrawInstancedPrimitives`) | ✅ | New `DrawInstancedPrimitivesEx` override plus a dedicated `instanced3d.glsl` shader, ported from `VulkanGraphicsBackend`'s own `instanced3d.{vert,frag}.glsl` (the same "VP-only in the shared uniform, World reconstructed from 4 per-instance vec4 columns in the vertex stage" shape bgfx/WebGPU's own instanced3d shaders already use) -- a deliberate, established scope reduction, not a Sokol-specific gap: flat `DiffuseColor` only, no vertex colour, texturing or lighting. Per-vertex mesh data stays at buffer slot 0 (unchanged, `step_func` defaults to `SG_VERTEXSTEP_PER_VERTEX`); the per-instance World matrix is a genuinely new consumer of buffer slot 1 (`step_func = SG_VERTEXSTEP_PER_INSTANCE`, `desc.layout.buffers[1].stride = 64`) -- confirmed via `grep` that no other feature in this backend (sprite batch, skinning, dual-texture, render targets) had ever touched a non-zero vertex-buffer slot before this task, so there was no collision risk. A new, smaller `PipelineInstanced3DKey`/`GetInstanced3DPipeline` (not `Pipeline3DKey`/`Get3DPipeline`) backs this, since instanced3d.glsl only ever reads Position from slot 0 (any stride) and the always-fixed-layout instance columns from slot 1 -- no color/texCoord/normal/blendWeight/blendIndices attribute set to key on, and no `Shader3DKind` (this key only ever targets one shader). `params.instanceVb == nullptr` falls back to a real, working `DrawColored3D` draw instead of throwing, matching `VulkanGraphicsBackend`/`D3D11GraphicsBackend`'s own identical fallback contract -- this is what `DrawColored3D`'s pre-existing (and, before this task, entirely unreachable -- `SokolGraphicsBackend` never overrode `DrawInstancedPrimitivesEx` at all, so every instanced call hit `IGraphicsBackend`'s base-class default throw before `DrawColored3D` was ever reached) `params.instanceCount > 1` refusal guarded against; that guard is now dead code for the real entry point but harmless (still correctly refuses a direct `DrawColored3D` call with `instanceCount>1` and no dedicated pipeline, which cannot happen via any public API). Verified with WEBGPU-27/38/68's own backend-agnostic oracle (no `CNA_BACKEND_*` conditionals, drives `IGraphicsBackend::DrawInstancedPrimitivesEx()` directly), reused as `Sokol_Instanced3D` -- 5/5 checks pass on the first real run: 3 distinct instances each paint their own screen-space quad with the exact shared `DiffuseColor` (proving genuine per-instance reads, not a hardcoded ≤2-instance special case), a region far from all 3 is untouched, and `instanceVb == nullptr` falls back to a real, correctly-coloured non-instanced draw rather than throwing or corrupting the frame. |
| SOKOL-37 | `Viewport.MinDepth`/`MaxDepth` | ✅ | `sg_apply_viewport` carries no depth-range parameter, so `ApplyPendingViewportAndScissor()` issues a raw `glDepthRangef(minDepth, maxDepth)` alongside the viewport/scissor rects it already reapplies on every bind and every `SetViewport`/`SetScissorRect` call -- the same GL-only escape hatch `SokolOcclusionQueryBackend`/`ReadBackbuffer` already use. Unlike `glBeginQuery`/`glEndQuery`, `glDepthRangef` never raises a GL error for any input (the spec clamps both values into `[0,1]` instead), so it needs none of `SokolOcclusionQueryBackend`'s pending-error guarding. Verified with REMED-GFX-079's own backend-agnostic 3D-viewport oracle (despite its "software_" filename), reused as `Sokol_Viewport_MinMaxDepth` -- 25/25 checks pass, including the pre-existing general-viewport-transform checks A-G/I-O this backend already had. Fixed a real, separately-discovered bug surfaced by that same oracle's check G: `ReadBackbuffer` threw `NotYetImplemented` whenever a `RenderTarget2D`/`RenderTargetCube` face was bound, on the mistaken assumption that `GraphicsDevice.GetBackBufferData` must always read the literal presented window surface -- the established, cross-backend convention this codebase actually uses (confirmed in `EasyGLGraphicsBackend::ReadBackbuffer`, and documented in the DX3/ASCII/Software backends' own tests) is "read from whatever is currently bound". sokol_gfx's GL backend does not rebind `GL_FRAMEBUFFER` back to 0 inside `sg_end_pass()` for an offscreen pass, so the fix needed no new binding call -- just removing the throw and sizing the read region from the bound target instead of the window when one is bound. |
| SOKOL-38 | `RenderTarget2D`/`RenderTargetCube` direct CPU readback (`GetData()`) | ✅ | `SokolRenderTargetBackend`/`SokolRenderTargetCubeBackend::GetData` both override `ITextureBackend`/`ITextureCubeBackend::GetData`, attaching the raw GL texture handle `sg_gl_query_image_info()` exposes to a throwaway GL FBO and `glReadPixels()`-ing it -- the same shape `ReadBackbuffer()` already uses for the default framebuffer, no sokol_gfx-level read-back API needed. Closes this half of `SOKOL-26`. Getting this working (the first tool able to observe real render-target pixel content on this backend) surfaced two real, independent, pre-existing bugs, both fixed in the same task rather than deferred: **(1) REMED-GFX-147, sampling a `RenderTarget2D` as a texture was exactly vertically mirrored.** A render target's colour image is written by real GPU rasterization (framebuffer-origin convention: CNA's logical row 0 lands at OpenGL's HIGH y), while a plain `SokolTextureBackend`'s CPU pixel buffer is uploaded byte-for-byte via `sg_make_image` with no Y-flip (row 0 lands at GL's LOW y / v=0) -- sampling both with the same UV convention silently flipped every render-target source. Fixed with a per-draw `rtFlipV` uniform (`sprite_fs_params`/`textured3d_fs_params`/`lit3d_fs_params`, applied as `mix(uv.y, 1.0-uv.y, rtFlipV)` before the texture sample), set via a new `IsRenderTargetSourceEXT()` check on the bound texture -- the same per-slot-uniform shape EasyGL's own `uRtFlipV` and bgfx's `u_rtFlipV` already use for this identical finding. This had been silently unverified since SOKOL-25 landed: `rendertarget_sampling_orientation_test.cpp`'s own comparisons never actually ran a real pixel comparison because `GetData()` always threw before this task, so `RequireReadable()`'s contract flag masked them -- 53/53 checks pass now that it does not. **(2) A `RenderTarget2D`/`RenderTargetCube` bound, `Clear()`-ed, and unbound with NO draw in between never actually reached the GPU at all.** `QueueClear()` only *records* the pending clear and closes any already-open pass; it never opens one. `BindSingleRenderTarget2D`/`BindRenderTargetCubeFace` (invoked on unbind) called `EndPassIfActive()` -- a no-op when no pass was ever opened -- then unconditionally reset the pending-clear flags, silently discarding a clear that was queued but never realized. `Present()` already carried the fix for the identical back-buffer-only case (its own "begin before ending so a frame whose only content was a Clear() still produces the pass" comment); the same `BeginPassIfNeeded()`-before-`EndPassIfActive()` guard, gated on a pending clear existing, now applies to both render-target bind functions too. Verified via the SOKOL-25/26 render-target oracle suite re-run with their contracts flipped to `Exact`/`true` (`rendertarget_sampling_orientation_test.cpp` 53/53, `rendertarget_producer_consumer_test.cpp` 37/37, `rendertarget_backbuffer_consumer_test.cpp` 87/87, `rendertarget_first_use_test.cpp` 26/26, `rendertarget_depthstencil_usage_test.cpp` 29/29 -- including U2's cube-shared-depth check, which hit the same Clear()-only bug on the cube path -- `rendertarget_pass_boundary_test.cpp` 40/40, `rendertargetcube_msaa_face_test.cpp` 9/9); every one of these previously degraded to a legality-only skeleton and now measures real pixels. |
| SOKOL-39 | `RenderTarget2D`/`RenderTargetCube` mip-mapped rendering (`mipMap=true`) | ✅ | Matches real D3D9 XNA's `D3DUSAGE_AUTOGENMIPMAP` semantics (confirmed via `EasyGLRenderTargetBackend`'s own established contract, `easygl_rendertarget2d_mip_test.cpp`): only level 0 is ever rendered into directly, and the rest of the mip chain is auto-regenerated from level 0's content on unbind. `SokolRenderTargetBackend`/`SokolRenderTargetCubeBackend` now take a `mipMap` constructor flag; when set, `num_mipmaps` on the colour image is `CalculateRenderTargetMipLevels(w, h)`/the existing `CalculateCubeMipLevels(size)`, which sokol_gfx's GL backend allocates as real immutable `glTexStorage2D`-backed storage for every declared level up front (`_sg_gl_create_image`/`_sg_gl_texstorage`), regardless of whether any level beyond 0 is ever written. `BindSingleRenderTarget2D`/`BindRenderTargetCubeFace` (both invoked on unbind, immediately after `SOKOL-38`'s Clear()-flush guard and before reassigning `currentRenderTarget_`/`currentRenderTargetCube_`) now call a new `RegenerateMipmapsIfNeededEXT()` on the outgoing target, a GL-only no-op when `mipMap=false`, otherwise `glGenerateMipmap` against the raw GL texture handle (`sg_gl_query_image_info`), the same escape-hatch shape as `ReadColorImagePixelsViaGL`. `GetData()` on both backends now accepts any allocated `level` (was `level != 0` only), reading via the existing throwaway-FBO helper (extended with a `level` parameter passed to `glFramebufferTexture2D`) sized to that level's `MipDimension`-halved width/height. Closes the other half of `SOKOL-26`. Verified with Task 336's shared, backend-agnostic RT2D mip-completeness oracle (registered as `Sokol_RenderTarget2D_Mip`, reused unmodified from EasyGL despite its filename) — fills level 0 solid, unbinds, samples back via `SpriteBatch` with `TextureFilter::Anisotropic`; solid black would mean a GL-incomplete mip chain, and the result is the fill colour. No shared cross-backend `RenderTargetCube` mip oracle exists yet (only per-backend bgfx/Vulkan variants) and no Sokol effect can sample a cube map at all yet (`SOKOL-34` not landed), so the cube half was additionally confirmed with a throwaway, uncommitted verification program directly against `RenderTargetCube::GetData(face, level=1, ...)`: a `32`x`32` mipMap=true cube, face `PositiveX` cleared solid green then unbound, reads back exactly `(0,255,0,255)` at mip level 1 (`LevelCount=6`, the correct `32->16->8->4->2->1` chain) — the expected mip-average of a uniform-colour source. |

`SOKOL-22` (remaining vertex declaration elements) and `SOKOL-26`'s MRT item were both formerly
blocked on `SOKOL-28` (custom `Effect` support), as noted in the gap table above. `SOKOL-28` is now
closed, giving both a real consumer (a custom `ShaderEffect` can declare arbitrary vertex inputs or
multiple fragment outputs). `SOKOL-26`'s MRT item has since been implemented in full (see its own
row above) as a direct follow-up once that blocker cleared.

**`SOKOL-22` closed 2026-08-04** for its actually-assigned scope: Position/Color/TextureCoordinate/
Normal (every stock effect through `EnvironmentMapEffect`) and BlendWeight/BlendIndices (`SkinnedEffect`,
`SOKOL-35`) are all read from a real `VertexDeclaration` at usage index 0. The remaining Tangent/
Binormal/PBR-input elements only have meaning for a normal-mapping/PBR shader, which this plan had
recorded as "no CNA backend has PBR yet, out of scope here" -- **stale**: `PbrEffect`/
`SkinnedPbrEffect` are real, implemented stock effects on every OTHER CNA backend (EasyGL, D3D9/
11/12, Vulkan, WebGPU, Bgfx, SdlGpu). Sokol is the only backend missing them. Porting them is a
genuine, sizeable feature (~1000+ lines across shaders and the XNA layer in the Vulkan
implementation alone, comparable to `SkinnedEffect`/`EnvironmentMapEffect`'s own scope), not a
"vertex declaration" bugfix -- tracked as its own task, `SOKOL-49`, rather than folded into
`SOKOL-22`.

---

### Phase 8 — Post-implementation audit remediation (SOKOL-40..48 CLOSED 2026-08-03)

This phase records defects found by reviewing the `GLCORE` implementation at commit `63a308d4`.
They were not contradicted by the green integration suite at the time: the existing tests exercised
the principal happy paths but did not cover repeated cache-key state changes, raw-GL state inherited
from a previous draw, two query objects active at once, resources that outlive device disposal, or a
texture upload between `ShaderEffect::SetTexture()` and the eventual draw. All nine tasks
(`SOKOL-40`..`SOKOL-48`) are now closed with committed regression tests (commits `fbf91773`,
`38220666`, `929abaf8`, `374d676f`, `7a49419d`); 36 registered Sokol CTest cases pass in full,
plus `CnaTests`' `OcclusionQuery`/`ShaderEffect`/`Effect` suites.

| ID | Task | Status | Finding and acceptance criteria |
|---|---|---|---|
| SOKOL-40 | Fix constant blend-colour pipeline caching (`SOKOL-15` corrective follow-up) | ✅ | `blendFactor_[4]` is copied into `sg_pipeline_desc.blend_color` when a sprite, stock-3D or instanced-3D pipeline is created, but it is absent from `PipelineKey`, `Pipeline3DKey` and `PipelineInstanced3DKey`. An otherwise-identical draw after changing `GraphicsDevice.BlendFactor` therefore reuses the first cached constant; `Blend::BlendFactor` and `Blend::InverseBlendFactor` render with stale state. **Closed 2026-08-03**: a packed `blendFactorPacked` (0xRRGGBBAA) field now participates in equality/hashing for all three keys. New `Sokol_BlendFactor_PipelineCache` test draws the identical `BlendState` three times, changing only `GraphicsDevice.BlendFactor` between draws (never re-touching `BlendState`, so every other key field stays bit-identical) -- (200,100,0)→(0,100,200)→(200,100,0), each read back exactly. |
| SOKOL-41 | Apply complete graphics state in both raw-GL custom-effect paths (`SOKOL-28` corrective follow-up) | ✅ | `DrawSpriteRunEXT`'s custom branch binds a program/texture/VAO and draws without applying BlendState, colour masks, depth/stencil state or rasterizer state; it also omits `SokolEffectBackend::ApplyPendingTextureBindsEXT()`, despite that method's header claiming SpriteBatch is a caller, so extra `ShaderEffect::SetTexture(unit, ...)` inputs are unavailable there. `DrawCustomEffect3D` applies depth and per-target colour masks only; blending (including equations/factors/constant colour), stencil, culling and depth bias still inherit arbitrary GL state from a prior Sokol pipeline. **Closed 2026-08-03**: two new shared helpers, `ApplyCustomEffectRasterStateEXT()` (depth/stencil/blend incl. `glBlendColor`/cull/winding/depth-bias) and `ApplyCustomEffectColorMasksEXT()`/`ResetCustomEffectColorMasksEXT()` (per-slot `ColorWriteChannels`), are now called from both `DrawCustomEffect3D` and `DrawSpriteRunEXT`'s custom branch, which also now calls `ApplyPendingTextureBindsEXT()`. New raw-GL enum tables (`ToGLBlendFactor`/`ToGLBlendOp`/`ToGLStencilOp`/`ToGLCullFace`) mirror the existing sokol-enum tables. New `Sokol_CustomEffect_BlendStateOrder` test (SpriteBatch path) proves both directions with real pixel readback: a custom `Opaque` draw immediately after a stock blended draw fully overwrites (stock→custom, the state-inheritance bug), a stock blended draw immediately after a custom `Opaque` draw still blends (custom→stock), and a custom `NonPremultiplied` draw genuinely blends rather than always inheriting whatever GL last had. Full stencil/cull/depth-bias coverage and the `DrawCustomEffect3D` (3D) path are left to `SOKOL-48`'s broader regression matrix; depth ownership for that path is already covered by the reused `easygl_mrt_test.cpp` oracle (`Sokol_MRT`). |
| SOKOL-42 | Release raw-GL resource backends during `Dispose()` before device/context teardown | ✅ | `GraphicsDevice::Dispose()` disposes tracked resources before destroying the backend, but `OcclusionQuery` has no `Dispose(bool)` override and `ShaderEffect` inherits `Effect::Dispose(bool)`, which only reaches `GraphicsResource`. Their `backend_` objects therefore survive logical disposal; later C++ destruction may call `glDeleteQueries`/`glDeleteProgram` after `sg_shutdown()` and SDL GL-context destruction. **Closed 2026-08-03**: both classes now override `Dispose(bool)` to reset their backend (`backend_.reset()`/`effectBackend_.reset()`) before calling into the base class, matching `VertexBuffer`/`IndexBuffer`/`Texture`'s established pattern; added `HasBackend()` accessors for testability. New `Sokol_DisposeOrder_OcclusionQueryShaderEffect` test verifies both resources are disposed and backend-released by `GraphicsDevice::Dispose()` (report safe defaults afterward), that a redundant explicit `Dispose()` does not throw, and that the C++ objects can be destroyed well after device disposal with no exception. |
| SOKOL-43 | Coordinate occlusion-query activity across objects (`SOKOL-29` corrective follow-up) | ✅ | `SokolOcclusionQueryBackend::active_` prevents only a repeated `Begin()` on the same object. OpenGL permits one active query per target per context, so `q1.Begin(); q2.Begin()` still reaches a second `glBeginQuery(GL_SAMPLES_PASSED)`, produces `GL_INVALID_OPERATION`, marks `q2` active despite its failed begin, and lets later `End()` calls end the wrong target or leave an error for sokol_gfx to assert on. **Closed 2026-08-03**: `SokolGraphicsBackend` now owns a single `activeOcclusionQueryEXT_` slot with `TryActivateOcclusionQueryEXT()`/`ReleaseOcclusionQueryEXT()`; each `SokolOcclusionQueryBackend` takes an optional owning-backend pointer and coordinates through it in `Begin()`/`End()`/its destructor (the last covering "destroyed while active" cleanup so the slot cannot be left pointing at a dangling object). A second object's overlapping `Begin()` is now silently absorbed, matching the existing same-object contract. Covered by the pre-existing `Sokol_OcclusionQuery_Cycle` regression suite (unchanged pass); a dedicated two-object interleaving test is deferred to `SOKOL-48`. |
| SOKOL-44 | Make deferred custom-effect texture bindings survive texture re-upload | ✅ | `SokolEffectBackend::BindTexture`/`BindTextureCube` records the current `sg_image` ID, while `SokolTextureBackend::RecreateImage()` and the cube equivalent destroy that image and allocate another on every `SetData()`. `effect.SetTexture(...); texture.SetData(...); draw` therefore queries a stale generation-tagged handle and may sample nothing or trip validation. **Closed 2026-08-03**: `PendingTextureBind` now stores the source `ITextureBackend*`/`ITextureCubeBackend*` and re-resolves the current `sg_image` in `ApplyPendingTextureBindsEXT()`, at draw time. This surfaced a sharper hazard while testing: `Texture2D::SetData(const Color*, int)` (unlike its sibling overloads, which mutate the same backend object's image in place) replaces `Texture2D::backend_` with an entirely new backend object, which crashed the naive re-resolve (`ResolveSampledTextureImageId` dynamic_cast on a dangling pointer, confirmed via `coredumpctl`). Fixed with `LiveSampledBackendRegistryEXT()`, a small liveness set populated by all four backend classes `ResolveSampledTextureImageId()`/`ResolveSampledCubeImageId()` accept (`SokolTextureBackend`, `SokolTextureCubeBackend`, `SokolRenderTargetBackend`, `SokolRenderTargetCubeBackend`); a pointer no longer in the registry is skipped, not dereferenced. New `Sokol_CustomEffect_TextureReupload` test covers 2D (via the in-place `SetData(level, rect, data, ...)` overload) and cube, both re-uploaded after `SetTexture()` and before the draw, reading back the new colour. The `Texture2D::SetData(const Color*, int)` whole-object-replacement case remains a documented residual boundary (degrades to "texture stays unbound", matching that overload's own pre-fix behaviour, not a crash) -- closing it fully would need a stable indirection carried across `IEffectBackend::BindTexture()`'s interface, shared by 9 backends, out of scope for a Sokol-local fix. |
| SOKOL-45 | Make GL-context construction fully transactional (`SOKOL-9` corrective follow-up) | ✅ | `CreateGpuContext()` is called before the constructor's cleanup `try` block. If `SDL_GL_CreateContext()` succeeds but `SDL_GL_MakeCurrent()` fails, construction throws while leaking the new context. **Closed 2026-08-03**: context creation now runs inside the same `try` block as `sg_setup()`/sprite-resource creation, sharing one `DestroyGpuContextIfAnyEXT()` cleanup path used by both the constructor's catch block and the destructor. A new NOXNA test-only constructor overload (`forceMakeCurrentFailureEXT`, `contextDestroyCountEXT`) makes the failure path injectable without needing to actually break SDL. New `Sokol_GLContext_Transactional` test forces a real `SDL_GL_CreateContext()` success followed by a forced `SDL_GL_MakeCurrent()` failure, verifying construction throws, exactly one context is destroyed, no backend is left registered, and the same window can immediately construct a real working backend afterward. |
| SOKOL-46 | Synchronize the plan, public capability document and inline comments with the landed implementation | ✅ | Several statements are historical but still written as current fact: old rows say 3D/custom effects/render targets/cube readback/mipmaps are absent; `docs/sokol-backend.md` says render targets cannot be multisampled and says `SOKOL-34` has not landed; the backend class/CreateRenderTarget/DrawPrimitives comments still describe the 2D-only boundary; the capability comments contradict the implementation/test contract. **Closed 2026-08-03**: rewrote `SokolGraphicsBackend`'s class-level Doxygen comment, `CreateRenderTarget2D`/`CreateRenderTargetCube`'s `@param mipMap`/`@param multiSampleCount` docs, `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx`'s effect-coverage comment, and `SupportsCapability()`'s `MultiSampleAntiAliasing`/`ThreeD` inline comments against the actual current implementation. `docs/sokol-backend.md`'s intro, its `RenderTargetCube` mip row, and its "entries need reading carefully" note were all similarly stale and rewritten; added the five SOKOL-40/41/42/44/45 regression tests to its verification table. No CMake cube-MSAA comment was found to be stale (grepped `cmake/`; none exists). |
| SOKOL-47 | Commit reproducible RenderTargetCube mipmap coverage (`SOKOL-39` verification follow-up) | ✅ | The cube half of `SOKOL-39` is marked ✅ using only a throwaway, uncommitted program, and its note incorrectly says `SOKOL-34`/cube sampling has not landed. **Closed 2026-08-03**: new `Sokol_RenderTargetCube_Mip` fills face `PositiveX` level 0 solid, unbinds (regenerating mips), then reads back every texel of levels 1 (16x16) and 2 (8x8) via `RenderTargetCube::GetData()`, plus `LevelCount` itself -- 3/3 pass. Sampling through `EnvironmentMapEffect` was deliberately not attempted: that effect has no explicit mip-bias/LOD control, so a full-screen quad has no reliable way to force a non-zero level over level 0 -- automatic GLSL LOD selection is not a controllable, reproducible test signal; direct `GetData()` readback is. |
| SOKOL-48 | Add a compact state-transition/lifetime regression matrix | ✅ | Preserve the existing 28 happy-path GPU tests, but add focused coverage for the failure dimensions exposed by this audit: first-use vs cached reuse, A→B→A state transitions, stock/custom draw ordering, two independent resource instances, mutation between bind and draw, and resource destruction after device disposal. **Closed 2026-08-03**: most of these dimensions are already covered by SOKOL-40/41/42/44/45's own per-ticket tests (A→B→A: `Sokol_BlendFactor_PipelineCache`; stock/custom ordering: `Sokol_CustomEffect_BlendStateOrder`; mutation between bind and draw: `Sokol_CustomEffect_TextureReupload`; destruction after device disposal: `Sokol_DisposeOrder_OcclusionQueryShaderEffect`); new `Sokol_StateLifetimeRegressionMatrix` adds the two dimensions those tickets' own closing notes explicitly deferred here: two independent `OcclusionQuery` instances interleaved in both begin/end orders plus a healthy fresh query afterward (SOKOL-43's own "two independent resource instances" gap), and `DrawCustomEffect3D`'s `CullMode` applied independently of a preceding stock draw's leftover GL cull state (SOKOL-41's own "the `DrawCustomEffect3D` (3D) path left to SOKOL-48" gap) -- 7/7 checks pass. |

The only work still genuinely open independently of this audit is the portability/hardware task
group `SOKOL-30..32`. Permanent upstream boundaries also remain explicit: `RenderTargetCube` MSAA
and `BlendState.MultiSampleMask` cannot currently be implemented through sokol_gfx. `SOKOL-22`
closed 2026-08-04 (see its own row above); the PBR gap it used to bundle in is now its own task,
`SOKOL-49`. `SOKOL-23` closed 2026-08-04: WireFrame fill mode turned out not to be a permanent gap
after all (see its own row above). `SOKOL-24` also closed 2026-08-04: `VertexBuffer`/`IndexBuffer`
re-upload now reuses a `dynamic_update` sokol_gfx buffer via `sg_update_buffer()` instead of
always recreating one (see its own row above).

### Phase 9 — Newly-scoped future work (open, 2026-08-04)

| ID | Task | Status | Notes |
|---|---|---|---|
| SOKOL-49 | Port `PbrEffect`/`SkinnedPbrEffect` to Sokol | ⬜ | Discovered while closing `SOKOL-22`: this plan's own prior claim that "no CNA backend has PBR yet" was stale -- `PbrEffect`/`SkinnedPbrEffect` are real, implemented stock effects on every OTHER CNA backend (EasyGL, D3D9/11/12, Vulkan, WebGPU, Bgfx, SdlGpu; ~1000+ lines across shaders and the XNA layer in Vulkan's own implementation alone). Sokol is the only backend still missing it. A genuine, sizeable feature -- a new `pbr3d.glsl` shader variant (metallic/roughness/normal maps, `VertexPositionNormalTangentTextureSkinned`'s Tangent/Binormal inputs, a new `Shader3DKind`), comparable in scope to `SkinnedEffect`/`EnvironmentMapEffect` (`SOKOL-34`/`SOKOL-35`) -- not attempted as part of `SOKOL-22`'s own "honour vertex declaration elements" scope. Deliberately deferred pending explicit scoping. |

---

## Post-audit integration (2026-08-06)

This backend was integrated into `integration/post-audit-phase1` as the campaign's twelfth lane and
the **thirtieth** public CNA backend identity. Original head `261ea700`, preserved unchanged behind
the signed archive tag `archive/preintegration/sokol-20260804`; adapted on `adapt/sokol` and merged
with `--no-ff`. Full record: `integration/lanes/sokol.md` on the planning branch.

**Interface drift was two references.** The lane was branched before REMED-GFX-201/202 replaced
`GpuDrawParams::instanceVb` (and the three fields beside it) with `vertexStreams`, one array
carrying every active `VertexBufferBinding` for every draw route. A compile probe of the backend
against the current head reported exactly those two errors and nothing else. The instanced route now
reads `FirstInstanceStream(params)` and, unlike the shape it replaced, honours the binding's own
`InstanceFrequency` (through `sg_vertex_buffer_layout_state.step_rate`, a `glVertexAttribDivisor` on
the GL backends) and its own `VertexOffset`.

**Three post-audit obligations were paid at adaptation.**

- `GraphicsCapability::MultiStreamVertexInput` answers **false** and
  `GraphicsCapability::Instancing` answers **true**, in an exhaustive eleven-member switch with no
  `default` arm. Both draw routes call `RejectUnsupportedStreamCombination()`, so a declaration
  split across several buffers -- or a second per-instance stream -- is refused before any pipeline
  is built rather than rendered from a subset of the bound streams.
- REMED-GFX-DECL-GUARD: `RequireFaithfulDeclarationEXT()`, header-only, called at draw time on both
  ordinary routes and the instanced route. It deliberately does **not** reuse the shared
  stride-inferring helper, which models a different mechanism -- this backend programs
  `sg_pipeline_desc::layout` from the declaration's own offsets and formats, so applying the
  stride-table rule would refuse correct draws. It refuses a declared stride the buffer was not
  uploaded with, an element outside its record, two elements claiming the same bytes, and a second
  usage-index set of a semantic the pipeline binds. A semantic no stock shader reads (Tangent,
  Binormal, Fog, ...) is explicitly not refused.
- REMED-GFX-209: `GraphicsCapability::WireFrame` corrected from `false` to **true**, on measurement.
  `CNA_BACKEND_SOKOL` joined `WireFrameTriangleOracle.hpp`'s pixel set and the shared
  asymmetric-triangle fixture read `interior 0/1089` under `WireFrame` against `1089/1089` under
  `Solid`, with all three disjoint edge probes lit at 25 px each, no stale state across alternating
  draws, and an exact solid recovery afterwards. `SOKOL-23`'s row above is corrected accordingly.

**Registration union.** The ninth of the campaign: `BackendSelection.cmake`, `BackendLibraries`,
`CnaLibrary`, `CMakeLists`, `GraphicsBackendType.hpp` (enum + `#elif` + name table),
`GraphicsBackendTypeTests.cpp`'s `ExpectedNameFor()` arm, the compile-definition count,
`GraphicsDevice::getBackendWindowFlags()`, README and `THIRD_PARTY_NOTICES.md`. Every pre-existing
identity was kept token-exact.

**Dependency, unchanged.** sokol pinned at `27b49604b19be8cee0dcc6b2bbfe803dd9517585` (zlib/libpng,
Andre Weissflog), fetched at configure time and never vendored; a shared `~/deps/sokol` checkout at
that exact commit serves offline builds through CMake's own `FETCHCONTENT_SOURCE_DIR_SOKOL`. No
carried patch was needed. `CNA_SOKOL_API=GLCORE` remains the only implemented value and is the
default, so backend selection is deterministic.

**Validated on Mesa 25.0.7 llvmpipe (LLVM 19.1.7), a real GL 4.5 core context on Xvfb.**

| Gate | Result |
|---|---|
| Pre-adaptation baseline, built from the original head at its own fork point | `Sokol_Smoke` 13/13 · `Sokol_2D` 15/15 · `Sokol_3D` 10/10 · `Sokol_Lit3D` 10/10 — 48 checks, 0 failures |
| Dedicated suites, adapted | **37/37** (`ctest -R "^Sokol"`) |
| Full corpus under `CNA_GRAPHICS_BACKEND=SOKOL` | 5776 registered · **5768 passed · 1 failed · 7 truthful skips · 0 aborts** (697 s). The one failure is the pre-existing networking Outcome-C flake, 3/3 green re-run in isolation |
| Multi-stream refusal | `InstancedDrawMultiStreamTest` + `OrdinaryDrawMultiStreamTest` **8/8**, both explicit deterministic-rejection cases included |
| ASan + UBSan over all 37 suites | **0 ASan errors, 0 UBSan runtime errors**; every leak rooted in `libGLX_mesa` with no CNA frame (240 732 B / 1073 allocs, identical in all 37), `detect_leaks=0` control **37/37** |

One CNA-frame leak was found and fixed by that sanitizer run: this backend's
`sokol_blendfactor_pipeline_cache_test.cpp` was the only harness in `examples/` that raw-`new`ed its
`GraphicsDeviceManager` instead of owning it in a `unique_ptr`, as every sibling — including this
backend's own `sokol_2d_test` — already does.

## Feature gap vs. EasyGL

SOKOL is not at EasyGL parity. This is the complete list of what EasyGL supports today that SOKOL
does not, as of the state above:

| Feature | EasyGL | SOKOL | Why |
|---|---|---|---|
| Custom `Effect` via `SpriteBatch.Begin(effect)` / arbitrary `ShaderEffect` | ✅ real GLSL compilation at runtime | ✅ | Closed by `SOKOL-28`. |
| `RasterizerState.FillMode = WireFrame` | ✅ CPU-side triangle-to-`GL_LINES` re-expansion at draw time | ✅ | Closed by `SOKOL-23`: the same CPU-side re-expansion technique, ported to sokol_gfx's immutable-pipeline model. |
| Vertex elements other than Position/Color/TextureCoordinate/Normal at usage index 0 (BlendWeight/BlendIndices for skinning, Tangent/Binormal for normal mapping/PBR) | ✅ (skinned, normal-mapped, PBR stock effect shaders) | Position/Color/TextureCoordinate/Normal/BlendWeight/BlendIndices ✅; Tangent/Binormal still ignored | **`SOKOL-22` closed 2026-08-04** for its actually-assigned scope. Tangent/Binormal only have meaning for `PbrEffect`/`SkinnedPbrEffect`, which is real on every OTHER CNA backend but not yet ported to Sokol -- tracked as `SOKOL-49`, not folded back into this task. |
| `DualTextureEffect` | ✅ | ✅ | Closed by `SOKOL-33`. |
| `EnvironmentMapEffect` (reflection cube mapping) | ✅ | ✅ | Closed by `SOKOL-34`. |
| `PbrEffect`/`SkinnedPbrEffect` (PBR 3D shading) | ✅ | ⬜ throws, naming the unsupported combination | Real on EasyGL/D3D9/D3D11/D3D12/Vulkan/WebGPU/Bgfx/SdlGpu -- Sokol is the only backend missing it. Not a permanent gap, just not yet ported; tracked as `SOKOL-49`. |
| `SkinnedEffect` (skinned `BasicEffect`) | ✅ | ✅ | Closed by `SOKOL-35`. |
| Instanced draws | ✅ | ✅ | Closed by `SOKOL-36`. |
| MRT (`SetRenderTargets` with more than one binding) | ✅ real multiple colour attachments | ✅ | Closed by `SOKOL-26`: a real multi-attachment `sg_pass`, 2-4 `RenderTarget2D` targets, both a custom-effect and every stock pipeline. |
| `RenderTarget2D`/`RenderTargetCube` mip-mapped (`mipMap=true`) | ✅ | ✅ | Closed by `SOKOL-26`/`SOKOL-39`. |
| `RenderTarget2D`/`RenderTargetCube` direct CPU readback (`GetData()`) | ✅ | ✅ | Closed by `SOKOL-38`, which also fixed two real pre-existing bugs it surfaced -- see its own row. |
| `RenderTargetCube` MSAA | ✅ six real multisample renderbuffers, one per face | ⬜ `multiSampleCount` silently clamped to 1 | **Permanent, not "not implemented yet"**: sokol_gfx's own validation layer hard-rejects any `SG_IMAGETYPE_CUBE` image with `sample_count > 1` (`VALIDATE_IMAGEDESC_ATTACHMENT_MSAA_CUBE_IMAGE`), confirmed empirically via a real `[sg][panic]` abort while prototyping the same layout `RenderTarget2D` MSAA uses successfully. `SOKOL-26`, closed as a permanent gap. |
| `BlendState.MultiSampleMask` | ✅ | ⬜ ignored | **No upstream API**: sokol_gfx exposes alpha-to-coverage only, no per-sample coverage mask. |
| `Viewport.MinDepth`/`MaxDepth` | ✅ | ✅ | Closed by `SOKOL-37`. |
| Cheaper `VertexBuffer`/`IndexBuffer` re-upload than recreate-per-`SetData` | ✅ | ✅ | Closed by `SOKOL-24`: a `dynamic_update` buffer reused via `sg_update_buffer()` across same-shape uploads, recreated only when data outgrows it or a second upload lands in the same frame. |
| Non-GL native context (`D3D11`/Metal/WebGPU dispatch inside sokol_gfx itself) | N/A (EasyGL is GL-only by design) | ⬜ configure warns, construction throws for any `CNA_SOKOL_API` other than `GLCORE` | `SOKOL-31`. |
| Verified on real discrete-GPU hardware | ✅ | ⬜ only verified under Mesa llvmpipe (Xvfb) so far | `SOKOL-30` — infeasible in this sandbox, no discrete GPU available. |

Everything **not** in this table (2D SpriteBatch, BasicEffect textured/lit 3D, depth/stencil,
culling, depth bias, RenderTarget2D incl. MSAA+resolve, RenderTargetCube, TextureCube, Texture3D
storage, occlusion queries) is already at parity with EasyGL — see the "What works" table in
[`docs/sokol-backend.md`](../docs/sokol-backend.md) for the verification evidence behind each one.

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
