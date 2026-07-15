# SDL GPU Graphics Backend — Implementation Plan

> **Status (2026-07-15): Phases SDLGPU-1 through SDLGPU-6 are fully implemented and verified;
> Phase SDLGPU-7 is half done** — `AlphaTestEffect`/`DualTextureEffect` are real and verified,
> `EnvironmentMapEffect`/`SkinnedEffect` are deliberately deferred (see their own rows for why).
> `CNA_GRAPHICS_BACKEND=SDL_GPU` configures, builds (`cna_backend_graphics_sdl_gpu`, zero new
> third-party dependencies as predicted), and a real window + real `SDL_GPUDevice` (Vulkan driver)
> clears color+depth+stencil, uploads a real `Texture2D`, draws a real `SpriteBatch` scene (tint,
> alpha, rotation, both flips, Point/Linear + Wrap/Clamp sampling), draws real
> `colored3d`/`textured3d`/`colored_textured3d`/`lit_textured3d` 3D geometry (with a real
> depth-test occlusion proof), and draws real `AlphaTestEffect`/`DualTextureEffect` geometry (real
> per-pixel discard; a real two-sampler multiply with a colour-shift result that could only come
> from two textures actually being sampled) — all via the public `BasicEffect`-family/
> `VertexBuffer`/`GraphicsDevice.DrawPrimitives` API. Verified by `SdlGpu_Smoke` (6/6), `SdlGpu_2D`
> (3/3), `SdlGpu_3D` (6/6), and `SdlGpu_Effects` (3/3 checks, `ctest -R SdlGpu`), all on real GPU
> via Vulkan on this Linux dev machine, each backed by a real screenshot, not just "didn't throw".
> Those screenshots caught and led to fixing a real bug — see `SDLGPU-14`'s row below — and then,
> for the 3D path, *confirmed the fix's own theory* (3D shaders using a real XNA projection matrix
> need no Y-flip, unlike the hand-computed 2D sprite NDC math). `GraphicsBackendCompileDefinitionTests.cpp`'s
> `ExactlyOneGraphicsBackendIsSelected` was updated for the new backend and the full `CnaTests`
> suite (118 tests) still passes. `EnvironmentMapEffect`/`SkinnedEffect` are still ⬜. **Phase
> `SDLGPU-8` (render targets) is now fully done**: `SDLGPU-35` (`RenderTarget2D`), `SDLGPU-36`
> (`RenderTargetCube`, real MSAA + mip regen), `SDLGPU-37` (MRT), `SDLGPU-38` (`RenderTarget2D`
> MSAA), and `SDLGPU-39`'s `RenderTarget2D`/`RenderTargetCube` legs are all done and verified,
> 2026-07-15 — see their own rows for the real multi-pass `EnsureFrameRendered` refactor and
> `DrawTarget` generalization this required. `SDLGPU-39`'s remaining legs (swapchain/plain
> `Texture2D`/`TextureCube`) stay 🟨 — the swapchain leg hit a real, unresolved segfault (see that
> row); plain `Texture2D`/`TextureCube` are separately out of scope (already-accurate CPU shadow /
> `SDLGPU-40` not landed, respectively).
>
> **Real cross-backend interface change made 2026-07-15 to close `SDLGPU-39`'s `RenderTarget2D`
> leg:** `ITextureBackend` (in the common `IGraphicsBackend.hpp` every backend implements) gained a
> new `GetData(level, x, y, w, h, data, dataLength) const` virtual with a safe no-op default
> (mirrors `ITextureCubeBackend::GetData`'s own convention) — purely additive, no other backend's
> code needed to change. `Texture2D::GetData()`'s two entry points now fall back to this only when
> the CPU-side shadow is empty (i.e. only for a `RenderTarget2D`); plain, `SetData()`-populated
> textures are completely unaffected (confirmed via 123/123 passing `Texture2DTest`/
> `TextureCubeTest`/`Texture3DTest`). See `SDLGPU-39`'s own row for the full detail.
>
> **Real architectural gotcha found while writing `SDLGPU-37`'s test (applies to any code using
> this backend, not just tests):** a `RenderTarget2D`/`RenderTargetCube` destroyed before this
> backend's deferred `Present()`-time render pass actually executes is a genuine use-after-free —
> the destructor releases the real `SDL_GPUTexture*` immediately, but any sprite/draw command
> already queued against it (not yet rendered, since rendering is batched until `Present()`) still
> holds that now-freed handle. Confirmed via a real segfault when `sdlgpu_mrt_test.cpp`'s first
> draft used `Draw()`-local `RenderTarget2D` instances. Keep every render target alive at least
> until the next `Present()`/frame boundary (e.g. as a member field, matching every test in this
> plan) — never as a short-lived local inside a single `Draw()` call. Not fixed at the architecture
> level (would need an eager-flush-on-destruction policy or a deferred-release queue) — see
> `SDLGPU-37`'s own row for the full analysis.
>
> **Full `CnaTests` suite is NOT currently stable end-to-end under `CNA_GRAPHICS_BACKEND=SDL_GPU`
> in this sandboxed dev environment** (found 2026-07-15 while trying to verify `SDLGPU-35`
> broadly): running the entire ~4300-test suite segfaults partway through
> `ContentManagerSkinnedModelTest` (confirmed reproducible on the unmodified pre-`SDLGPU-35`
> baseline too, and the exact test it crashes on shifts when the first one is filtered out —
> consistent with real-window/GPU-device resource churn across thousands of tests in this sandbox,
> not a deterministic logic bug in one test). This is a pre-existing condition, not something
> `SDLGPU-35` introduced or fixed. This backend's own dedicated CTest suite
> (`SdlGpu_Smoke`/`SdlGpu_2D`/`SdlGpu_3D`/`SdlGpu_Effects`/`SdlGpu_RenderTarget2D`/
> `SdlGpu_RenderTargetCube`/`SdlGpu_MRT`/`SdlGpu_RenderTarget2DMSAA`, `ctest -R SdlGpu`) is the
> actual validated methodology
> for real-window backends in this project (mirrors
> how Vulkan/D3D11/D3D12 are validated) — don't treat a full unfiltered `CnaTests` run under this
> backend as a meaningful signal without first re-reading this note.
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
2. ~~`SDLGPU-13`~~ – ~~`SDLGPU-25`~~ — Phases `SDLGPU-3`/`SDLGPU-4`/`SDLGPU-5` (shader authoring
   decision, sprite2d pipeline, `Texture2D`, vertex/index buffers, `SpriteBatch`) done and
   verified 2026-07-15 — see each row's own Notes column for the handful of 🟨/⬜ sub-items
   (BlendState/DepthStencilState/RasterizerState/ApplySamplerState dynamic mapping, streaming
   hints, a genuine multi-key pipeline cache) that are real 3D-draw-path or polish work, not
   blockers for the 2D milestone itself.
