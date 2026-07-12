# WebGPU Backend Implementation Plan

> WebGPU is an authorized, experimental workstream. Its first goal is a **verified native 2D
> backend on Linux desktop**, not feature parity with Vulkan, Android support, or browser WebGPU.
> Do not begin another 3D/effect/render-target feature until the active 2D validation gate below
> is green.
>
> **Status legend:** ✅ implemented *and verified against its stated acceptance criteria*;
> 🟨 code or documentation exists but has not met those criteria; ⬜ not implemented.

> **History:** this content originally lived inline in `plan_graphics.md` as Phases 56–69,
> numbered `501`–`661`, then renumbered to `10001`+ (2026-07-02) to free up the low task-ID
> range for further core-plan growth. Moved to this dedicated file and renumbered again to
> `WEBGPU-1`–`WEBGPU-123` (2026-07-07) to fully separate the parked WebGPU work from the active
> `plan_graphics.md` backlog — no task content changed in either move, only the numbering and
> file location.
>
> **Verified starting point (2026-07-12):** CMake configuration succeeds on Linux x86_64 with an
> explicit `CNA_WEBGPU_ROOT`, but `WebGPUGraphicsBackend.cpp` does not compile against the pinned
> `wgpu-native v29.0.1.1` headers: it references non-existent
> `WGPUSurfaceGetCurrentTextureStatus_OutOfMemory` and `...DeviceLost` values. No WebGPU binary,
> runtime frame, shader compilation, or automated test has passed yet. All existing “implemented”
> code is therefore unverified.
>
> **Platform scope until expanded by a completed task:** Linux desktop (X11 and Wayland), x86_64,
> using an explicit extracted `wgpu-native` package. Windows and macOS are code paths only, not
> validation claims. Android is blocked by the absence of an Android package/build route, and
> browser/Emscripten WebGPU is a separate future workstream.

## Active execution order — do this one task at a time

1. `WEBGPU-124` — make the pinned native backend compile without compatibility defines.
2. `WEBGPU-125` — link and run a minimal clear/present loop on a real Linux desktop GPU.
3. `WEBGPU-126` — verify the complete existing SpriteBatch 2D slice visually and under validation.
4. `WEBGPU-127` — harden lifecycle and recovery paths discovered by the first run.
5. `WEBGPU-128` — make the verified Linux package/runtime deployment reproducible.
6. `WEBGPU-129` — add a maintained automated native 2D smoke-test harness.
7. `WEBGPU-130` — make `../mobile-eggbert` a repeatable desktop integration test.
8. `WEBGPU-131` — record the 2D baseline and open the next tranche.
9. Only then schedule readback/pixel tests (`WEBGPU-51`, `WEBGPU-55`, `WEBGPU-88`–`WEBGPU-92`)
   and the 3D backlog (Phases 57–66).

For every task: use a clean build directory, pass `CNA_WEBGPU_ROOT` and
`CNA_WEBGPU_AUTO_DOWNLOAD=OFF`, record the exact command and result in the task note, and do not
mark it ✅ from source inspection alone.

---

## Phase 56 — WebGPU backend: infrastructure and CMake setup

> The native implementation is pinned to **wgpu-native v29.0.1.1** and uses WGSL. `CNA_WEBGPU_ROOT`
> is the supported reproducible input; automatic download is convenience-only until it has a
> checksum and CI/runtime coverage. Push constants are unavailable in WebGPU, so future 3D work
> must use uniform buffers.

