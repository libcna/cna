# SDL GPU Graphics Backend — Implementation Plan

> **Status (2026-07-15): Phases SDLGPU-1/SDLGPU-2 (infrastructure + device/window/swapchain
> lifecycle) are implemented and verified.** `CNA_GRAPHICS_BACKEND=SDL_GPU` configures, builds
> (`cna_backend_graphics_sdl_gpu`, zero new third-party dependencies as predicted), and a real
> window + real `SDL_GPUDevice` (Vulkan driver) clears color+depth+stencil and presents 60 frames
> with no exception, verified by `SdlGpu_Smoke` (6/6 checks, `ctest -R SdlGpu_Smoke`, real GPU via
> Vulkan on this Linux dev machine). `GraphicsBackendCompileDefinitionTests.cpp`'s
> `ExactlyOneGraphicsBackendIsSelected` was updated for the new backend and the full `CnaTests`
> suite (118 tests) still passes. Texture/vertex/index buffer creation, `SpriteBatch`, and all 3D
> draw paths (Phase SDLGPU-5 onward) are still ⬜ and throw `std::runtime_error`.
>
> **Known partial gaps in what's landed so far:** `SDL_CreateGPUDevice`'s `debug_mode` is
> hardcoded `false`, not yet wired to a CNA-side debug/validation toggle (minor, deferred).
> "Recoverable handling of a failed/occluded swapchain acquisition" (SDLGPU-11) only covers the
> one case `SDL_gpu` itself documents (a null texture on a minimized window) — there is no
> WebGPU-style surface-loss/outdated-recovery case to handle because `SDL_gpu` does not expose
> one at this API level.
>
> **Real, cross-backend bug found while verifying this phase (out of scope for this plan, not
> fixed here):** `GraphicsDeviceManager::SynchronizeWithVerticalRetrace` +
> `ApplyChanges()`/`applyToExistingBackend()` never actually reaches
> `IGraphicsBackend::SetSwapInterval()` for *any* backend — `applyToExistingBackend()` calls
> `GraphicsDevice::Reset(pp, adapter)`, which never calls `SetPresentationParameters()` (the only
> method that forwards to `backend_->SetSwapInterval()`). `SdlGpu_Smoke` works around this by
> calling `backend.SetSwapInterval(0)` directly rather than through `GraphicsDeviceManager`, since
> this test's virtual/headless display has no real vblank signal and VSync waits ~1s/frame here.
> This looks like a genuine, long-standing gap affecting every backend's
> `GraphicsDeviceManager.SynchronizeWithVerticalRetrace` property, not something introduced by or
> specific to SDL_GPU — worth its own task in whichever plan file owns `GraphicsDeviceManager`,
> not this one.
>
> **Status legend:** ✅ implemented *and verified against its stated acceptance criteria*;
> 🟨 code or documentation exists but has not met those criteria; ⬜ not implemented.

---

## Why an SDL GPU backend

- **Zero new third-party dependency.** SDL3 is already a vendored/git-cloned dependency of this
  project (`third_party/SDL`), and `SDL_gpu.c` is already compiled into every prebuilt SDL3
  package this repo produces (`.sdl-prebuilt*/`). Unlike WebGPU (`wgpu-native`, a separate
  library) or Vulkan (the Vulkan SDK/loader), adding this backend requires no new external
  package — only new CNA-side code.
- **One API, several native backends.** `SDL_gpu` itself dispatches to Vulkan, D3D12, or Metal
  depending on platform/availability. A working SDL GPU backend gets CNA a second real path to
  Direct3D 12 and Metal without maintaining separate `D3D12`-style or `Metal`-style CNA backend
  code for them — at the cost of losing direct control over backend-specific behavior that the
  dedicated `D3D11`/`D3D12`/`Vulkan` backends have.
- **Consistent with the existing multi-backend architecture.** CNA already supports 9
  `CNA_GRAPHICS_BACKEND` values (`SDL_RENDERER`, `EASYGL`, `BGFX`, `VULKAN`, `WEBGPU`, `HEADLESS`,
  `SOFTWARE`, `D3D11`, `D3D12`) selected at CMake configure time; this is simply the tenth,
  following the exact same `IGraphicsBackend` contract every other backend already implements
  (`include/CNA/Internal/Backends/Common/IGraphicsBackend.hpp`).