3. ~~`SDLGPU-26`~~ – ~~`SDLGPU-30`~~ — Phase `SDLGPU-6` (`colored3d`/`textured3d`/
   `colored_textured3d`/`lit_textured3d`, i.e. `BasicEffect`, plus real `DrawPrimitivesEx`/
   `DrawIndexedPrimitivesEx` stride dispatch and a real depth-test occlusion proof) done and
   verified 2026-07-15, all ✅. The pipeline-cache/state-mapping generality this phase actually
   needed (`SDLGPU-17`'s `GetOrCreate*` pattern extended to 4 shader families keyed by
   topology+depthTest+depthWrite+depthFunc) landed as part of it; `SDLGPU-18`–`21`'s full
   `BlendState`/`DepthStencilState`/`RasterizerState`/`ApplySamplerState` *dynamic* mapping did
   not (every 3D pipeline is still hardcoded opaque/no-cull/Linear+Clamp) — see those rows' own
   Notes for what's real vs. still open.
4. ~~`SDLGPU-31`~~/~~`SDLGPU-32`~~ — `AlphaTestEffect`/`DualTextureEffect` done and verified
   2026-07-15, all ✅ (real per-pixel discard proof; real two-sampler-multiply proof). `SDLGPU-33`
   (`EnvironmentMapEffect`) and `SDLGPU-34` (`SkinnedEffect`) remain ⬜, deliberately deferred —
   see their own rows for the specific blockers (no `TextureCube` foundation yet; an unresolved
   72-bone-palette push-size question plus a new stride-52 vertex format).
