# WebGPU Backend Implementation Plan

> ⛔ **WEBGPU IS FORBIDDEN FOR NOW.** Do not work on any item in this file — do not start,
> stub, scaffold, or plan implementation — until the project owner explicitly lifts this
> restriction. This is a hard prohibition, not just a priority/ordering note; see `CLAUDE.md`
> ("WebGPU Is Forbidden For Now"), which is authoritative and takes precedence over this file's
> own notes if the two ever seem to disagree. When working through `plan_graphics.md`
> autonomously, this entire file (and everything in it) is out of scope.

> **History:** this content originally lived inline in `plan_graphics.md` as Phases 56–69,
> numbered `501`–`661`, then renumbered to `10001`+ (2026-07-02) to free up the low task-ID
> range for further core-plan growth. Moved to this dedicated file and renumbered again to
> `WEBGPU-1`–`WEBGPU-123` (2026-07-07) to fully separate the parked WebGPU work from the active
> `plan_graphics.md` backlog — no task content changed in either move, only the numbering and
> file location.
>
> **Status:** parked. Revisit only after `plan_graphics.md`'s Phases 1–73 (the four active
> backends: SDL_Renderer, EasyGL, Vulkan, Bgfx) are done — see `plan_graphics.md`'s own
> "Execution order" section for the authoritative priority sequence.

---

## Phase 56 — WebGPU backend: infrastructure and CMake setup

> WebGPU backend uses **wgpu-native v29** (C API header `webgpu.h` + `wgpu.h`).
> Installed at `vendor/wgpu-native/`. Shaders are written in **WGSL** (not SPIR-V).
> Push constants do not exist in WebGPU — replaced by uniform buffers (bind group 0, binding 0).
> Backend selection: `-DCNA_GRAPHICS_BACKEND=WEBGPU`, build dir `cmake-build-webgpu`.
>
> Strategy: mirror the Vulkan backend structure, adapt to WebGPU API differences.
> Estimated total effort: ~4–6 weeks (Tasks 501–750).

| #   | Task                                                                                                          | Status | Notes                                                                 |
| --- | ------------------------------------------------------------------------------------------------------------- | ------ | --------------------------------------------------------------------- |
| WEBGPU-1 | Add `CNA_GRAPHICS_BACKEND=WEBGPU` CMake option; find `vendor/wgpu-native` headers + libs; define `CNA_BACKEND_WEBGPU` | ⬜ | Mirror VULKAN block in CMakeLists.txt |
| WEBGPU-2 | Create `include/CNA/Internal/Backends/WebGPU/WebGPUGraphicsBackend.hpp` — class skeleton, all IGraphicsBackend sub-interfaces declared | ⬜ | ~12 nested backend classes |
| WEBGPU-3 | Create `src/CNA/Internal/Backends/WebGPU/WebGPUGraphicsBackend.cpp` — stub all methods (throw not-implemented) | ⬜ | Compiles clean, no functionality yet |
| WEBGPU-4 | SDL3 surface creation: obtain `WGPUSurface` via `SDL_GetProperty(SDL_PROP_WINDOW_WGPU_SURFACE_POINTER)` or `wgpuInstanceCreateSurface` | ⬜ | Prerequisite for all rendering |
| WEBGPU-5 | `WGPUInstance` + `WGPUAdapter` + `WGPUDevice` + `WGPUQueue` initialization via `wgpuCreateInstance` / `wgpuInstanceRequestAdapter` / `wgpuAdapterRequestDevice` | ⬜ | All synchronous in wgpu-native |
| WEBGPU-6 | Swap chain: `WGPUSurface` configure + `wgpuSurfaceGetCurrentTexture` + `wgpuTextureCreateView` for backbuffer | ⬜ | Replaces `vkAcquireNextImageKHR` |
| WEBGPU-7 | Command encoder: `wgpuDeviceCreateCommandEncoder` + `wgpuCommandEncoderFinish` + `wgpuQueueSubmit` per frame | ⬜ | Replaces Vulkan command buffer recording |
| WEBGPU-8 | Render pass: `wgpuCommandEncoderBeginRenderPass` with color attachment (backbuffer view) + depth attachment | ⬜ | Equivalent to `vkCmdBeginRenderPass` |
| WEBGPU-9 | `Clear()`: set clear color in `WGPURenderPassColorAttachment.clearValue`; implement depth clear in pass descriptor | ⬜ | |
| WEBGPU-10 | `Present()`: `wgpuSurfacePresent()` after queue submit | ⬜ | |