This plan does **not** propose retiring any existing backend. See
`Internal (CNA) vs XNA Layer` in `CLAUDE.md` for the backend-selection architecture this plan
extends.

---

## Non-goals and known constraints (read before starting)

- **Not a replacement for any existing backend.** SDL GPU is an *additional* pluggable backend.
- **No occlusion-query support in `SDL_gpu` itself.** As of the vendored `third_party/SDL/include/SDL3/SDL_gpu.h`,
  there is no query-pool/occlusion-query type anywhere in the API (only `SDL_GPUFence` for
  CPU/GPU synchronization). `IOcclusionQueryBackend::CreateOcclusionQuery()` must return
  `nullptr` on this backend, exactly like the `Headless`/`Software` backends already do for
  their own reasons. This is a **permanent limitation of the underlying API**, not a task gap —
  do not open a task to "fix" it unless a future SDL3 version adds real query support.
- **Shaders must be precompiled bytecode with a mandatory resource-binding order.**
  `SDL_gpu` does not accept GLSL/HLSL source at runtime. `SDL_CreateGPUShader` takes
  `SDL_GPUShaderFormat`-tagged bytecode (SPIR-V for the Vulkan driver, DXBC/DXIL for the D3D12
  driver, MSL/metallib for the Metal driver), and — per `SDL_gpu.h` (~L2606–2744) — every
  shader stage's samplers/storage-textures/storage-buffers/uniform-buffers must be declared in a
  **fixed HLSL-style order** (`t[n]`/`s[n]` space0, `u[n]` space1, `b[n]` space2 for the vertex
  stage; an analogous space2/space3 split for the fragment stage). This is a different
  convention from the raw `layout(set=, binding=)` numbers the existing `VulkanGraphicsBackend`
  GLSL shaders use (`src/CNA/Internal/Backends/Vulkan/shaders/*.glsl`) — those shaders are a
  useful **algorithmic** reference (the lighting math, the alpha-test logic, the dual-texture
  blend) but their compiled SPIR-V is **not** directly reusable as-is; the binding layout has to
  be re-authored to satisfy `SDL_gpu`'s convention. See Phase `SDLGPU-3` for the decision this
  implies (hand-author SPIR-V for the Vulkan driver first vs. vendoring the official
  `SDL_shadercross` cross-compiler for all formats).
- **Verified target for the first milestone: Linux desktop via `SDL_gpu`'s Vulkan driver.**
  This matches the current dev environment (the only `SDL_gpu` driver compiled into this
  project's prebuilt SDL3 packages on Linux is `vulkan`, per
  `.sdl-prebuilt-Linux-x86_64/SDL/build/.../src/gpu/`). Windows (D3D12 driver) and macOS/iOS
  (Metal driver) remain **code paths only, not validation claims**, until run on that real
  hardware — the same phased-claim discipline already used for `D3D11`/`D3D12` (Wine/DXVK/
  vkd3d-proton verified, real-Windows-hardware tasks marked `needs_human`) and for WebGPU
  (Linux-desktop-verified, Windows/macOS unverified).
- **Pipelines are immutable objects**, exactly like Vulkan/D3D12/WebGPU in this codebase — a
  pipeline cache keyed by (shader pair, vertex format, blend/depth-stencil/rasterizer state,
  sample count, target formats) is required from the start, not an afterthought.

---

## Naming conventions for this backend

| Item | Value |
| --- | --- |
| `CNA_GRAPHICS_BACKEND` value | `SDL_GPU` |
| CMake option | `CNA_BACKEND_SDL_GPU` |
| Compile definition | `CNA_BACKEND_SDL_GPU` |
| Backend directory | `src/CNA/Internal/Backends/SdlGpu/`, `include/CNA/Internal/Backends/SdlGpu/` |
| CMake target | `cna_backend_graphics_sdl_gpu` |
| Main class | `CNA::Internal::Backends::SdlGpuGraphicsBackend` (deliberately distinct from the existing `SdlGraphicsBackend` in `SdlRenderer/`, which wraps the older `SDL_Renderer` 2D API — the two are unrelated APIs and must not share a class name) |
| Task prefix | `SDLGPU-` |
| CTest target | `SdlGpu_Smoke` (plus one CTest binary per capability, mirroring `WebGPU_Colored3D`, `D3D11_Smoke`, etc.) |