| #   | Task                                                                                                          | Status | Notes                                                                 |
| --- | ------------------------------------------------------------------------------------------------------------- | ------ | --------------------------------------------------------------------- |
| WEBGPU-1 | Add `CNA_GRAPHICS_BACKEND=WEBGPU` CMake option; locate headers + libs; define `CNA_BACKEND_WEBGPU` | 🟨 | Selector and imported target exist; Linux configure with `CNA_WEBGPU_ROOT` works, but compilation, final executable linkage/runtime loading, package integrity and non-Linux paths are unverified. `vendor/wgpu-native` is not a portable vendored dependency. |
| WEBGPU-2 | Create `include/CNA/Internal/Backends/WebGPU/WebGPUGraphicsBackend.hpp` — class skeleton, all IGraphicsBackend sub-interfaces declared | 🟨 | Core backend, Texture2D, vertex/index buffers and SpriteBatch classes exist; remaining render-target/cube/3D/effect/query classes are still open. |
| WEBGPU-3 | Create `src/CNA/Internal/Backends/WebGPU/WebGPUGraphicsBackend.cpp` — initial functional baseline and explicit unsupported 3D paths | 🟨 | Functional code exists, but it currently fails the pinned-header compilation gate. |
| WEBGPU-4 | SDL3 surface creation | 🟨 | Win32, Metal, X11 and Wayland branches exist. Only Linux is in current scope; Android branch is not a supported build route. |
| WEBGPU-5 | Instance, adapter, device and queue initialization | 🟨 | Code exists; runtime callback behaviour, error handling and timeout behaviour require `WEBGPU-125`. |
| WEBGPU-6 | Surface configuration, resize and backbuffer acquisition | 🟨 | Code exists; uncompiled/unrun. |
| WEBGPU-7 | Command encoding and queue submission per frame | 🟨 | Code exists; uncompiled/unrun. |
| WEBGPU-8 | Main render pass with colour/depth/stencil attachments | 🟨 | Code exists; uncompiled/unrun. |
| WEBGPU-9 | Colour/depth/stencil clear state | 🟨 | Code exists; must be exercised by the first runtime frame. |
| WEBGPU-10 | Present and recoverable acquisition handling | 🟨 | Code exists; the status handling is the current compilation blocker. |

---

## Phase 56.1 — Recovery and verified native 2D vertical slice (active)

| # | Task | Status | Acceptance criteria |
| --- | --- | --- | --- |
| WEBGPU-124 | Align the backend with the pinned `wgpu-native v29.0.1.1` C API, beginning with surface-acquisition status handling. | ⬜ **NEXT** | A fresh Linux x86_64 CMake build with explicit `CNA_WEBGPU_ROOT` compiles and links `cna_backend_graphics_webgpu` with no compatibility `-D` aliases or source-specific compiler workaround. |
| WEBGPU-125 | Build and run a minimal native window that initializes the backend, clears, presents at least 60 frames, then exits cleanly. | ⬜ | Run on a real Linux X11 or Wayland desktop; no hang in adapter/device request, no uncaptured WebGPU error, no device-loss report and no dynamic-loader failure. |
| WEBGPU-126 | Validate the existing 2D slice: texture upload, source rectangles, tint/alpha, rotation, flip, linear/point sampling, wrap/clamp/mirror, logical presentation and resize. | ⬜ | A reproducible manual checklist passes on the runtime from `WEBGPU-125`; compare screenshots with a known-good established backend where practical. |
| WEBGPU-127 | Harden lifecycle and failure paths discovered in the first run: request timeout/polling strategy, surface loss/outdated recovery, zero-size windows and destruction order. | ⬜ | Targeted regression checks cover every repaired branch and the application exits without leaks/errors under validation layers where available. |
| WEBGPU-128 | Make package discovery and runtime deployment reproducible for the verified Linux target. | ⬜ | Document one offline package layout and command; validate final executable runtime discovery. Defer auto-download checksum work and all unvalidated platform packages rather than claiming support. |
| WEBGPU-129 | Add a backend-specific native smoke-test target and CTest registration that can run only when a display/GPU is available. | ⬜ | Fresh configure/build registers the test; it skips clearly when no desktop GPU/display is available and otherwise executes `WEBGPU-125` automatically. |
| WEBGPU-130 | Integrate `../mobile-eggbert` as the first real desktop 2D application smoke test. | ⬜ | Its CMake can select `WEBGPU` without force-overriding it to Vulkan, recognizes `cna_backend_graphics_webgpu` in the linker group, and the game reaches menu and gameplay. No Android change is implied. |
| WEBGPU-131 | Establish the native 2D test baseline before 3D work. | ⬜ | `WEBGPU-124`–`WEBGPU-130` are complete; manual test evidence and known limitations are written in `docs/webgpu-backend.md`. |