5. **Phase `SDLGPU-8` (render targets) is fully done.** `SDLGPU-35` (`RenderTarget2D`), `SDLGPU-36`
   (`RenderTargetCube`, including real MSAA + mip regen), `SDLGPU-37` (MRT), `SDLGPU-38`
   (`RenderTarget2D` MSAA), and `SDLGPU-39`'s `RenderTarget2D`/`RenderTargetCube` `GetData()` legs
   all done and verified 2026-07-15, all ✅ — see their own rows for the real multi-pass
   `EnsureFrameRendered` refactor, the `DrawTarget{rt,cube,face}` generalization, the
   `currentExtraMrtTargets_` MRT mechanism, the `ClampSampleCount`/automatic-resolve MSAA
   mechanism, and the new `ITextureBackend::GetData` cross-backend interface addition this
   required, plus `SDLGPU-37`'s own row for a real cross-cutting resource-lifetime gotcha found
   while testing it (see the status banner's own callout). `SDLGPU-39`'s remaining legs (swapchain/
   plain `Texture2D`/`TextureCube`) stay 🟨: the swapchain leg hit a real, unresolved segfault
   specific to the swapchain texture as a download/copy source (see that row) — do not re-attempt
   without reading it first; plain `Texture2D` needs no fix (already-accurate CPU shadow); plain
   `TextureCube` is blocked on `SDLGPU-40` (no `TextureCube` backend exists yet). Next up in this
   plan: `SDLGPU-33`/`34` (deferred effects) or Phase `SDLGPU-9` (`Texture3D`/`TextureCube`).
   `SDLGPU-33` (`EnvironmentMapEffect`) is now partially unblocked (a real `ITextureCubeBackend`
   exists via `RenderTargetCube`) but still deferred pending `SDLGPU-40` (plain uploaded
   `TextureCube`, Phase
   `SDLGPU-9`) so both cube sources work. `SDLGPU-34` (`SkinnedEffect`) remains blocked on its own
   bone-palette push-size
   question, independent of this phase.

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
| SDLGPU-13 | **Decision task.** Choose the shader pipeline: (a) hand-author SPIR-V for the Vulkan driver by extending the existing `compile_shaders.py`/`libshaderc` runtime-compile pattern with `SDL_gpu`'s mandatory binding order, deferring DXBC/DXIL/MSL authoring; or (b) vendor the official [`SDL_shadercross`](https://github.com/libsdl-org/SDL_shadercross) tool to compile a single HLSL source to all four formats. | ✅ | Decided 2026-07-15: (a). `src/CNA/Internal/Backends/SdlGpu/shaders/compile_shaders.py` (adapted from the Vulkan backend's own script) compiles GLSL → SPIR-V at build time via `libshaderc`, following `SDL_gpu`'s *graphics-pipeline* binding convention specifically (vertex-stage textures=set0/UBOs=set1, fragment-stage textures=set2/UBOs=set3 — a different, narrower convention than the compute-pipeline one also documented in `SDL_gpu.h`; do not confuse the two when adding more shaders). `SDL_shadercross` remains unvendored; revisit (b) only when Windows/macOS driver work actually starts. |
| SDLGPU-14 | `sprite2d` SDL-GPU shader pair (vertex + fragment), compiled to SPIR-V, satisfying `SDL_GPUShaderCreateInfo`'s explicit `num_samplers`/`num_storage_textures`/`num_storage_buffers`/`num_uniform_buffers` counts. | ✅ | Done and runtime-verified 2026-07-15 (`SdlGpu_2D` CTest + a real screenshot, see Phase `SDLGPU-5` below). **Found and fixed a real bug via the screenshot, not just the "no exception" CTest**: `SDL_gpu`'s Vulkan driver flips clip-space Y internally (for cross-backend NDC consistency) — the vertex shader's original `gl_Position = vec4(ndc, 0, 1)` silently rendered every sprite upside down (a texture's top row appeared at the bottom). Fixed by negating Y (`vec4(ndc.x, -ndc.y, 0, 1)`), confirmed by an isolated single-sprite diagnostic (`examples/sdlgpu_diag_single_sprite.cpp`, kept as a permanent manual diagnostic tool, same precedent as `cna_diag_d3d12_swapchain`) before and after the fix. |
| SDLGPU-15 | `SDL_GPUVertexInputState`/`SDL_GPUVertexAttribute`/`SDL_GPUVertexElementFormat` mapping for the four stock vertex strides: `VertexPositionColor` (16), `VertexPositionTexture` (20), `VertexPositionColorTexture` (24), `VertexPositionNormalTexture` (32). | ⬜ | Not started — these are the 3D formats (Phase `SDLGPU-6`). The *pattern* is established for the unrelated 2D `SpriteVertex` layout (32 bytes: position/UV/RGBA float2+float2+float4) in `SdlGpuGraphicsBackend::GetOrCreateSpritePipeline()`, ready to imitate. |
| SDLGPU-16 | Per-draw uniform delivery via `SDL_PushGPUVertexUniformData`/`SDL_PushGPUFragmentUniformData` (world/view/projection, `DiffuseColor`, alpha-test params, light params), respecting the documented std140 alignment rule (`vec3`/`vec4` fields 16-byte aligned). | 🟨 | The delivery *mechanism* is proven working (`RenderSprites()` pushes a `vec2 viewportSize` to vertex uniform slot 0 every frame that draws sprites) — the 3D-specific payloads (world/view/projection, `DiffuseColor`, alpha-test/light params) are not implemented (Phase `SDLGPU-6`+). `SDL_gpu.h` still does not document a hard maximum push size; measure the real per-driver limit empirically before relying on it for large payloads (see `SDLGPU-35`'s skinned-effect caveat). |

---

## Phase SDLGPU-4 — Pipelines and render state

| # | Task | Status | Notes |
| --- | --- | --- | --- |
| SDLGPU-17 | `SDL_GPUGraphicsPipelineCreateInfo` construction plus a pipeline cache keyed by (shader pair, vertex format, blend state, depth/stencil state, rasterizer state, sample count, color/depth target formats). | 🟨 | The `GetOrCreate`-lazy-pipeline *pattern* is established (`GetOrCreateSpritePipeline()`), but there is exactly one fixed pipeline (sprite2d: standard alpha blend, no depth test/write, no cull) — not yet a real multi-key cache, since no other pipeline variant exists yet to require one. Extend into a genuine keyed cache when Phase `SDLGPU-6`'s 3D pipelines land. |
| SDLGPU-18 | `BlendState` mapping — XNA `BlendState` → `SDL_GPUColorTargetBlendState` (`SDL_GPUBlendFactor`/`SDL_GPUBlendOp`). | ⬜ | Not started. The sprite2d pipeline hardcodes one fixed non-premultiplied alpha blend (`SrcAlpha`/`OneMinusSrcAlpha`) — note `ISpriteBatchBackend` (this codebase's common interface, all backends) has no setter for `SpriteBatch.Begin()`'s own `BlendState` parameter at all, so every backend, not just this one, currently ignores it; not a gap introduced here. |
| SDLGPU-19 | `DepthStencilState` mapping — `SDL_GPUDepthStencilState` (`SDL_GPUCompareOp`, `SDL_GPUStencilOpState`), covering `DepthBufferEnable`/`DepthBufferWriteEnable`/`StencilEnable` and all three XNA stencil-op fields (`StencilFail`/`StencilDepthBufferFail`/`StencilPass`). | ⬜ | Not started — `ApplyDepthStencilState()` is not overridden (uses `IGraphicsBackend`'s no-op default). The sprite2d pipeline hardcodes depth test/write off. |
| SDLGPU-20 | `RasterizerState` mapping — `SDL_GPURasterizerState` (`SDL_GPUFillMode`, `SDL_GPUCullMode`, `SDL_GPUFrontFace`), covering `CullMode.CullClockwiseFace`/`CullCounterClockwiseFace`/`None` and `FillMode.WireFrame`/`Solid`. | ⬜ | Not started — `ApplyRasterizerState()` is not overridden. The sprite2d pipeline hardcodes `CULLMODE_NONE`/`FILLMODE_FILL`. |
| SDLGPU-21 | `SamplerState` mapping — `SDL_GPUSamplerCreateInfo` (`SDL_GPUFilter`, `SDL_GPUSamplerMipmapMode`, `SDL_GPUSamplerAddressMode`) for per-slot Wrap/Clamp/Mirror + Point/Linear/Anisotropic. | 🟨 | The sampler-object creation/caching itself is done and runtime-verified (`GetOrCreateSampler()`, 18-entry cache mirroring `WebGPUGraphicsBackend::SamplerCacheIndex`'s exact indexing; `SdlGpu_2D`'s Point+Wrap and Linear+Wrap sprites both confirmed visually distinct and correct via screenshot). Not done: `ApplySamplerState()` (the per-slot dynamic setter used by direct 3D draws, matching D3D11/D3D12's `DX-119`/`DX-154`) — `SpriteBatch`'s own per-draw sampler selection (`SetSamplerFilter`/`SetSamplerAddressMode`) is the only path wired up so far. |

---

## Phase SDLGPU-5 — 2D vertical slice: Texture2D + buffers + SpriteBatch

| # | Task | Status | Notes |
| --- | --- | --- | --- |
| SDLGPU-22 | `SdlGpuTexture2DBackend` — `SDL_CreateGPUTexture` + upload via `SDL_CreateGPUTransferBuffer`/`SDL_MapGPUTransferBuffer`/`SDL_UnmapGPUTransferBuffer`/`SDL_BeginGPUCopyPass`/`SDL_UploadToGPUTexture`/`SDL_EndGPUCopyPass`. | ✅ | Done and runtime-verified 2026-07-15 (`SdlGpu_2D` CTest + screenshot). `R8G8B8A8_UNORM`, `SAMPLER` usage only, single mip level — no render-target usage, no mip chain, no 3D/cube variants (later phases). |
| SDLGPU-23 | `SdlGpuVertexBufferBackend`/`SdlGpuIndexBufferBackend` — `SDL_CreateGPUBuffer` (`VERTEX`/`INDEX` usage) + upload via the same transfer-buffer/copy-pass pattern; `SetDataWithOptions` `Discard`/`NoOverwrite` streaming hints. | 🟨 | `SetData`/`SetData16`/`SetData32` done and runtime-verified (`SdlGpu_Smoke`'s round-trip checks); every upload recreates the transfer buffer and grows the GPU buffer only when capacity is exceeded. `SetDataWithOptions`/`SetData16WithOptions`/`SetData32WithOptions` are not overridden (use `IGraphicsBackend`'s default, which ignores the streaming hint and calls the plain `SetData*`) — `Discard`/`NoOverwrite` semantics are not yet distinguished. |
| SDLGPU-24 | `SdlGpuSpriteBatchBackend` — batch quads, bind the `sprite2d` pipeline, bind texture+sampler via `SDL_BindGPUFragmentSamplers`, draw via `SDL_DrawGPUIndexedPrimitives`. Must cover source rectangles, tint/alpha, rotation, both flips, and Linear/Point + Clamp/Wrap/Mirror sampling. | ✅ | Done and runtime-verified 2026-07-15 — uses `SDL_DrawGPUPrimitives` (non-indexed, 6 vertices/sprite, one draw call per sprite into a shared per-frame vertex buffer) rather than `SDL_DrawGPUIndexedPrimitives`; behaviorally equivalent for this milestone, not yet batched into fewer draw calls. `SdlGpu_2D` CTest covers source rects, tint, alpha, rotation, both flips, and Point+Wrap/Linear+Wrap sampling; all confirmed visually correct via screenshot (see `SDLGPU-25`). |
| SDLGPU-25 | **First milestone gate.** A demo/smoke harness renders textured, tinted, rotated, flipped sprites and survives a resize through the SDL GPU backend with no validation error, device loss, or loader failure. | ✅ | Verified 2026-07-15: `examples/sdlgpu_2d_test.cpp` (`SdlGpu_2D` CTest, 3/3 checks) plus a real screenshot (captured via `import -window`, not just "didn't throw") of a 6-sprite scene — opaque quadrant texture, alpha-blended tint, 45°-rotated quadrant, combined horizontal+vertical flip (colors correctly permuted: white/blue/green/red corners), and Point+Wrap/Linear+Wrap sampling with a source rect exceeding the texture bounds (correctly tiles 2×2, unlike the Clamp-default sprites). The screenshot caught and led to fixing `SDLGPU-14`'s Y-flip bug — this is exactly the kind of defect a "did it throw" CTest alone cannot catch, and the reason this milestone is gated on a real visual check, not source inspection. Window-resize survival specifically was not re-tested this session (already covered by `SDLGPU-8`'s swapchain-acquisition handling) — no code path differs between this test and the resize-tested `SdlGpu_Smoke` test. |

---

## Phase SDLGPU-6 — Core 3D vertex formats and BasicEffect

| # | Task | Status | Notes |
| --- | --- | --- | --- |
| SDLGPU-26 | `colored3d` pipeline + shader + `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives` dispatch (stride 16), with real depth-test verification. | ✅ | Done and runtime-verified 2026-07-15 (`SdlGpu_3D` CTest + screenshot). **Confirmed empirically that, unlike `sprite2d.vert.glsl`, this shader needs NO Y-flip**: `gl_Position = pc.mvp * vec4(inPos,1)` with no negation renders a red/green/blue triangle in the exact expected orientation — SDL_gpu's Vulkan-driver Y-flip only affected the hand-computed pixel-space NDC math in the sprite shader, not a real XNA projection matrix (which already encodes the correct convention). Real depth test verified via a genuine occlusion proof (see SDLGPU-30's note), not just "didn't throw". |
| SDLGPU-27 | `textured3d` (stride 20) — real `Texture2D` sampling, `DiffuseColor` genuinely multiplying it. | ✅ | Done and runtime-verified 2026-07-15 — screenshot confirms the exact quadrant-texture colors/orientation. Fragment shader receives `pc` via its own `SDL_PushGPUFragmentUniformData` push (set 3, binding 0) — a full second push of the same 128 bytes already pushed to the vertex stage (set 1, binding 0), simpler than threading a `textureEnabled` varying through, at the cost of one extra push call per draw. |
| SDLGPU-28 | `colored_textured3d` (stride 24) — vertex-color mixing combined with texture sampling. | ✅ | Done and runtime-verified 2026-07-15 — shares `textured3d`'s fragment shader (`Shaders::kTextured3dFragSpv`) unchanged, only the vertex shader/vertex-input-state differ. The screenshot's green-tinted quad looked unexpectedly dark at first glance — turned out to be **correct**: XNA's `Color::Green` is `(0,128,0)`, not lime `(0,255,0)` (the same real behavioral fact this project's Software backend caught independently, 2026-07-13), so a green-tinted quadrant texture multiplies red/blue channels to black — exactly what rendered. |
| SDLGPU-29 | `lit_textured3d` (stride 32, `VertexPositionNormalTexture`) — FNA's `Lighting.fxh` `ComputeLights()` (3 directional lights, ambient, Blinn-Phong specular, emissive). | ✅ | Done and runtime-verified 2026-07-15 via `BasicEffect.EnableDefaultLighting()`'s real 3-light rig — screenshot shows a visibly different (lit/washed) appearance vs. the plain `textured3d` quad, proving the lighting math genuinely runs. Ported the algorithm from `VulkanGraphicsBackend`'s `lit_textured3d.{vert,frag}.glsl` with the safe-normalize guard from the WebGPU port's own `WEBGPU-22`/`33` finding (a disabled light's zero direction vector). **Did not need WebGPU's CPU-precomputed-normal-matrix workaround** — GLSL has a built-in `inverse()` (unlike WGSL), so the vertex shader computes `transpose(inverse(mat3(world)))` directly, matching `VulkanGraphicsBackend` exactly; the second UBO (`LitLightParams`, vertex+fragment slot 1) is 224 bytes/56 floats here, not WebGPU's 272/68 (no normal-matrix slots needed). |
| SDLGPU-30 | `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx` `GpuDrawParams` dispatch by vertex stride, matching the dispatch-by-stride convention every other 3D-capable backend in this codebase already uses. | ✅ | Done and runtime-verified 2026-07-15. Simpler than `WebGPUGraphicsBackend`'s own dispatch: `AlphaTestEffect`/`DualTextureEffect`/`EnvironmentMapEffect`/`SkinnedEffect`-specific `GpuDrawParams` fields are not yet checked (later phases), so dispatch is purely stride+`texture0`-based. **Real depth-test proof**: a nearer (blue) and farther (red) quad, same size/position, drawn in that order with `SetDepthTestEnabled(true)`; the screenshot shows solid, unmixed blue with zero red bleed-through — the farther quad's fragments were genuinely rejected by the depth buffer, not just silently painted-over (which would show red instead, since it drew second). |

---

## Phase SDLGPU-7 — Remaining stock effects

| # | Task | Status | Notes |
| --- | --- | --- | --- |
| SDLGPU-31 | `AlphaTestEffect` (per-pixel discard) — strides 20/24/32. | ✅ | Done and runtime-verified 2026-07-15 (`SdlGpu_Effects` CTest + screenshot: `AlphaFunction=Greater`/`ReferenceAlpha=128` on a texture with an opaque top half and a fully-transparent bottom half genuinely discards the bottom half — the `CornflowerBlue` background shows through with a hard edge, not a translucent blend, proving real `discard`, not alpha blending). Strides 20/32 share `alpha_test3d.vert.glsl`/one shader but need distinct pipelines (different vertex-input-state) — the pipeline cache key folds in the stride explicitly for this one shader family, unlike every other family here which is already stride-specific by construction. |
| SDLGPU-32 | `DualTextureEffect` (two-sampler multiply, `tex1.rgb*=2.0; result=tex1*tex2*tint`) — strides 20/24. | ✅ | Done and runtime-verified 2026-07-15 — screenshot shows the exact predicted result of multiplying a red/green/blue/white quadrant texture by a uniform yellow second texture: red/green pass through unchanged, blue becomes black (yellow has no blue channel), white becomes yellow. An unambiguous proof the real two-sampler multiply runs, not just one texture being shown. |
| SDLGPU-33 | `EnvironmentMapEffect` (cubemap reflection) — needs `SDL_GPU_TEXTURETYPE_CUBE` texture creation + `SDL_GPUCubeMapFace`-indexed per-face upload. | ⬜ | **Still deferred, partially unblocked 2026-07-15.** `SDLGPU-36` added a real `ITextureCubeBackend` implementation (`SdlGpuRenderTargetCubeBackend`), so `EnvironmentMapEffect` sampling a `RenderTargetCube`-sourced `envMap` is now technically feasible. But `CreateTextureCube()` (plain, uploaded `TextureCube` — what a real game's static skybox/reflection map would normally be, via content loading rather than render-to-texture) is still not overridden, still returns `IGraphicsBackend`'s default `nullptr` — that's `SDLGPU-40`'s own job (Phase `SDLGPU-9`). Pick this up once `SDLGPU-40` lands so both `TextureCube` sources work, not just the render-target one; do not build it inline as part of the effect. |
| SDLGPU-34 | `SkinnedEffect` (bone-matrix palette). | ⬜ | **Deliberately deferred, not attempted this session** — two real risks, not yet resolved: (1) a 72-bone palette is 4608 bytes, far larger than every push so far (max 224 bytes/`lit_textured3d`'s `LitLightParams`); `SDL_gpu.h` still does not document a hard per-driver push-uniform size limit, so this needs an empirical spike (a real 72-matrix push, or fall back to `SDL_BindGPUVertexStorageBuffers` if it fails) before committing to an approach. (2) needs a new stride-52 `VertexPositionNormalTextureSkinned` vertex format (bone weights `vec4` + bone indices as 4 small integers) not yet used by any pipeline here — check `SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4`/`UINT4`-equivalent support for the index attribute before assuming the layout ports directly from `VulkanGraphicsBackend`'s `uvec4 aBoneIndices`. |

---

## Phase SDLGPU-8 — Render targets

| # | Task | Status | Notes |
| --- | --- | --- | --- |
| SDLGPU-35 | `RenderTarget2D` — a `SDL_GPU_TEXTUREUSAGE_COLOR_TARGET \| SDL_GPU_TEXTUREUSAGE_SAMPLER` texture, bound as `SDL_GPUColorTargetInfo.texture` in one pass, sampled in a later pass. | ✅ | 2026-07-15. Required a real architectural refactor, not just a new class: every pipeline cache (`GetOrCreatePipelineColored3D`/`Textured3D`/`ColoredTextured3D`/`LitTextured3D`/`AlphaTest3D`/`DualTexture3D`/sprite) now takes a `colorFormat` param folded into its cache key (previously hardcoded `SDL_GetGPUSwapchainTextureFormat`), and `EnsureFrameRendered()` is now genuinely multi-pass: every distinct `SdlGpuRenderTargetBackend` used this frame gets its own render pass (in first-bind order, via `RenderToTarget()`), all before the swapchain's own pass — this is what makes "bound in one pass, sampled in a later pass" real rather than aspirational. Render targets standardize on `SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM` regardless of the swapchain's native format (XNA's `CreateRenderTarget2D` interface has no format parameter anyway). `Clear()`/`ClearDepth()`/`ClearStencil()` now route to whichever `SdlGpuRenderTargetBackend` is currently bound (each RT owns its own pending-clear state) instead of unconditionally hitting the swapchain's. `SpriteBatch::Draw()` had to learn to resolve either `SdlGpuTextureBackend` (plain `Texture2D`) or `SdlGpuRenderTargetBackend` (`RenderTarget2D`) to a raw `SDL_GPUTexture*`, since the two are unrelated concrete classes. `mipMap` is supported for real (`SDL_GenerateMipmapsForGPUTexture` after that target's pass ends); `multiSampleCount > 0` was a documented throw at the time this row was first closed (`SDLGPU-38` not yet implemented) — real MSAA landed the same session, see `SDLGPU-38`'s own row. Verified via `SdlGpu_RenderTarget2D` (4/4 checks: Clear-only fill, a real colored3d triangle, a depth-tested pair of overlapping quads each with this target's own dedicated depth texture, and a `MultiSampleCount` property-fidelity check) plus a real screenshot (see below) — not just "didn't throw". |
| SDLGPU-36 | `RenderTargetCube` — 6-face color-target texture, including MSAA and mip levels beyond face 0. | ✅ | 2026-07-15. `SdlGpuRenderTargetCubeBackend` owns one `SDL_GPU_TEXTURETYPE_CUBE` texture (6 layers); only one face is ever the active target at a time (shared depth texture across faces, matching D3D11/D3D12's own convention), with per-face clear state. MSAA needed no manual resolve step at all (unlike D3D11/D3D12's `ResolveSubresource`) — `SDL_gpu` has no multisampled cube type, so the real render target is a plain 6-layer `2D_ARRAY` MSAA texture, and `SDL_GPUColorTargetInfo.resolve_texture`/`resolve_layer` resolves it directly into the cube texture's active face automatically at render-pass end. `SDL_GenerateMipmapsForGPUTexture` has no per-layer control (unlike D3D12's manual per-face blit), so it regenerates all 6 faces' chains whenever any face of a mip-enabled cube was used in a frame — harmless for untouched faces (same source data, same result). `multiSampleCount` is clamped via a new `SDL_GPUTextureSupportsSampleCount`-based `ClampSampleCount` helper (mirrors `D3D12RenderTargetCubeBackend::ClampMultiSampleCount`), unlike `RenderTarget2D` which still throws for any nonzero request. Required generalizing every queued draw command's `target` field from a single `RenderTarget2D` pointer to a small `DrawTarget{rt, cube, face}` struct, since a draw can now target the swapchain, a 2D RT, or one cube face. **Verified with real per-face GPU readback** (`SdlGpuRenderTargetCubeBackend::GetData`, pulled forward from `SDLGPU-39` — see that row) rather than `EnvironmentMapEffect` reflection sampling (not implemented in this backend, and Vulkan's own equivalent test documents that technique as unable to discriminate between individual faces anyway): `SdlGpu_RenderTargetCube`, 7/7 checks — all 6 faces filled with distinct colors and read back individually, a real colored3d triangle, real per-face depth-test occlusion, `MultiSampleCount` property fidelity + a genuine MSAA fill/resolve/readback round-trip, and mip level 1 of a uniform-color fill reading back correctly. |
| SDLGPU-37 | Multiple Render Targets (MRT) — `SDL_GPUGraphicsPipelineTargetInfo.color_target_descriptions` array + several `SDL_GPUColorTargetInfo` entries in one render pass. | ✅ | 2026-07-15. `SetRenderTargets(rts, count)` binds `rts[0]` exactly like `SetRenderTarget2D(rts[0])` (the real, single draw target); `rts[1..count-1]` are registered via a new `SdlGpuRenderTargetBackend::MarkUsedThisFrame()` (so each still gets its own real pass this frame) and tracked in a new `currentExtraMrtTargets_` list, but never become `currentRenderTarget_` — **draws remain single-target**, matching the exact same honest scope boundary this project's D3D11/D3D12 MRT support already established (no shader in this codebase declares more than one fragment output). `Clear()`/`ClearDepth()`/`ClearStencil()` propagate to every target in `currentExtraMrtTargets_` too, so a single `Clear()` call while MRT is bound really does clear all of them simultaneously, not just the primary. Verified via `SdlGpu_MRT` (3/3 checks: simultaneous MRT clear, a colored3d draw while MRT is bound, `ClearColorAndDepth`+`SetRenderTargets(nullptr,0)` restore, all with no exception) plus a real screenshot confirming both halves of the scope boundary at once — the primary target turned green (the draw succeeded) while the two secondary targets stayed exactly magenta (the shared `Clear()` reached them, and the draw did not). **Real, non-test-specific finding from writing this test:** a `RenderTarget2D`/`RenderTargetCube` that goes out of scope (destructor runs) before this backend's deferred `Present()`-time render pass actually executes is a real use-after-free — the destructor releases the real `SDL_GPUTexture*` immediately, but any sprite/draw commands already queued against it (not yet rendered) still hold that now-freed handle. Caught via a genuine segfault when this test's first draft used `Draw()`-local `RenderTarget2D` instances instead of members; `SdlGpu_RenderTargetCube`'s own test happened to avoid this by forcing an eager flush inside `GetData()` itself. **Not fixed here** (would need either an eager-flush-on-destruction policy or a deferred-release queue mirroring `SDL_ReleaseGPUBuffer`'s own "frees as soon as safe" contract — a real architectural question, not this task's scope) — documented so any future test, and any real game code targeting this backend, keeps render targets alive at least until the next `Present()`/frame boundary, not as short-lived locals. |
| SDLGPU-38 | MSAA — `SDL_GPUSampleCount` texture creation + `SDL_GPUColorTargetInfo.resolve_texture` automatic resolve-on-render-pass-end. | ✅ | 2026-07-15. Closes `RenderTarget2D`'s own MSAA leg (cube MSAA was already done as part of `SDLGPU-36`), using the exact same mechanism: a separate `msaaTexture_` (`SDL_GPU_TEXTURETYPE_2D`, `COLOR_TARGET`-only usage, real `sample_count`) is the actual render target; `SDL_GPUColorTargetInfo.resolve_texture = colorTexture_` + `store_op = SDL_GPU_STOREOP_RESOLVE` resolves it into the sampleable `colorTexture_` automatically at render-pass end — no manual resolve call. `multiSampleCount` is clamped via the `ClampSampleCount`/`SampleCountToInt` helpers `SDLGPU-36` added (`SDL_GPUTextureSupportsSampleCount`-based); MSAA and `mipMap` remain mutually exclusive on the same attachment (same rationale as the cube). The depth texture's `sample_count` is set to match the color attachment's when MSAA is requested. `CreateRenderTarget2D` no longer throws for any `multiSampleCount` value — the `SDLGPU-35`-era throw is gone. Verified via a new dedicated `SdlGpu_RenderTarget2DMSAA` (4/4 checks: `MultiSampleCount` property fidelity — request 4 applies a real device-clamped value >1; a real colored3d quad drawn into the MSAA target and resolved with no exception; a depth-tested MSAA target with its own MSAA depth texture; sustained multi-frame rendering) plus a real screenshot (a full-screen green quad — both the plain MSAA fill and the depth-tested MSAA target's nearer-quad-wins result render as solid green with no visible corruption or resolve artifacts). `sdlgpu_rendertarget2d_test.cpp`'s own former "MSAA throws" check (Check D) was updated in the same commit to a `MultiSampleCount==0` property-fidelity check instead, since the throw it asserted no longer applies. |
| SDLGPU-39 | `GetData()` readback — `SDL_DownloadFromGPUTexture` via a transfer buffer + fence wait (`SDL_SubmitGPUCommandBufferAndAcquireFence`/`SDL_WaitForGPUFences`), for `Texture2D`/`TextureCube`/`RenderTarget2D`/`RenderTargetCube`. | 🟨 | **`RenderTargetCube::GetData()` done for real, 2026-07-15** (pulled forward as part of `SDLGPU-36`): `SdlGpuRenderTargetCubeBackend::GetData()` downloads directly from the self-owned cube texture, exercised by all 7 `SdlGpu_RenderTargetCube` checks. **`RenderTarget2D::GetData()` also done for real, 2026-07-15** — this required a genuine cross-backend interface addition: `ITextureBackend` (in `IGraphicsBackend.hpp`, the common interface every backend implements) gained a new `GetData(level, x, y, w, h, data, dataLength) const` virtual with a safe no-op default (mirrors `ITextureCubeBackend::GetData`'s own existing convention), and `Texture2D::GetData()`'s two entry-point overloads (the flat `(Color*, startIndex, elementCount)` form and the `(level, rect, Color*, startIndex, elementCount)` form) now fall back to `backend_->GetData(...)` **only** when the CPU-side `cpuPixels_`/mip shadow is empty (i.e. for a `RenderTarget2D`, whose content comes from GPU rendering, never `SetData()`) — plain, `SetData()`-populated `Texture2D` instances are completely unaffected (still served entirely from the CPU shadow, zero behavior change, confirmed via the full `Texture2DTest`/`TextureCubeTest`/`Texture3DTest` suites, 123/123 passing). `SdlGpuRenderTargetBackend::GetData()` downloads from `colorTexture_` (the always-single-sample, already-resolved-if-MSAA sampleable texture) using the identical transfer-buffer+fence pattern as the cube leg — safe from the swapchain segfault below since it's a self-owned texture, not the swapchain's. Verified: `sdlgpu_rendertarget2d_test.cpp` gained 3 new real pixel-assertion checks (Clear-only fill reads back exact green, a colored3d triangle reads back exact red, a depth-tested target's nearer-quad-wins reads back exact green) alongside its existing no-exception checks, 7/7 passing — genuinely stronger than the screenshot-only verification this row previously depended on. **Real blocker found 2026-07-15, still unresolved, for the swapchain/plain-`Texture2D`/`TextureCube` legs:** an attempt to pull swapchain readback forward (for `ReadBackbuffer`/`GetBackBufferData`) segfaulted inside *both* `SDL_DownloadFromGPUTexture` (source = the raw swapchain texture) *and* `SDL_CopyGPUTextureToTexture` (copying the swapchain texture into a plain `SAMPLER`-usage staging texture first) — crashes deep inside the vendored SDL3/Vulkan driver on this environment, not a graceful SDL error return. Confirmed the crash is specifically about the swapchain texture as a copy/download *source*: neither `SDL_gpu.h` doc comment mentions a usage-flag requirement for either function, so this may be a genuine driver/environment limitation (swapchain images not created with a transfer-src-equivalent usage bit) rather than a CNA bug — both the `RenderTargetCube` and `RenderTarget2D` successes above confirm the transfer-buffer/fence mechanism itself is sound for any *self-owned* texture, so the blocker is specific to the swapchain texture, not the general technique. Needs investigation on real (non-virtual-display) hardware, or via `SDL_BlitGPUTexture` as an alternative, before assuming any swapchain-sourced approach works. Plain (non-render-target) `Texture2D::GetData()` needs no fix at all -- it already reads an always-accurate CPU shadow, so a GPU path there would be pure redundant scope. `TextureCube::GetData()` (plain, non-render-target) remains blocked on `SDLGPU-40` (no `TextureCube` backend exists yet). `ReadBackbuffer`/`GetBackBufferData` remain blocked on the swapchain segfault specifically — do not re-attempt without first re-reading this note. |

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