---

## Phase 57 — WebGPU backend: uniform buffer system (replaces push constants)

| #   | Task                                                                                                          | Status | Notes                                                                 |
| --- | ------------------------------------------------------------------------------------------------------------- | ------ | --------------------------------------------------------------------- |
| WEBGPU-11 | Design `GpuUniforms` struct (128 bytes = 32 floats) matching Vulkan push constant layout; upload via `wgpuQueueWriteBuffer` | ⬜ | Central UBO for MVP + effect params |
| WEBGPU-12 | Create `WGPUBuffer` (uniform, size=128) per frame (or ring buffer of 3); map on CPU side via `wgpuBufferGetMappedRange` | ⬜ | |
| WEBGPU-13 | `WGPUBindGroupLayout` for slot 0 binding 0 (uniform buffer) — shared across all 3D pipelines | ⬜ | |
| WEBGPU-14 | `WGPUBindGroup` creation and per-draw update for MVP matrix | ⬜ | |
| WEBGPU-15 | `WGPUBindGroupLayout` for slot 1 binding 0 (texture sampler) — for textured pipelines | ⬜ | |
| WEBGPU-16 | `WGPUSampler` creation mapping `SamplerState` (filter, address mode) → WGPU descriptor | ⬜ | |
| WEBGPU-17 | `WGPUPipelineLayout` combining UBO bind group layout + texture bind group layout | ⬜ | |

---

## Phase 58 — WebGPU backend: WGSL shaders

| #   | Task                                                                                                          | Status | Notes                                                                 |
| --- | ------------------------------------------------------------------------------------------------------------- | ------ | --------------------------------------------------------------------- |
| WEBGPU-18 | Write `sprite2d.wgsl` — 2D sprite vertex + fragment shader (pos + UV + RGBA tint); embed as C++ string literal | ⬜ | Equivalent to `sprite2d.vert/frag.glsl` |
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
| WEBGPU-29 | `WGPURenderPipelineDescriptor` builder helper: vertex state, primitive state, depth-stencil state, multisample state, fragment state | ⬜ | Reusable for all pipelines |
| WEBGPU-30 | Pipeline cache: `std::unordered_map<uint64_t, WGPURenderPipeline>` with MakeKey(topo, depth, blend, cull, stride, wireframe, msaa) | ⬜ | Mirror Vulkan MakeKey / GetOrCreate* |
| WEBGPU-31 | `GetOrCreatePipeline2D()` — sprite pipeline (stride=24, Sprite2DVertex layout, no depth) | ⬜ | |
| WEBGPU-32 | `GetOrCreatePipelineColored3D()` — stride=16, VPC layout | ⬜ | |
| WEBGPU-33 | `GetOrCreatePipelineExt3D()` — stride 20/24/32 dispatch matching Vulkan | ⬜ | |
| WEBGPU-34 | `GetOrCreatePipelineAlphaTest3D()` — alpha discard variant | ⬜ | |
| WEBGPU-35 | `GetOrCreatePipelineDualTex3D()` — two-texture variant | ⬜ | |
| WEBGPU-36 | `GetOrCreatePipelineEnvMap3D()` — cube map variant | ⬜ | |
| WEBGPU-37 | `GetOrCreatePipelineSkinned3D()` — bone palette variant | ⬜ | |
| WEBGPU-38 | `GetOrCreatePipelineInstanced3D()` — per-instance binding variant | ⬜ | |
| WEBGPU-39 | Depth-stencil: `WGPUDepthStencilState` mapping `DepthFormat` + `CompareFunction` + `StencilOperation` | ⬜ | |
| WEBGPU-40 | Blend state: `WGPUBlendState` mapping `BlendFunction` + `BlendFactor` (Opaque, AlphaBlend, Additive, NonPremultiplied) | ⬜ | |
| WEBGPU-41 | Rasterizer: `WGPUPrimitiveState` mapping `CullMode`, `FillMode` (WireFrame via `topology=LineStrip` fallback or unsupported) | ⬜ | |