---

## Phase 57 — WebGPU backend: uniform buffer system (replaces push constants)

| #   | Task                                                                                                          | Status | Notes                                                                 |
| --- | ------------------------------------------------------------------------------------------------------------- | ------ | --------------------------------------------------------------------- |
| WEBGPU-11 | Design `GpuUniforms` struct (128 bytes = 32 floats) matching Vulkan push constant layout; upload via `wgpuQueueWriteBuffer` | ⬜ | Central UBO for MVP + effect params |
| WEBGPU-12 | Create `WGPUBuffer` (uniform, size=128) per frame (or ring buffer of 3); map on CPU side via `wgpuBufferGetMappedRange` | ⬜ | |
| WEBGPU-13 | `WGPUBindGroupLayout` for slot 0 binding 0 (uniform buffer) — shared across all 3D pipelines | ⬜ | |
| WEBGPU-14 | `WGPUBindGroup` creation and per-draw update for MVP matrix | ⬜ | |
| WEBGPU-15 | `WGPUBindGroupLayout` for slot 1 binding 0 (texture sampler) — for textured pipelines | ⬜ | |
| WEBGPU-16 | `WGPUSampler` creation mapping `SamplerState` (filter, address mode) → WGPU descriptor | 🟨 | SpriteBatch sampler cache maps linear/point and wrap/clamp/mirror; full per-slot 3D SamplerState mapping remains open. |
| WEBGPU-17 | `WGPUPipelineLayout` combining UBO bind group layout + texture bind group layout | ⬜ | |

---

## Phase 58 — WebGPU backend: WGSL shaders

| #   | Task                                                                                                          | Status | Notes                                                                 |
| --- | ------------------------------------------------------------------------------------------------------------- | ------ | --------------------------------------------------------------------- |
| WEBGPU-18 | Write `sprite2d.wgsl` — 2D sprite vertex + fragment shader (pos + UV + RGBA tint); embed as C++ string literal | 🟨 | Embedded WGSL exists, but neither backend compilation nor runtime shader validation has passed. `WEBGPU-126` owns its acceptance. |
| WEBGPU-19 | Write `colored3d.wgsl` — 3D vertex shader (float3 pos + ubyte4 color), flat fragment; UBO for MVP | ⬜ | stride=16 |
| WEBGPU-20 | Write `textured3d.wgsl` — 3D vertex (float3 pos + float2 UV); texture2D sampler in fragment | ⬜ | stride=20 |
| WEBGPU-21 | Write `colored_textured3d.wgsl` — float3 + ubyte4 color + float2 UV; multiply tex×color in fragment | ⬜ | stride=24 |
| WEBGPU-22 | Write `lit_textured3d.wgsl` — float3 pos + float3 normal + float2 UV; Blinn-Phong lighting in fragment | ⬜ | stride=32 |
| WEBGPU-23 | Write `alpha_test3d.wgsl` — per-pixel alpha discard matching XNA AlphaTestEffect semantics | ⬜ | |
| WEBGPU-24 | Write `dual_texture3d.wgsl` — two texture samplers, multiply/blend in fragment | ⬜ | |
| WEBGPU-25 | Write `env_map3d.wgsl` — cube map sampler + reflection vector from normal | ⬜ | |
| WEBGPU-26 | Write `skinned3d.wgsl` — bone palette as uniform array (max 72 mat4); blend 4 weights+indices | ⬜ | |
| WEBGPU-27 | Write `instanced3d.wgsl` — per-instance mat4 world transform in second vertex buffer binding | ⬜ | |
| WEBGPU-28 | Compile-time validation: embed all WGSL as `constexpr const char*` in `webgpu_shaders.hpp`; validate via `wgpuDeviceCreateShaderModule` at startup | ⬜ | Catch WGSL errors early |

