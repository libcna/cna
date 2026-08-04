# Wicked Engine Backend Implementation Plan

> **Status legend:** ✅ implemented *and verified against its stated acceptance criteria*;
> 🟨 code or documentation exists but has not met those criteria; ⬜ not implemented.

CNA's fifteenth graphics backend (`CNA_GRAPHICS_BACKEND=WICKED`) is built on
[Wicked Engine](https://github.com/turanszkij/WickedEngine)'s render hardware interface,
`wi::graphics::GraphicsDevice`, which itself dispatches to Vulkan (Linux/Windows) or D3D12
(Windows).

**What exists today (2026-08-04):** a first native baseline on Linux/Vulkan — device and swap-chain
creation from CNA's own SDL3 window, an off-screen scene target with letterbox/overscan/stretch
presentation, colour/depth/stencil clears, `Texture2D` upload and readback, vertex/index buffers,
a batched `SpriteBatch`, the four stock stride-dispatched 3D shader variants (16/20/24/32) with
diffuse/vertex-colour/alpha-test/fog/three-directional-light shading, `RenderTarget2D`, full
blend/depth-stencil/rasterizer/sampler state mapping, real GPU occlusion queries, MSAA on the scene
target, back-buffer readback, `TextureCube` and `Texture3D` storage, and instanced draws. This is
**not** parity with the Vulkan or EasyGL backends — see "Remaining work" below for exactly what is
missing.

---

## Design decisions

1. **Only the RHI layer of Wicked Engine is consumed.** CNA uses `wi::graphics::GraphicsDevice`
   and `wi::shadercompiler` and nothing above them. `wi::renderer`, `wi::scene`, `wi::Application`,
   `wi::input`, the Jolt physics integration and the Lua scripting layer are all linked (they are
   part of the same static library) but never called: CNA already owns the XNA 4.0 programming
   model above this boundary and needs a device abstraction below it, not a second engine.

2. **Wicked Engine is patched for SDL3.** Upstream's Unix platform layer is SDL2-only —
   `wiPlatform.h` calls SDL2 window functions unconditionally under `PLATFORM_LINUX`, and
   `GraphicsDevice_Vulkan::CreateSwapChain` has a hard `#error PLATFORM NOT SUPPORTED` when neither
   `_WIN32` nor `SDL2` is defined. CNA is SDL3-only, and SDL2 and SDL3 cannot be loaded into one
   process (they export the same symbol names with different ABIs), so there is no configuration in
   which unpatched upstream and CNA can share a window. `cmake/patches/wicked-sdl3-platform.patch`
   adds a parallel `SDL3` branch everywhere the `SDL2` one already exists — six files, no
   behavioural change to the SDL2 path — following the same pinned-revision + patch discipline the
   BGFX backend already uses (`cmake/patches/bgfx-max-render-target-msaa.patch`).

3. **Native only.** Wicked Engine has no WebGPU/WebGL device, so an Emscripten configuration is a
   hard `FATAL_ERROR` rather than a warning. Linux and Windows are the supported targets; the
   backend is developed and verified against Linux/Vulkan.

4. **Shaders are HLSL, compiled at runtime through `wi::shadercompiler`.** One source produces
   SPIR-V for the Vulkan device and DXIL for D3D12, so the backend never carries per-device shader
   binaries. Because `wi::shadercompiler::Compile()` reads its input from a file path, the source is
   embedded in `WickedShaderSources.hpp` and materialised into a private temporary directory at
   device creation — a CNA game therefore needs no shader-asset deployment step at all.
   `wi::shadercompiler` loads `./libdxcompiler.so` **relative to the current working directory**,
   which is a real deployment constraint documented in `docs/wicked-backend.md`.

5. **Game rendering goes to an off-screen scene target, not straight to the swap chain.** Wicked
   Engine's swap-chain render pass acquires an image and always clears, so it can be entered exactly
   once per frame. Rendering into an off-screen colour target and blitting it at `Present()` is what
   lets CNA switch freely between the back buffer and a `RenderTarget2D` mid-frame, and it is the
   same texture the virtual-resolution/letterbox presentation needs anyway.

6. **Unsupported draws are refused, never approximated.** `EnvironmentMapEffect`, `SkinnedEffect`,
   `PbrEffect`, MRT, `RenderTargetCube` and an unexpressible instance-step-rate all throw at the
   call site rather than rendering an unlit, single-target or wrong-rate stand-in that would look
   like a working draw. `SupportsCapability()` reports the same set, so callers can ask instead of
   catching.

---

## Task table

### Phase W1 — build integration

| ID | Task | Status |
|----|------|--------|
| WICKED-1 | `CNA_GRAPHICS_BACKEND=WICKED` / `CNA_BACKEND_WICKED` selection, backend dir + target | ✅ |
| WICKED-2 | `cmake/ThirdPartyWicked.cmake`: resolve `CNA_WICKED_ROOT`, optional pinned FetchContent, disable editor/tests/samples, link `WickedEngine` | ✅ |
| WICKED-3 | `cmake/patches/wicked-sdl3-platform.patch`: SDL3 branch in `wiPlatform.h` and `wiGraphicsDevice_Vulkan.cpp`, the three `CMakeLists.txt`, FAudio's `FAUDIO_SDL3_PLATFORM`, and an inert `wiInput.cpp` cursor table (6 files, 94 added lines) | ✅ |
| WICKED-4 | Hard Emscripten gate, Linux/Windows platform note | ✅ |
| WICKED-5 | Copy `libdxcompiler.so` next to the built backend | ✅ |
| WICKED-6 | Pin the Wicked revision (`CNA_WICKED_COMMIT`) the patch is authored against | ✅ |
| WICKED-7 | `CNA::GraphicsBackendType::Wicked` + `"WICKED"` name mapping, and its shared test's switch | ✅ |

### Phase W2 — device, swap chain, presentation

| ID | Task | Status |
|----|------|--------|
| WICKED-10 | `GraphicsDevice_Vulkan` creation from CNA's SDL3 window | ✅ |
| WICKED-11 | Swap-chain creation, VSync from `PresentInterval`, resize recreation | ✅ |
| WICKED-12 | Off-screen scene colour/depth target at the virtual resolution | ✅ |
| WICKED-13 | Letterbox / Overscan / Stretch / NativeBackBuffer / FixedHeightDynamicWidth present blit | ✅ |
| WICKED-14 | `TransformWindowToLogical` / `TransformLogicalToWindow` | ✅ |
| WICKED-15 | `SetVirtualResolution` / `SetPresentationMode` / `SetSwapInterval` at runtime | ✅ |
| WICKED-16 | MSAA scene target with resolve, device-clamped `ApplyMultiSampleCount` | ✅ |
| WICKED-17 | `ReadBackbuffer` via a READBACK texture copy | ✅ |
| WICKED-18 | Real end-to-end run on a GPU host (window, present, no validation error) | ⬜ |

### Phase W3 — resources

| ID | Task | Status |
|----|------|--------|
| WICKED-20 | `Texture2D` creation from `ImageData` | ✅ |
| WICKED-21 | `UpdatePixels` / `UpdatePixelsLevel` | ✅ |
| WICKED-22 | `Texture2D` readback (`GetData`) | ✅ |
| WICKED-33 | Shared `UploadTextureRegion`/`ReadbackTextureRegion` helpers behind every 2D/cube/3D/render-target transfer | ✅ |
| WICKED-23 | Vertex buffers (UPLOAD-mapped, stride-tagged) | ✅ |
| WICKED-24 | 16- and 32-bit index buffers | ✅ |
| WICKED-25 | `RenderTarget2D` (colour + optional depth/stencil, `RenderTargetUsage`) | ✅ |
| WICKED-26 | `RenderTarget2D` readback | ✅ |
| WICKED-27 | Real GPU occlusion queries (`GPUQueryHeap` + readback buffer) | ✅ |
| WICKED-28 | Mip-chain allocation for `Texture2D` (allocated + uploadable per level); automatic GENERATION still absent | 🟨 |
| WICKED-29 | `TextureCube` (six-face upload + readback; not yet sampleable — no env-map shader, WICKED-56) | ✅ |
| WICKED-30 | `Texture3D` (volume upload + readback, real GPU storage) | ✅ |
| WICKED-31 | Batch texture uploads instead of submit-and-wait per call | ⬜ |
| WICKED-32 | `SetDataOptions` (`Discard` / `NoOverwrite`) honoured on buffer uploads | ⬜ |

### Phase W4 — pipeline, state and draws

| ID | Task | Status |
|----|------|--------|
| WICKED-40 | Pipeline-state cache keyed on the complete render state (`WickedPipelineKey`) | ✅ |
| WICKED-41 | Blend state incl. per-MRT-slot write masks and the coverage sample mask | ✅ |
| WICKED-42 | Depth/stencil state incl. two-sided stencil and standalone `ReferenceStencil` | ✅ |
| WICKED-43 | Rasterizer state incl. wireframe, cull mode and depth bias | ✅ |
| WICKED-44 | Sampler cache (filter / address modes / anisotropy); per-slot state recorded by `ApplySamplerState` and bound at draw time, since XNA sets it outside a draw | ✅ |
| WICKED-45 | Stride-dispatched input layouts and vertex shaders for strides 16/20/24/32 | ✅ |
| WICKED-46 | `DrawColoredPrimitives` / `DrawIndexedColoredPrimitives` | ✅ |
| WICKED-47 | `DrawPrimitivesEx` / `DrawIndexedPrimitivesEx` with real `GpuDrawParams` dispatch | ✅ |
| WICKED-48 | `BasicEffect` shading: diffuse, vertex colour, texture, three directional lights, specular, emissive, ambient | ✅ |
| WICKED-49 | `AlphaTestEffect` (FNA's four-component alpha-test comparison) | ✅ |
| WICKED-50 | `DualTextureEffect` (second sampler slot) | ✅ |
| WICKED-51 | Fog (FNA's `EffectHelpers.SetFogVector` fog vector) | ✅ |
| WICKED-52 | MSAA `RenderTarget2D` readback (needs an explicit resolve) | ⬜ |
| WICKED-53 | `DrawInstancedPrimitivesEx` (per-instance 64-byte `Matrix` stream at input slot 1, four instanced VS variants); `InstanceFrequency != 1` refused — Wicked's `InputLayout` has no step-rate field | ✅ |
| WICKED-54 | Multiple simultaneous render targets | ⬜ |
| WICKED-55 | `RenderTargetCube` | ⬜ |
| WICKED-56 | `EnvironmentMapEffect`, `SkinnedEffect`, `PbrEffect` shader variants | ⬜ |
| WICKED-57 | Custom `ShaderEffect` (`CreateEffectBackend`) | ⬜ |
| WICKED-58 | Multi-stream vertex input (`GraphicsCapability::MultiStreamVertexInput`) | ⬜ |
| WICKED-59 | Strides 48/52/56/68 (tangent/skinned layouts) | ⬜ |
| WICKED-60 | D3D12 device selection (needs Wicked's root-signature macro in CNA's HLSL) | ⬜ |

### Phase W5 — 2D

| ID | Task | Status |
|----|------|--------|
| WICKED-65 | `SpriteBatch`: batched quads, tint, rotation, origin, flip, layer depth | ✅ |
| WICKED-66 | Per-batch sampler filter and address mode | ✅ |
| WICKED-67 | `SpriteBatch` transform matrix | ✅ |
| WICKED-68 | Custom `Effect` passed to `SpriteBatch.Begin()` | ⬜ |

### Phase W6 — tests and documentation

| ID | Task | Status |
|----|------|--------|
| WICKED-70 | Device-independent unit coverage of the pipeline cache key | ✅ |
| WICKED-71 | `cmake/Tests/WickedTests.cmake` registration | ✅ |
| WICKED-72 | `docs/wicked-backend.md` capability boundary | ✅ |
| WICKED-73 | Backend row in `docs/graphics-backend-feature-matrix.md` | ✅ |
| WICKED-74 | GPU smoke test (`cna_demo_2d --smoke`) verified on a real display | ⬜ |
| WICKED-75 | Pixel-asserted readback tests (clear colour, sprite, 3D draw) | ⬜ |
| WICKED-76 | Cross-backend pixel-parity comparison against EasyGL/Vulkan | ⬜ |

---

## Remaining work (read this first)

- **No verified run on real hardware yet (`WICKED-18`, `WICKED-74`).** The backend compiles and
  links against a patched Wicked Engine, but this development environment has no GPU, no Vulkan
  loader and no display, so nothing here has been executed. Every ✅ above means "implemented and
  compiles"; only `WICKED-70`'s unit test has actually been run. Treat the first run on a GPU host
  as the next task, not as a formality.
- **3D effect coverage stops at `BasicEffect`/`AlphaTestEffect`/`DualTextureEffect`**
  (`WICKED-56`). `EnvironmentMapEffect`, `SkinnedEffect` and `PbrEffect` throw.
- **No MRT, no `RenderTargetCube`, no custom `ShaderEffect`** (`WICKED-54`/`55`/`57`/`68`). Each is
  refused explicitly and reported through `SupportsCapability()`.
- **A `TextureCube` cannot yet be sampled** (`WICKED-29`). Its storage, upload and readback are
  real, but `EnvironmentMapEffect` — the only thing that binds one — still throws (`WICKED-56`).
- **Instancing accepts only `InstanceFrequency == 1`** (`WICKED-53`). Wicked Engine's `InputLayout`
  carries no instance-step-rate field, so any other frequency is refused rather than silently
  drawn at rate 1. The per-instance record must be CNA's 64-byte column-major `Matrix`.
- **Texture uploads submit and wait per call** (`WICKED-31`). Correct, but a per-frame
  `Texture2D.SetData` pattern will stall; batching is a real follow-up.
- **Vertex/index buffers are written straight into UPLOAD memory** (`WICKED-32`). `SetDataOptions`
  is ignored, so overwriting a buffer the GPU may still be reading is possible; the same shape as
  several other CNA backends, but it should become a proper ring or staged copy.
- **Mip chains are allocated but not generated** (`WICKED-28`). Every declared level exists and can
  be uploaded through `SetData`, but nothing downsamples level 0 into the rest.
- **D3D12 is not selectable** (`WICKED-60`). Wicked's HLSL6 path compiles with
  `-rootsig-define WICKED_ENGINE_DEFAULT_ROOTSIGNATURE`, which CNA's own shader source does not
  declare, so the Vulkan device is chosen on every platform.

---

## Boundaries

- CNA never calls into Wicked Engine above `wi::graphics` / `wi::shadercompiler`. If a future task
  needs something from `wi::renderer` or `wi::scene`, that is a design change to be agreed first,
  not an incremental step.
- The SDL3 patch is deliberately minimal and additive. It must not change behaviour on the SDL2
  path, so that rebasing it onto a newer Wicked revision stays mechanical.
- The established backends are untouched: every change for this backend is either backend-local or
  in the shared CMake selection/linking lists.