---

## Phase 60 — WebGPU backend: vertex and index buffers

| #   | Task                                                                                                          | Status | Notes                                                                 |
| --- | ------------------------------------------------------------------------------------------------------------- | ------ | --------------------------------------------------------------------- |
| WEBGPU-42 | `WebGPUVertexBufferBackend`: create `WGPUBuffer` (vertex, size=capacity×stride) with `COPY_DST` usage | ⬜ | |
| WEBGPU-43 | `SetData()`: upload via `wgpuQueueWriteBuffer(queue, buffer, 0, data, byteSize)` | ⬜ | Simpler than Vulkan staging |
| WEBGPU-44 | `SetDataWithOptions()`: `Discard` = reallocate buffer; `NoOverwrite` = `wgpuQueueWriteBuffer` at offset | ⬜ | |
| WEBGPU-45 | `WebGPUIndexBufferBackend`: 16-bit and 32-bit index buffers via `WGPUIndexFormat` | ⬜ | |
| WEBGPU-46 | `SetData16()` / `SetData32()`: `wgpuQueueWriteBuffer` | ⬜ | |
| WEBGPU-47 | Disposed guard in all SetData methods (throw `ObjectDisposedException`) | ⬜ | Match Task 240 pattern |
| WEBGPU-48 | `SetVertexBuffer(wgpuRenderPassSetVertexBuffer)` + `SetIndexBuffer(wgpuRenderPassSetIndexBuffer)` in draw dispatch | ⬜ | |

---

## Phase 61 — WebGPU backend: textures

| #   | Task                                                                                                          | Status | Notes                                                                 |
| --- | ------------------------------------------------------------------------------------------------------------- | ------ | --------------------------------------------------------------------- |
| WEBGPU-49 | `WebGPUTextureBackend`: `WGPUTexture` (2D, RGBA8Unorm, COPY_DST + TEXTURE_BINDING) + `WGPUTextureView` | ⬜ | |
| WEBGPU-50 | `SetData()`: `wgpuQueueWriteTexture()` with `WGPUImageCopyTexture` + `WGPUTextureDataLayout` | ⬜ | |
| WEBGPU-51 | `GetData()`: `WGPUBuffer` (MAP_READ) + `wgpuCommandEncoderCopyTextureToBuffer` + `wgpuBufferMapAsync` + poll | ⬜ | Async → synchronous via polling |
| WEBGPU-52 | Mip levels: generate via `wgpuCommandEncoderCopyTextureToTexture` per level or leave as mip=1 (document) | ⬜ | |
| WEBGPU-53 | `WebGPURenderTargetBackend`: `WGPUTexture` (RENDER_ATTACHMENT + TEXTURE_BINDING) + depth texture | ⬜ | |
| WEBGPU-54 | `SetRenderTarget(rt)` / `SetRenderTarget(nullptr)`: switch render pass target between RT and swapchain view | ⬜ | |
| WEBGPU-55 | `GetBackBufferData()`: readback via MAP_READ buffer + `wgpuCommandEncoderCopyTextureToBuffer` | ⬜ | |
| WEBGPU-56 | `WebGPUTextureCubeBackend`: `WGPUTexture` (dimension=2D, arrayLayerCount=6, CUBE_COMPATIBLE) | ⬜ | |
| WEBGPU-57 | `WebGPUTexture3DBackend`: `WGPUTexture` (dimension=3D) | ⬜ | |
| WEBGPU-58 | MSAA: `WGPUTexture` with `sampleCount=4`; resolve in render pass via `resolveTarget` | ⬜ | |