---

## Phase 59 — WebGPU backend: render pipeline creation

| #   | Task                                                                                                          | Status | Notes                                                                 |
| --- | ------------------------------------------------------------------------------------------------------------- | ------ | --------------------------------------------------------------------- |
| WEBGPU-29 | `WGPURenderPipelineDescriptor` builder helper: vertex state, primitive state, depth-stencil state, multisample state, fragment state | 🟨 | A concrete SpriteBatch descriptor exists but is uncompiled/unrun; reusable all-pipeline builder remains open. |
| WEBGPU-30 | Pipeline cache: `std::unordered_map<uint64_t, WGPURenderPipeline>` with MakeKey(topo, depth, blend, cull, stride, wireframe, msaa) | ⬜ | Mirror Vulkan MakeKey / GetOrCreate* |
| WEBGPU-31 | `GetOrCreatePipeline2D()` — sprite pipeline (stride=24, Sprite2DVertex layout, no depth) | 🟨 | Code for opaque and premultiplied-alpha variants exists; correctness belongs to `WEBGPU-126`. |
| WEBGPU-32 | `GetOrCreatePipelineColored3D()` — stride=16, VPC layout | ⬜ | |
| WEBGPU-33 | `GetOrCreatePipelineExt3D()` — stride 20/24/32 dispatch matching Vulkan | ⬜ | |
| WEBGPU-34 | `GetOrCreatePipelineAlphaTest3D()` — alpha discard variant | ⬜ | |
| WEBGPU-35 | `GetOrCreatePipelineDualTex3D()` — two-texture variant | ⬜ | |
| WEBGPU-36 | `GetOrCreatePipelineEnvMap3D()` — cube map variant | ⬜ | |
| WEBGPU-37 | `GetOrCreatePipelineSkinned3D()` — bone palette variant | ⬜ | |
| WEBGPU-38 | `GetOrCreatePipelineInstanced3D()` — per-instance binding variant | ⬜ | |
| WEBGPU-39 | Depth-stencil: `WGPUDepthStencilState` mapping `DepthFormat` + `CompareFunction` + `StencilOperation` | ⬜ | |
| WEBGPU-40 | Blend state: `WGPUBlendState` mapping `BlendFunction` + `BlendFactor` (Opaque, AlphaBlend, Additive, NonPremultiplied) | 🟨 | Opaque and premultiplied-alpha SpriteBatch pipelines exist; complete XNA BlendState mapping remains open. |
| WEBGPU-41 | Rasterizer: `WGPUPrimitiveState` mapping `CullMode`, `FillMode` (WireFrame via `topology=LineStrip` fallback or unsupported) | ⬜ | |

---

## Phase 60 — WebGPU backend: vertex and index buffers

| #   | Task                                                                                                          | Status | Notes                                                                 |
| --- | ------------------------------------------------------------------------------------------------------------- | ------ | --------------------------------------------------------------------- |
| WEBGPU-42 | `WebGPUVertexBufferBackend`: create `WGPUBuffer` (vertex, size=capacity×stride) with `COPY_DST` usage | 🟨 | Lazily sized VERTEX\|COPY_DST code and capacity validation exist; normal WebGPU compilation has not succeeded. |
| WEBGPU-43 | `SetData()`: upload via `wgpuQueueWriteBuffer(queue, buffer, 0, data, byteSize)` | 🟨 | Upload code exists; no runtime validation yet. |
| WEBGPU-44 | `SetDataWithOptions()`: `Discard` = reallocate buffer; `NoOverwrite` = `wgpuQueueWriteBuffer` at offset | ⬜ | |
| WEBGPU-45 | `WebGPUIndexBufferBackend`: 16-bit and 32-bit index buffers via `WGPUIndexFormat` | 🟨 | Both backend classes exist; draw dispatch and runtime validation remain open. |
| WEBGPU-46 | `SetData16()` / `SetData32()`: `wgpuQueueWriteBuffer` | 🟨 | Both upload paths exist; no normal build/runtime validation yet. |
| WEBGPU-47 | Disposed guard in all SetData methods (throw `ObjectDisposedException`) | ⬜ | Match Task 240 pattern |
| WEBGPU-48 | `SetVertexBuffer(wgpuRenderPassSetVertexBuffer)` + `SetIndexBuffer(wgpuRenderPassSetIndexBuffer)` in draw dispatch | ⬜ | |