---

## Active execution order — do this one task at a time

1. ~~`SDLGPU-1`~~ – ~~`SDLGPU-12`~~ — Phases `SDLGPU-1`/`SDLGPU-2` (infrastructure, device/window/
   swapchain lifecycle, color+depth+stencil clear/present) done and verified 2026-07-15, all ✅
   except two 🟨 rows noted in each row's own Notes column (debug-mode toggle not yet wired;
   `IGraphicsBackend`-family concrete subclasses besides the main backend class don't exist yet).
2. Next: Phase `SDLGPU-3` (`SDLGPU-13`–`16`, shader authoring/resource-binding strategy —
   `SDLGPU-13` is a **decision task**, resolve it before writing any WGSL/SPIR-V-equivalent
   shader) or Phase `SDLGPU-4` (pipeline/render-state mapping) in either order, since both are
   prerequisites for the Phase `SDLGPU-5` 2D vertical slice. Do not skip ahead to 3D/effects work
   before the Phase `SDLGPU-5` 2D vertical slice is verified end-to-end, matching how every other
   3D-capable backend in this repo was built up (Headless/Software/Vulkan/WebGPU all landed a
   working 2D baseline before any 3D vertex format).

---

## Phase SDLGPU-1 — Infrastructure and CMake integration

| # | Task | Status | Notes |
| --- | --- | --- | --- |
| SDLGPU-1 | Add `CNA_GRAPHICS_BACKEND=SDL_GPU` as a tenth valid value: extend the `CACHE STRING`/`set_property(... STRINGS ...)` list (`CMakeLists.txt` ~L94–95), the `option(CNA_BACKEND_SDL_GPU ...)` declaration and its inclusion in the mutual-exclusion `_cna_enabled_backends` list (~L97–147). | ✅ | Done 2026-07-15. Also updated `tests/.../GraphicsBackendCompileDefinitionTests.cpp`'s `ExactlyOneGraphicsBackendIsSelected` counter, which would otherwise fail under this backend. |
| SDLGPU-2 | Add the `elseif(CNA_GRAPHICS_BACKEND STREQUAL "SDL_GPU")` branch (~L184–255) setting `BACKEND_DIR`, `BACKEND_TARGET`, `add_compile_definitions(CNA_BACKEND_SDL_GPU)`. | ✅ | Done 2026-07-15, mirroring `HEADLESS`/`SOFTWARE` — no `find_package` call needed. |
| SDLGPU-3 | Add the matching backend-library-target block (~L301+) and, if the backend needs test-only source files, its own `CNA_BUILD_TESTS AND CNA_GRAPHICS_BACKEND STREQUAL "SDL_GPU"` guard (mirroring `HEADLESS`/`SOFTWARE`/`D3D11`/`D3D12` at ~L6497–6799). | ✅ | Link block done 2026-07-15 (`SDL3::SDL3` only). The `CNA_BUILD_TESTS` guard was added directly as `SdlGpu_Smoke`'s own registration (see SDLGPU-12), not as an empty placeholder. |
| SDLGPU-4 | Create `include/CNA/Internal/Backends/SdlGpu/SdlGpuGraphicsBackend.hpp` — class skeleton declaring every `IGraphicsBackend`-family interface (`IVertexBufferBackend`, `IIndexBufferBackend`, `ITextureBackend`, `ITextureCubeBackend`, `ITexture3DBackend`, `IRenderTargetBackend`, `IRenderTargetCubeBackend`, `IEffectBackend`, `ISpriteBatchBackend`, `IOcclusionQueryBackend`, `IGraphicsBackend`). | 🟨 | Done 2026-07-15 for every pure-virtual `IGraphicsBackend` method actually required to compile (Clear/Present/viewport/window/state plumbing). Concrete `ITextureBackend`/`IVertexBufferBackend`/etc. subclasses don't exist yet — not needed until SDLGPU-22/23 give them a real body. |
| SDLGPU-5 | Create `src/CNA/Internal/Backends/SdlGpu/SdlGpuGraphicsBackend.cpp` — constructor/destructor scaffolding, explicit `ThrowUnsupported...`-style paths for every not-yet-implemented method, so the backend compiles and links from task 1. | ✅ | Done 2026-07-15 (`ThrowNotImplemented()`); `CreateTexture`/`CreateSpriteBatch`/`CreateVertexBuffer`/`CreateIndexBuffer16`/`DrawColoredPrimitives`/`DrawIndexedColoredPrimitives` all throw, verified by `SdlGpu_Smoke`'s own throw checks. |