---

## Phase 62 — WebGPU backend: 2D rendering (SpriteBatch)

| #   | Task                                                                                                          | Status | Notes                                                                 |
| --- | ------------------------------------------------------------------------------------------------------------- | ------ | --------------------------------------------------------------------- |
| WEBGPU-59 | `WebGPUSpriteBatchBackend`: dynamic vertex buffer ring (3 frames) for sprite quads | ⬜ | |
| WEBGPU-60 | Upload sprite quads via `wgpuQueueWriteBuffer` per batch | ⬜ | |
| WEBGPU-61 | Per-batch draw: set pipeline, bind groups (UBO + texture), vertex buffer, draw | ⬜ | |
| WEBGPU-62 | Viewport UBO (2 floats: width, height) in sprite UBO slot | ⬜ | Replaces Vulkan sprite push constants |
| WEBGPU-63 | Sprite sort modes: Immediate, Deferred, Texture, FrontToBack, BackToFront — mirror Vulkan implementation | ⬜ | |

---

## Phase 63 — WebGPU backend: 3D draw dispatch

| #   | Task                                                                                                          | Status | Notes                                                                 |
| --- | ------------------------------------------------------------------------------------------------------------- | ------ | --------------------------------------------------------------------- |
| WEBGPU-64 | `DrawPrimitives()`: bind colored3d pipeline + UBO + vertex buffer + `wgpuRenderPassEncoderDraw` | ⬜ | |
| WEBGPU-65 | `DrawIndexedPrimitives()`: bind index buffer + `wgpuRenderPassEncoderDrawIndexed` | ⬜ | |
| WEBGPU-66 | `DrawPrimitivesEx()`: dispatch by `GpuDrawParams` (stride, textureEnabled, lightingEnabled, dualTexture, skinned, instanced) | ⬜ | Mirror Vulkan dispatch logic |
| WEBGPU-67 | `DrawUserPrimitives()`: transient `WGPUBuffer` (COPY_DST + VERTEX, mappedAtCreation=false); upload + draw + release | ⬜ | |
| WEBGPU-68 | `DrawInstancedPrimitivesEx()`: second vertex buffer binding (per-instance mat4 world transforms) | ⬜ | |
| WEBGPU-69 | PrimitiveType mapping: TriangleList→`WGPUPrimitiveTopology_TriangleList`, TriangleStrip→Strip, LineList→LineList, LineStrip→LineStrip, PointList→PointList | ⬜ | |
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
| WEBGPU-82 | `SetSamplerState()`: per-slot `WGPUSampler` cache (filter + address mode key) | ⬜ | |
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

## Phase 67 — WebGPU backend: integration tests