---

## Phase 61 — WebGPU backend: textures

| #   | Task                                                                                                          | Status | Notes                                                                 |
| --- | ------------------------------------------------------------------------------------------------------------- | ------ | --------------------------------------------------------------------- |
| WEBGPU-49 | `WebGPUTextureBackend`: `WGPUTexture` (2D, RGBA8Unorm, COPY_DST + TEXTURE_BINDING) + `WGPUTextureView` | 🟨 | RGBA8Unorm Texture2D + view code exists; runtime validation is part of `WEBGPU-126`. |
| WEBGPU-50 | `SetData()`: `wgpuQueueWriteTexture()` with `WGPUImageCopyTexture` + `WGPUTextureDataLayout` | 🟨 | Level-upload code exists; it has not run against a device. |
| WEBGPU-51 | `Texture2D::GetData()`: staged MAP_READ copy with aligned rows and asynchronous map/poll completion | ⬜ | Detailed acceptance and shared implementation are scheduled as `WEBGPU-91`, after the 2D runtime gate. |
| WEBGPU-52 | Mip levels: generate via `wgpuCommandEncoderCopyTextureToTexture` per level or leave as mip=1 (document) | 🟨 | Requested mip count is allocated and explicit level uploads work; automatic mip generation is not implemented. |
| WEBGPU-53 | `WebGPURenderTargetBackend`: `WGPUTexture` (RENDER_ATTACHMENT + TEXTURE_BINDING) + depth texture | ⬜ | |
| WEBGPU-54 | `SetRenderTarget(rt)` / `SetRenderTarget(nullptr)`: switch render pass target between RT and swapchain view | ⬜ | |
| WEBGPU-55 | `GetBackBufferData()`: readback via MAP_READ buffer + `wgpuCommandEncoderCopyTextureToBuffer` | ⬜ | Use the shared `WEBGPU-91` readback path; do not build a duplicate implementation. |
| WEBGPU-56 | `WebGPUTextureCubeBackend`: `WGPUTexture` (dimension=2D, arrayLayerCount=6, CUBE_COMPATIBLE) | ⬜ | |
| WEBGPU-57 | `WebGPUTexture3DBackend`: `WGPUTexture` (dimension=3D) | ⬜ | |
| WEBGPU-58 | MSAA: `WGPUTexture` with `sampleCount=4`; resolve in render pass via `resolveTarget` | ⬜ | |

---

## Phase 62 — WebGPU backend: 2D rendering (SpriteBatch)

| #   | Task                                                                                                          | Status | Notes                                                                 |
| --- | ------------------------------------------------------------------------------------------------------------- | ------ | --------------------------------------------------------------------- |
| WEBGPU-59 | `WebGPUSpriteBatchBackend`: dynamic vertex buffer ring (3 frames) for sprite quads | 🟨 | Growable dynamic SpriteBatch vertex buffer exists; three-frame ring/fencing optimization remains open. |
| WEBGPU-60 | Upload sprite quads via `wgpuQueueWriteBuffer` per batch | 🟨 | Queued sprites are flattened and upload code exists; it has not run against a device. |
| WEBGPU-61 | Per-batch draw: set pipeline, bind groups (UBO + texture), vertex buffer, draw | 🟨 | Pipeline/bind-group/draw code exists; it has not run against a device. |
| WEBGPU-63 | Verify SpriteBatch sort modes: Immediate, Deferred, Texture, FrontToBack, BackToFront. | ⬜ | Queue ordering is primarily shared `SpriteBatch` behaviour; validate the WebGPU submission path in `WEBGPU-126` rather than reimplementing it. |