---

## Phase SDLGPU-2 — Device, window and swapchain lifecycle

| # | Task | Status | Notes |
| --- | --- | --- | --- |
| SDLGPU-6 | `SDL_CreateGPUDevice` — request `SDL_GPU_SHADERFORMAT_SPIRV` first (Linux/Vulkan driver target), debug-mode flag wired to a CNA-side validation/debug toggle. | 🟨 | Done 2026-07-15 except the debug-mode flag, which is hardcoded `false` rather than wired to a CNA-side toggle (deferred, minor). `SDL_GPUSupportsShaderFormats`-gated startup error not added — `SDL_CreateGPUDevice` returning null already throws with `SDL_GetError()`, judged sufficient for now. |
| SDLGPU-7 | `SDL_ClaimWindowForGPUDevice` + present-mode negotiation (`SDL_WindowSupportsGPUPresentMode`, `SDL_SetGPUSwapchainParameters`) mapped from XNA's `PresentationParameters`/vsync equivalent. | ✅ | Done 2026-07-15. Verified both paths: VSync (default) and Immediate (`SdlGpu_Smoke` calls `SetSwapInterval(0)` directly — see this file's top-of-file note on the separate `GraphicsDeviceManager` gap that motivated calling it directly). |
| SDLGPU-8 | Per-frame `SDL_AcquireGPUCommandBuffer` + `SDL_WaitAndAcquireGPUSwapchainTexture` (or `SDL_AcquireGPUSwapchainTexture` for the non-blocking variant), including the documented zero-size/minimized-window null-texture case. | ✅ | Done 2026-07-15. The null-texture case is handled (submit the empty command buffer, skip the frame) per `SDL_gpu.h`'s own documented contract, but not independently exercised by a real minimized-window test yet — code-path only for that specific branch. |
| SDLGPU-9 | Main render pass (`SDL_BeginGPURenderPass`/`SDL_EndGPURenderPass`) with color + depth + stencil `SDL_GPUColorTargetInfo`/`SDL_GPUDepthStencilTargetInfo` attachments. | ✅ | Done 2026-07-15, including real depth+stencil texture creation/recreation-on-resize (`EnsureDepthStencilTexture`, D24_UNORM_S8_UINT preferred, D32_FLOAT_S8_UINT fallback, per-device `SDL_GPUTextureSupportsFormat` query). Verified via `SdlGpu_Smoke`'s combined `Target\|DepthBuffer\|Stencil` clear, not yet by an actual depth-tested draw (no draw path exists yet). |
| SDLGPU-10 | Clear semantics — map XNA's `ClearOptions` combinations (`Target`/`DepthBuffer`/`Stencil` and their unions, i.e. `Clear`/`ClearColorAndDepth`/`ClearDepth`/`ClearStencil`/`ClearDepthAndStencil`/`ClearColorAndStencil`/`ClearColorDepthAndStencil`) onto `SDL_GPULoadOp::CLEAR` vs `LOAD` per attachment. | ✅ | Done 2026-07-15, all 7 `IGraphicsBackend` clear methods implemented; `SdlGpu_Smoke` exercises the combined color+depth+stencil path every frame. |
| SDLGPU-11 | Present via `SDL_SubmitGPUCommandBuffer` (or `SDL_SubmitGPUCommandBufferAndAcquireFence` where a fence is needed for readback), with recoverable handling of a failed/occluded swapchain acquisition. | 🟨 | Done 2026-07-15 for the one recoverable case `SDL_gpu` itself documents (null swapchain texture, e.g. minimized window — submit and skip, no throw). There is no WebGPU-style surface-loss/outdated/suboptimal recovery case to add here: `SDL_gpu`'s own API doesn't expose one at this level. A hard acquisition failure (`SDL_WaitAndAcquireGPUSwapchainTexture` returning `false`) still throws — not yet proven recoverable in practice. |
| SDLGPU-12 | First end-to-end proof: a minimal native window that initializes the backend, clears, presents at least 60 frames, then exits cleanly. | ✅ | Verified 2026-07-15: `examples/sdlgpu_smoke_test.cpp` + `SdlGpu_Smoke` CTest, real window + real `SDL_GPUDevice` (Vulkan driver) on this Linux dev machine, 60 frames of combined color+depth+stencil `Clear()`+`Present()`, plus `GetWindowInternal`/`GetRendererInternal`/`GetViewportSize`/`CreateVertexBuffer`/`CreateIndexBuffer16` checks — 6/6 checks, `ctest -R SdlGpu_Smoke` passes in ~2s (with VSync off; VSync itself was also confirmed working, at ~1s/frame on this environment's virtual display, before switching the smoke test to Immediate for speed). |

---

## Phase SDLGPU-3 — Shader authoring and resource-binding strategy

| # | Task | Status | Notes |
| --- | --- | --- | --- |
| SDLGPU-13 | **Decision task.** Choose the shader pipeline: (a) hand-author SPIR-V for the Vulkan driver by extending the existing `compile_shaders.py`/`libshaderc` runtime-compile pattern with `SDL_gpu`'s mandatory binding order, deferring DXBC/DXIL/MSL authoring; or (b) vendor the official [`SDL_shadercross`](https://github.com/libsdl-org/SDL_shadercross) tool to compile a single HLSL source to all four formats. Neither is currently vendored in this repo (`SDL_shadercross` was not found anywhere under this tree as of 2026-07-15). Document the choice and its platform scope explicitly in this row before starting `SDLGPU-14`. | ⬜ | Recommendation: start with (a) — it reuses this repo's already-working `libshaderc` toolchain and needs no new vendored project — and revisit (b) only when Windows/macOS validation (Phase `SDLGPU-13` platform-expansion phase, not to be confused with this task's number) actually becomes active work. |
| SDLGPU-14 | `sprite2d` SDL-GPU shader pair (vertex + fragment), compiled to SPIR-V, satisfying `SDL_GPUShaderCreateInfo`'s explicit `num_samplers`/`num_storage_textures`/`num_storage_buffers`/`num_uniform_buffers` counts. | ⬜ | The 2D vertical slice (Phase `SDLGPU-5`) depends on this. |
| SDLGPU-15 | `SDL_GPUVertexInputState`/`SDL_GPUVertexAttribute`/`SDL_GPUVertexElementFormat` mapping for the four stock vertex strides: `VertexPositionColor` (16), `VertexPositionTexture` (20), `VertexPositionColorTexture` (24), `VertexPositionNormalTexture` (32). | ⬜ | Mirror the existing per-backend vertex-format helper pattern (see `include/CNA/Internal/Backends/Vulkan/VulkanVertexFormatHelper.hpp` for the closest analogous helper to imitate the *shape* of, not the Vulkan-specific binding numbers). |
| SDLGPU-16 | Per-draw uniform delivery via `SDL_PushGPUVertexUniformData`/`SDL_PushGPUFragmentUniformData` (world/view/projection, `DiffuseColor`, alpha-test params, light params), respecting the documented std140 alignment rule (`vec3`/`vec4` fields 16-byte aligned). | ⬜ | This is push-style data recorded per command buffer — ergonomically closer to Vulkan push constants than to WebGPU's mandatory-UBO-only model. `SDL_gpu.h` does not document a hard maximum push size; measure the real per-driver limit empirically before relying on it for large payloads (see `SDLGPU-35`'s skinned-effect caveat). |

---

## Phase SDLGPU-4 — Pipelines and render state

| # | Task | Status | Notes |
| --- | --- | --- | --- |
| SDLGPU-17 | `SDL_GPUGraphicsPipelineCreateInfo` construction plus a pipeline cache keyed by (shader pair, vertex format, blend state, depth/stencil state, rasterizer state, sample count, color/depth target formats). | ⬜ | Required from the start — `SDL_gpu` pipelines are immutable, matching the same constraint `VulkanGraphicsBackend.cpp`/`WebGPUGraphicsBackend.cpp` already solved; follow their caching pattern. |
| SDLGPU-18 | `BlendState` mapping — XNA `BlendState` → `SDL_GPUColorTargetBlendState` (`SDL_GPUBlendFactor`/`SDL_GPUBlendOp`). | ⬜ | |
| SDLGPU-19 | `DepthStencilState` mapping — `SDL_GPUDepthStencilState` (`SDL_GPUCompareOp`, `SDL_GPUStencilOpState`), covering `DepthBufferEnable`/`DepthBufferWriteEnable`/`StencilEnable` and all three XNA stencil-op fields (`StencilFail`/`StencilDepthBufferFail`/`StencilPass`). | ⬜ | |
| SDLGPU-20 | `RasterizerState` mapping — `SDL_GPURasterizerState` (`SDL_GPUFillMode`, `SDL_GPUCullMode`, `SDL_GPUFrontFace`), covering `CullMode.CullClockwiseFace`/`CullCounterClockwiseFace`/`None` and `FillMode.WireFrame`/`Solid`. | ⬜ | |
| SDLGPU-21 | `SamplerState` mapping — `SDL_GPUSamplerCreateInfo` (`SDL_GPUFilter`, `SDL_GPUSamplerMipmapMode`, `SDL_GPUSamplerAddressMode`) for per-slot Wrap/Clamp/Mirror + Point/Linear/Anisotropic. | ⬜ | Match the per-slot dynamic sampler behavior already verified for D3D11/D3D12 (`DX-119`, `DX-154`). |

---

## Phase SDLGPU-5 — 2D vertical slice: Texture2D + buffers + SpriteBatch

| # | Task | Status | Notes |
| --- | --- | --- | --- |
| SDLGPU-22 | `SdlGpuTexture2DBackend` — `SDL_CreateGPUTexture` + upload via `SDL_CreateGPUTransferBuffer`/`SDL_MapGPUTransferBuffer`/`SDL_UnmapGPUTransferBuffer`/`SDL_BeginGPUCopyPass`/`SDL_UploadToGPUTexture`/`SDL_EndGPUCopyPass`. | ⬜ | |
| SDLGPU-23 | `SdlGpuVertexBufferBackend`/`SdlGpuIndexBufferBackend` — `SDL_CreateGPUBuffer` (`VERTEX`/`INDEX` usage) + upload via the same transfer-buffer/copy-pass pattern; `SetDataWithOptions` `Discard`/`NoOverwrite` streaming hints. | ⬜ | |
| SDLGPU-24 | `SdlGpuSpriteBatchBackend` — batch quads, bind the `sprite2d` pipeline, bind texture+sampler via `SDL_BindGPUFragmentSamplers`, draw via `SDL_DrawGPUIndexedPrimitives`. Must cover source rectangles, tint/alpha, rotation, both flips, and Linear/Point + Clamp/Wrap/Mirror sampling. | ⬜ | Same behavioral bar as `WEBGPU-126`'s validation scene. |
| SDLGPU-25 | **First milestone gate.** A demo/smoke harness renders textured, tinted, rotated, flipped sprites and survives a resize through the SDL GPU backend with no validation error, device loss, or loader failure. | ⬜ | Same acceptance bar as `WEBGPU-124`–`131`'s "verified native 2D baseline" declaration — do not mark this ✅ from source inspection alone. |

---

## Phase SDLGPU-6 — Core 3D vertex formats and BasicEffect

| # | Task | Status | Notes |
| --- | --- | --- | --- |
| SDLGPU-26 | `colored3d` pipeline + shader + `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives` dispatch (stride 16), with real depth-test verification. | ⬜ | |
| SDLGPU-27 | `textured3d` (stride 20) — real `Texture2D` sampling, `DiffuseColor` genuinely multiplying it. | ⬜ | |
| SDLGPU-28 | `colored_textured3d` (stride 24) — vertex-color mixing combined with texture sampling. | ⬜ | |
| SDLGPU-29 | `lit_textured3d` (stride 32, `VertexPositionNormalTexture`) — FNA's `Lighting.fxh` `ComputeLights()` (3 directional lights, ambient, Blinn-Phong specular, emissive). | ⬜ | Reuse the *algorithm* from `VulkanGraphicsBackend`'s `lit_textured3d.{vert,frag}.glsl` (already ported once from FNA) — re-author the binding layout, do not assume the compiled SPIR-V drops in unchanged. Reuse the same safe-normalize guard the WebGPU port needed for a zero-direction disabled light (`WEBGPU-22`/`33` note). |
| SDLGPU-30 | `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx` `GpuDrawParams` dispatch by vertex stride, matching the dispatch-by-stride convention every other 3D-capable backend in this codebase already uses. | ⬜ | |

---

## Phase SDLGPU-7 — Remaining stock effects

| # | Task | Status | Notes |
| --- | --- | --- | --- |
| SDLGPU-31 | `AlphaTestEffect` (per-pixel discard) — strides 20/24/32. | ⬜ | |
| SDLGPU-32 | `DualTextureEffect` (two-sampler multiply, `tex1.rgb*=2.0; result=tex1*tex2*tint`) — strides 20/24. | ⬜ | |
| SDLGPU-33 | `EnvironmentMapEffect` (cubemap reflection) — needs `SDL_GPU_TEXTURETYPE_CUBE` texture creation + `SDL_GPUCubeMapFace`-indexed per-face upload. | ⬜ | |
| SDLGPU-34 | `SkinnedEffect` (bone-matrix palette). | ⬜ | Verify the real per-driver push-uniform size limit before committing to `SDL_PushGPUVertexUniformData` for full bone palettes — if the limit is too small, a genuine uniform/storage buffer bound via `SDL_BindGPUVertexStorageBuffers` may be required instead, mirroring the second-UBO approach WebGPU needed for `lit_textured3d` (`WEBGPU-22`/`33`). |

---

## Phase SDLGPU-8 — Render targets

| # | Task | Status | Notes |
| --- | --- | --- | --- |
| SDLGPU-35 | `RenderTarget2D` — a `SDL_GPU_TEXTUREUSAGE_COLOR_TARGET \| SDL_GPU_TEXTUREUSAGE_SAMPLER` texture, bound as `SDL_GPUColorTargetInfo.texture` in one pass, sampled in a later pass. | ⬜ | |
| SDLGPU-36 | `RenderTargetCube` — 6-face color-target texture, including MSAA and mip levels beyond face 0. | ⬜ | Match the bar already met by D3D11/D3D12 (`DX-152`/`DX-153`). |
| SDLGPU-37 | Multiple Render Targets (MRT) — `SDL_GPUGraphicsPipelineTargetInfo.color_target_descriptions` array + several `SDL_GPUColorTargetInfo` entries in one render pass. | ⬜ | |
| SDLGPU-38 | MSAA — `SDL_GPUSampleCount` texture creation + `SDL_GPUColorTargetInfo.resolve_texture` automatic resolve-on-render-pass-end. | ⬜ | |
| SDLGPU-39 | `GetData()` readback — `SDL_DownloadFromGPUTexture` via a transfer buffer + fence wait (`SDL_SubmitGPUCommandBufferAndAcquireFence`/`SDL_WaitForGPUFences`), for `Texture2D`/`TextureCube`/`RenderTarget2D`/`RenderTargetCube`. | ⬜ | |

---

## Phase SDLGPU-9 — Texture3D and remaining texture surface

| # | Task | Status | Notes |
| --- | --- | --- | --- |
| SDLGPU-40 | `Texture3D` — `SDL_GPU_TEXTURETYPE_3D`, sub-volume upload/readback. | ⬜ | Match the byte-exact round-trip bar D3D12 already met (`DX-122`). |
| SDLGPU-41 | Mipmaps — `SDL_GenerateMipmapsForGPUTexture` for the generated case, explicit per-level `SDL_UploadToGPUTexture` calls when XNA supplies authored mip data. | ⬜ | |

---

## Phase SDLGPU-10 — Custom ShaderEffect (`IEffectBackend`)

| # | Task | Status | Notes |
| --- | --- | --- | --- |
| SDLGPU-42 | **Decision task.** `SDL_gpu` only accepts precompiled bytecode, so an arbitrary user-authored `ShaderEffect` needs either (a) a runtime GLSL→SPIR-V compile via `libshaderc` for the Vulkan-driver case only, or (b) deferring custom-`ShaderEffect` support on this backend until `SDLGPU-13`'s `SDL_shadercross` alternative (if chosen) is vendored. Document the choice and its exact platform scope — do not silently support it on Linux only while implying full parity with `D3D11`'s runtime `D3DCompile()` path. | ⬜ | |
| SDLGPU-43 | Implement `SdlGpuEffectBackend::CompileProgram`/`Bind`/`Unbind`/`IsValid`/`GetCompileError` per the `SDLGPU-42` decision. | ⬜ | |

---

## Phase SDLGPU-11 — Known permanent limitation: occlusion queries

| # | Task | Status | Notes |
| --- | --- | --- | --- |
| SDLGPU-44 | Confirm and document that `SDL_gpu` has no occlusion-query API in the vendored SDL3 (`third_party/SDL/include/SDL3/SDL_gpu.h`, checked 2026-07-15 — no query-pool type exists). `CreateOcclusionQuery()` returns `nullptr`, matching `IGraphicsBackend`'s own documented default and the posture `Headless`/`Software` already take. | ⬜ | Not a gap to close by writing code — only re-check this row if a future SDL3 upgrade adds real query support. |

---

## Phase SDLGPU-12 — Testing and CI

| # | Task | Status | Notes |
| --- | --- | --- | --- |
| SDLGPU-45 | `SdlGpu_Smoke` CTest target (mirroring `D3D11_Smoke`/`D3D12_Smoke`/`WebGPU_Native2D_Smoke`), reporting SKIPPED with a clear reason when no `DISPLAY`/`WAYLAND_DISPLAY` is present. | ⬜ | |
| SDLGPU-46 | One CTest binary per capability, matching this codebase's existing per-backend test structure: `SdlGpu_Colored3D`, `SdlGpu_Textured3D`, `SdlGpu_ColoredTextured3D`, `SdlGpu_LitTextured3D`, `SdlGpu_AlphaTest3D`, `SdlGpu_DualTexture3D`, `SdlGpu_EnvMap`, `SdlGpu_Skinned`, `SdlGpu_RenderTarget2D`, `SdlGpu_RenderTargetCube`, `SdlGpu_MRT`, `SdlGpu_MSAA`, `SdlGpu_Texture3D`. | ⬜ | Every public method/operator/constant this backend newly exposes still needs its own test per `CLAUDE.md`'s testing rules — this row is the backend-specific CTest scaffolding, not a substitute for per-class unit tests. |
| SDLGPU-47 | Cross-backend diagnostic — compare SDL GPU output against an existing verified backend (Vulkan or Software) pixel-for-pixel for at least one shared scene. | ⬜ | `CNA_GRAPHICS_BACKEND` is a compile-time choice, so this needs two separate builds compared offline, exactly as documented in `CMakeLists.txt` (~L6799) for the existing Software-backend cross-backend diagnostic (Phase S9). |

---

## Phase SDLGPU-13 — Platform expansion (deferred until the Linux/Vulkan-driver path is fully verified)

| # | Task | Status | Notes |
| --- | --- | --- | --- |
| SDLGPU-48 | Windows validation via `SDL_gpu`'s D3D12 driver. | ⬜ | Code paths only until run on real (or Wine/DXVK-class) Windows infrastructure, same caveat structure already used for `D3D11`/`D3D12`/WebGPU. |
| SDLGPU-49 | macOS/iOS validation via `SDL_gpu`'s Metal driver. | ⬜ | `needs_human` — no Apple hardware in this dev environment, same class of gate as `DX-90`/`DX-91`/`DX-114`. |
| SDLGPU-50 | Android validation via `SDL_gpu`'s Vulkan driver. | ⬜ | The Android prebuilt SDL3 package already exists (`.sdl-prebuilt-Android-aarch64/`) and already compiles `SDL_gpu.c` — check whether its Vulkan GPU driver was also compiled in before assuming this is free. |

---

## Closing notes

- Follow `CHECKLIST.md`'s per-file checklist for every new `.hpp`/`.cpp` pair this plan produces
  (SPDX header, Doxygen on every public member, `GetTypeName()` override, etc.) — this plan file
  only tracks *what* to build, not the per-file compliance checklist, which is authoritative on
  its own.
- One task = one commit, per `CLAUDE.md`'s git-commit rules — do not bundle multiple `SDLGPU-`
  rows into a single commit, and update this file's status column in the same commit that closes
  a task.
- Update the phase-level summary at the top of this file once the Phase `SDLGPU-5` 2D vertical
  slice is verified, the same way `plan_webgpu.md`'s header was rewritten once its own 2D
  baseline landed — do not let the header go stale relative to the task table below it.