| #   | Task                                                                                                          | Status | Notes                                                                 |
| --- | ------------------------------------------------------------------------------------------------------------- | ------ | --------------------------------------------------------------------- |
| WEBGPU-88 | `cmake-build-webgpu` directory; `cna_webgpu_test` macro in CMakeLists.txt | ⬜ | Mirror `cna_vulkan_test` |
| WEBGPU-89 | Smoke test: init device, clear to blue, `GetBackBufferData`, assert pixel | ⬜ | `webgpu_smoke_test.cpp` |
| WEBGPU-90 | 2D sprite test: `SpriteBatch` draw white 1×1 texture → assert pixel | ⬜ | |
| WEBGPU-91 | 3D colored quad: stride=16 VPC, red quad, assert center pixel | ⬜ | `webgpu_vertex_format_test.cpp` |
| WEBGPU-92 | 3D textured quad: stride=20 VPT, green texture, assert center pixel | ⬜ | |
| WEBGPU-93 | 3D colored+textured: stride=24 VPCT, blue vertex + white tex | ⬜ | |
| WEBGPU-94 | 3D lit textured: stride=32 VPNT, magenta tex, no lighting | ⬜ | |
| WEBGPU-95 | `AlphaTestEffect`: draw with alpha < threshold → pixel transparent | ⬜ | |
| WEBGPU-96 | `DualTextureEffect`: two textures → multiply blend | ⬜ | |
| WEBGPU-97 | `EnvironmentMapEffect`: emissive color only (envAmount=0) → red pixel | ⬜ | |
| WEBGPU-98 | `SkinnedEffect`: identity bone palette → same as lit textured | ⬜ | |
| WEBGPU-99 | Instanced draw: 3 instances at different positions, assert 3 pixels | ⬜ | |
| WEBGPU-100 | RenderTarget2D: draw red into RT, blit to backbuffer → assert red | ⬜ | |
| WEBGPU-101 | MSAA 4x: draw red quad with MSAA, resolve, assert pixel | ⬜ | |
| WEBGPU-102 | OcclusionQuery: draw occluded geometry, assert query result = 0 | ⬜ | |
| WEBGPU-103 | VertexBuffer dispose guard: assert `ObjectDisposedException` after `Dispose()` | ⬜ | |
| WEBGPU-104 | Dynamic buffer stress: 12 frames × None/Discard/NoOverwrite | ⬜ | |
| WEBGPU-105 | WebGPU vertex format mapping table test (mirror Task 248 for WebGPU) | ⬜ | `WGPUVertexFormat` enum |

---

## Phase 68 — WebGPU backend: advanced and parity

| #   | Task                                                                                                          | Status | Notes                                                                 |
| --- | ------------------------------------------------------------------------------------------------------------- | ------ | --------------------------------------------------------------------- |
| WEBGPU-106 | `SetStringMarkerEXT()`: no-op (WebGPU has no debug labels in wgpu-native C API yet) | ⬜ | Document deviation |
| WEBGPU-107 | `DebugSimulateContextLoss()`: destroy and recreate device (wgpu-native supports `wgpuDeviceDestroy`) | ⬜ | |
| WEBGPU-108 | `PresentationInterval` → vsync: `wgpuSurfaceConfigure.presentMode` (Fifo=VSync, Immediate=no VSync, Mailbox=adaptive) | ⬜ | |
| WEBGPU-109 | `IsFullScreen` via `SDL_SetWindowFullscreen` — same as other backends | ⬜ | |
| WEBGPU-110 | `BackBufferWidth/Height` changes: reconfigure swap chain via `wgpuSurfaceConfigure` | ⬜ | |
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
| WEBGPU-117 | `docs/webgpu-backend.md`: architecture, deviations from Vulkan, WGSL shader map, UBO layout | ⬜ | |
| WEBGPU-118 | `docs/webgpu-vs-vulkan-deviations.md`: push constants → UBO, no wireframe, async→sync strategy | ⬜ | |
| WEBGPU-119 | Emscripten target: configure CNA for `emcc` build with `-sUSE_WEBGPU=1`; WebGPU backend routes to browser `navigator.gpu` | ⬜ | True browser WASM target |
| WEBGPU-120 | Emscripten: SDL3 Emscripten port + WebGPU surface via `emscripten_webgpu_get_device()` | ⬜ | |
| WEBGPU-121 | Emscripten: verify all 9 WGSL shader pairs compile in browser via `createShaderModule` | ⬜ | |
| WEBGPU-122 | Emscripten: run 2D smoke test in headless Chrome via `--headless=new --enable-features=WebGPU` | ⬜ | CI-friendly |
| WEBGPU-123 | Cross-backend pixel comparison: same scene rendered on EasyGL/Vulkan/Bgfx/WebGPU — assert pixel-level parity | ⬜ | |