---

## Phase 63 — WebGPU backend: 3D draw dispatch

| #   | Task                                                                                                          | Status | Notes                                                                 |
| --- | ------------------------------------------------------------------------------------------------------------- | ------ | --------------------------------------------------------------------- |
| WEBGPU-64 | `DrawPrimitives()`: bind colored3d pipeline + UBO + vertex buffer + `wgpuRenderPassEncoderDraw` | ⬜ | |
| WEBGPU-65 | `DrawIndexedPrimitives()`: bind index buffer + `wgpuRenderPassEncoderDrawIndexed` | ⬜ | |
| WEBGPU-66 | `DrawPrimitivesEx()`: dispatch by `GpuDrawParams` (stride, textureEnabled, lightingEnabled, dualTexture, skinned, instanced) | ⬜ | Mirror Vulkan dispatch logic |
| WEBGPU-67 | `DrawUserPrimitives()`: transient `WGPUBuffer` (COPY_DST + VERTEX, mappedAtCreation=false); upload + draw + release | ⬜ | |
| WEBGPU-68 | `DrawInstancedPrimitivesEx()`: second vertex buffer binding (per-instance mat4 world transforms) | ⬜ | |
| WEBGPU-69 | PrimitiveType mapping: TriangleList→`WGPUPrimitiveTopology_TriangleList`, TriangleStrip→Strip, LineList→LineList, LineStrip→LineStrip, PointList→PointList | 🟨 | All PrimitiveType values map to WebGPU topologies, but 3D draw dispatch using them remains open. |
| WEBGPU-70 | `vertexStart` / `startIndex` / `baseVertex` support in draw calls | ⬜ | Match Task 110 |

---

## Phase 64 — WebGPU backend: Effects

| #   | Task                                                                                                          | Status | Notes                                                                 |
| --- | ------------------------------------------------------------------------------------------------------------- | ------ | --------------------------------------------------------------------- |
| WEBGPU-71 | `WebGPUEffectBackend`: `BasicEffect` wires to `FillGpuDrawParams` → UBO upload | ⬜ | |
| WEBGPU-72 | `AlphaTestEffect`: UBO alpha test params (function, reference) | ⬜ | |
| WEBGPU-73 | `DualTextureEffect`: second texture bind group | ⬜ | |
| WEBGPU-74 | `EnvironmentMapEffect`: cube map bind group + reflection UBO params | ⬜ | |
| WEBGPU-75 | `SkinnedEffect`: bone palette as large UBO (72 × mat4 = 4608 bytes) in separate bind group | ⬜ | WebGPU min UBO size: 65536 bytes — fits |
| WEBGPU-76 | `ShaderEffect` (custom WGSL): `wgpuDeviceCreateShaderModule` from user-provided WGSL source string | ⬜ | NOXNA extension |

---

## Phase 65 — WebGPU backend: state management

| #   | Task                                                                                                          | Status | Notes                                                                 |
| --- | ------------------------------------------------------------------------------------------------------------- | ------ | --------------------------------------------------------------------- |
| WEBGPU-77 | `SetDepthTestEnabled()` / `SetDepthWriteEnabled()`: bake into pipeline key | ⬜ | WebGPU requires pipeline rebuild on change |
| WEBGPU-78 | `SetBlendState()`: map `BlendState` preset → `WGPUBlendState` | ⬜ | |
| WEBGPU-79 | `SetRasterizerState()`: `CullMode` → `WGPUCullMode`; `FillMode::WireFrame` unsupported (log warning) | ⬜ | WebGPU has no polygon mode |
| WEBGPU-80 | `SetScissorRectangle()`: `wgpuRenderPassEncoderSetScissorRect` | ⬜ | |
| WEBGPU-81 | `SetViewport()`: `wgpuRenderPassEncoderSetViewport` | ⬜ | |
| WEBGPU-82 | `SetSamplerState()`: per-slot `WGPUSampler` cache (filter + address mode key) | 🟨 | 18-entry SpriteBatch sampler cache implemented; full graphics-device per-slot sampler state remains open. |
| WEBGPU-83 | `SetDepthStencilState()`: stencil ops → `WGPUStencilFaceState` | ⬜ | |
| WEBGPU-84 | `OcclusionQuery`: `WGPUQuerySet` (type=Occlusion) + `wgpuRenderPassEncoderBeginOcclusionQuery` | ⬜ | |

---

## Phase 66 — WebGPU backend: Multiple Render Targets

| #   | Task                                                                                                          | Status | Notes                                                                 |
| --- | ------------------------------------------------------------------------------------------------------------- | ------ | --------------------------------------------------------------------- |
| WEBGPU-85 | MRT render pass: `WGPURenderPassDescriptor` with array of `WGPURenderPassColorAttachment` (up to 4) | ⬜ | |
| WEBGPU-86 | `GetOrCreateMRTRenderPipeline(colorAttachmentCount)`: pipeline with matching `targetCount` in fragment state | ⬜ | |
| WEBGPU-87 | `SetRenderTargets(vector<RenderTarget2D*>)`: configure MRT pass descriptor | ⬜ | |

---

## Phase 67 — WebGPU backend: automated validation after the native 2D gate

| #   | Task                                                                                                          | Status | Notes                                                                 |
| --- | ------------------------------------------------------------------------------------------------------------- | ------ | --------------------------------------------------------------------- |
| WEBGPU-88 | Establish CMake/CTest registration for native WebGPU tests. | ⬜ | Runs only after `WEBGPU-129`; clear skip reason when no display/GPU is available. |
| WEBGPU-89 | No-readback smoke: initialize, clear to a known colour, present 60 frames, release. | ⬜ | Automated equivalent of `WEBGPU-125`; catches initialization, lifecycle and loader regressions. |
| WEBGPU-90 | No-readback SpriteBatch scene: RGBA texture, source rectangle, tint, rotation and sampler variants. | ⬜ | Automated device-error-free run plus captured/manual comparison while readback is unavailable. |
| WEBGPU-91 | Implement reusable GPU readback with correct row alignment, asynchronous map/poll completion and timeout/error propagation. | ⬜ | Prerequisite for pixel assertions; consolidates the old `WEBGPU-51`/`WEBGPU-55` readback intent. |
| WEBGPU-92 | Pixel-asserted 2D tests: clear and white 1×1 sprite, then alpha/source-rectangle/sampler cases. | ⬜ | Requires `WEBGPU-91`; this is the first deterministic 2D correctness gate. |
| WEBGPU-93 | 3D coloured-quad pixel test (stride 16). | ⬜ | Schedule only after `WEBGPU-19`, `WEBGPU-32`, `WEBGPU-64` and `WEBGPU-77`–`WEBGPU-83`. |
| WEBGPU-94 | 3D textured-quad pixel test (stride 20). | ⬜ | Schedule only after the matching pipeline and draw dispatch exist. |
| WEBGPU-95 | 3D coloured+textured pixel test (stride 24). | ⬜ | Schedule only after the matching pipeline and draw dispatch exist. |
| WEBGPU-96 | Lit-textured, alpha-test and dual-texture effect tests. | ⬜ | Split into independently runnable cases when those effects land. |
| WEBGPU-97 | Environment-map and skinned-effect tests. | ⬜ | Split into independently runnable cases when those effects land. |
| WEBGPU-98 | Instancing, render-target, MSAA and occlusion-query tests. | ⬜ | Split by feature; do not add placeholder tests before implementation. |
| WEBGPU-99 | Buffer disposal, `SetDataOptions` and vertex-format mapping tests. | ⬜ | Schedule with `WEBGPU-44`, `WEBGPU-47` and `WEBGPU-116`. |

---

## Phase 68 — WebGPU backend: advanced and parity

| #   | Task                                                                                                          | Status | Notes                                                                 |
| --- | ------------------------------------------------------------------------------------------------------------- | ------ | --------------------------------------------------------------------- |
| WEBGPU-106 | `SetStringMarkerEXT()`: no-op (WebGPU has no debug labels in wgpu-native C API yet) | ⬜ | Document deviation |
| WEBGPU-107 | `DebugSimulateContextLoss()`: destroy and recreate device (wgpu-native supports `wgpuDeviceDestroy`) | ⬜ | |
| WEBGPU-108 | `PresentationInterval` → vsync: `wgpuSurfaceConfigure.presentMode` (Fifo=VSync, Immediate=no VSync, Mailbox=adaptive) | 🟨 | Selection code exists but has no runtime validation. |
| WEBGPU-109 | `IsFullScreen` via `SDL_SetWindowFullscreen` — same as other backends | ⬜ | |
| WEBGPU-110 | `BackBufferWidth/Height` changes: reconfigure swap chain via `wgpuSurfaceConfigure` | 🟨 | Reconfiguration code exists but has no runtime validation. |
| WEBGPU-111 | DXT1/DXT3/DXT5 compressed texture upload: `WGPUTextureFormat_BC1RGBAUnorm` etc. | ⬜ | Requires `wgpuAdapterHasFeature(BC_texture_compression)` |
| WEBGPU-112 | Texture3D: `WGPUTextureDimension_3D` + layered upload | ⬜ | |
| WEBGPU-113 | TextureCube: `WGPUTexture` arrayLayerCount=6 + `WGPUTextureViewDimension_Cube` | ⬜ | |
| WEBGPU-114 | RenderTargetCube: `WGPUTexture` cube + per-face `WGPUTextureView` as render attachment | ⬜ | |
| WEBGPU-115 | `FillMode::WireFrame`: document as unsupported in WebGPU (no polygon mode); add to deviations doc | ⬜ | |
| WEBGPU-116 | WebGPU vertex format helper: `WGPUVertexFormat WebGPUVertexFormatFromVEF(VertexElementFormat)` (mirror Task 248) | ⬜ | |

---

## Phase 69 — WebGPU: documentation and future (Emscripten/WASM)

| #   | Task                                                                                                          | Status | Notes                                                                 |
| --- | ------------------------------------------------------------------------------------------------------------- | ------ | --------------------------------------------------------------------- |
| WEBGPU-117 | `docs/webgpu-backend.md`: architecture, deviations from Vulkan, WGSL shader map, UBO layout | 🟨 | Document exists but must be revised after `WEBGPU-124`–`WEBGPU-131` with only verified claims and exact test commands. |
| WEBGPU-118 | `docs/webgpu-vs-vulkan-deviations.md`: push constants → UBO, no wireframe, async→sync strategy | ⬜ | |
| WEBGPU-119 | Emscripten target: configure CNA for `emcc` build with `-sUSE_WEBGPU=1`; WebGPU backend routes to browser `navigator.gpu` | ⬜ | True browser WASM target |
| WEBGPU-120 | Emscripten: SDL3 Emscripten port + WebGPU surface via `emscripten_webgpu_get_device()` | ⬜ | |
| WEBGPU-121 | Emscripten: verify all 9 WGSL shader pairs compile in browser via `createShaderModule` | ⬜ | |
| WEBGPU-122 | Emscripten: run 2D smoke test in headless Chrome via `--headless=new --enable-features=WebGPU` | ⬜ | CI-friendly |
| WEBGPU-123 | Cross-backend pixel comparison: same scene rendered on EasyGL/Vulkan/Bgfx/WebGPU — assert pixel-level parity | ⬜ | |
