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
target, back-buffer readback, `TextureCube` and `Texture3D` storage, `EnvironmentMapEffect` cube
reflections, `RenderTargetCube`, `SkinnedEffect`, `PbrEffect`/`SkinnedPbrEffect`, the full stock
stride set (16/20/24/32/48/52/56/68), and instanced draws. This is
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

6. **Unsupported draws are refused, never approximated.** An unexpressible instance-step-rate,
   a skinned draw on a layout without blend weights, a PBR draw on a layout without a tangent, and
   an instanced draw on a wider stride all throw at the call site rather than rendering an unlit,
   bind-pose, single-target or wrong-rate stand-in that would look like a working draw.
   `SupportsCapability()` reports the same set, so callers can ask instead of catching.

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
| WICKED-28 | Mip chains for `Texture2D` — every declared level filled with a real 2x2 box-filtered reduction, computed on the CPU at creation | ✅ |
| WICKED-29 | `TextureCube` (six-face upload + readback, sampled by `EnvironmentMapEffect`) | ✅ |
| WICKED-30 | `Texture3D` (volume upload + readback, real GPU storage) | ✅ |
| WICKED-31 | Texture uploads no longer stall — the staging resource rides Wicked's deferred-destruction queue instead of a submit-and-wait | ✅ |
| WICKED-32 | `SetDataOptions` honoured — vertex/index buffers are region-orphaned on `Discard`/`None`, written in place on `NoOverwrite` | ✅ |

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
| WICKED-52 | MSAA `RenderTarget2D` resolve — single-sample resolve destination written by the render pass, used for both readback and sampling | ✅ |
| WICKED-53 | `DrawInstancedPrimitivesEx` (per-instance 64-byte `Matrix` stream at input slot 1, four instanced VS variants); `InstanceFrequency != 1` refused — Wicked's `InputLayout` has no step-rate field | ✅ |
| WICKED-54 | Multiple simultaneous render targets (up to 4, shared depth from slot 0, size-mismatched sets refused) | ✅ |
| WICKED-55 | `RenderTargetCube` (per-face RTV subresources, whole-cube SRV, sampleable by `EnvironmentMapEffect`) | ✅ |
| WICKED-56 | `EnvironmentMapEffect` (cube reflections, flat and Fresnel-weighted, dedicated VS/PS pair matching the established CNA env-map shading) | ✅ |
| WICKED-56b | `SkinnedEffect` (bone palette at b1, FNA's `WeightsPerVertex` gating, skin composed with the world normal matrix, post-skin fog) | ✅ |
| WICKED-56c | `PbrEffect` / `SkinnedPbrEffect` (glTF 2.0 metallic-roughness BRDF, normal/metallic-roughness/emissive/occlusion maps, strides 48 and 68) | ✅ |
| WICKED-57 | Custom `ShaderEffect` (`CreateEffectBackend`) | ⬜ |
| WICKED-58 | Multi-stream vertex input — a split `VertexDeclaration` is re-slotted into its own input layout; several per-INSTANCE streams still refused | ✅ |
| WICKED-59 | Strides 48/52/56/68 (tangent/skinned layouts) — declared, matched to the D3D11/D3D12 element tables, drawn with the ordinary shading | ✅ |
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
| WICKED-77 | Instanced draws honour the geometry stream's `VertexOffset` (regression `Wicked_GeometryVertexOffset`) | ✅ |
| WICKED-78 | Device teardown releases every GPU/VMA allocation (`cmake/patches/wicked-device-teardown.patch`; regression `Wicked_DeviceLifecycle`) | ✅ |
| WICKED-79 | Staged texture uploads store at the staging texture's own mapped pitches (narrow cube/3D/rect uploads no longer smear) | ✅ |
| WICKED-80 | Staging buffers sized to their aligned mapped-layout footprint (`cmake/patches/wicked-staging-footprint.patch`; regression `Wicked_Texture3DStagedTransfer`) | ✅ |

---

## Remaining work (read this first)

- **First executed 2026-08-05 on a software Vulkan device; real-hardware verification still open
  (`WICKED-18`, `WICKED-74`).** The backend builds against the patched Wicked Engine, creates a
  real `GraphicsDevice_Vulkan` and compiles all 22 shader entry points at device creation; the
  pipeline-key, device-lifecycle and geometry-offset suites and the 2D demo smoke run pass on
  llvmpipe/lavapipe. What remains open is a run on real GPU hardware with a real display.
- **`WICKED-78` (found and fixed at first execution): upstream device teardown leaked.** At the
  pinned revision the Vulkan device destructor never destroys its three null images, so VMA's
  "Some allocations were not freed" assertion aborted every device that never drew; the engine's
  pool-allocated command lists were also never freed, so a device that HAD drawn leaked its whole
  `VkInstance`/`VkDevice`/allocator instead — masking the assertion. Both destructor gaps are
  closed by `cmake/patches/wicked-device-teardown.patch`.
- **`WICKED-79` (found by the first full corpus run): staged texture uploads smeared at narrow
  widths.** Passing a tightly packed box as `CreateTexture` initial data loses every row whose
  byte width is not a multiple of the device's `optimalBufferCopyRowPitchAlignment` -- the
  initial-data repack stores rows tightly while `CopyTexture` consumes the ALIGNED mapped
  pitches, so narrow cube faces, volumes and sub-rect uploads read back zeros past the first
  rows. The staging texture is now created unpopulated and written through its own
  `mapped_subresources[0]` pitches, and each staged upload submits before returning -- two
  staged copies recorded on one command list interfere (measured on a raw device: the first
  cube face reads back with another upload's rows spliced in; a submit between them is
  byte-exact at every width).
- **`WICKED-80` (found by the Batch 2 stabilization's sanitized narrow-upload probe, 2026-08-06;
  RESOLVED the same day by the third carried patch): upstream under-allocates every narrow
  UPLOAD/READBACK staging buffer.** The stabilization measured `Texture3D` SetData/GetData round
  trips corrupting shape-dependent tail rows/slices (5×5×3 in one probe layout, 4×5×3 and 6×5×3 in
  another), with the wrong bytes equal to EARLIER texels of the same uploaded pattern. The
  raw-`wi::graphics` control (no CNA in the process) settled ownership as an upstream defect:
  `GraphicsDevice_Vulkan::CreateTexture` sizes UPLOAD/READBACK staging buffers with
  `ComputeTextureMemorySizeInBytes` — the TIGHT texel size — while the mapped layout it hands out
  (`CreateTextureSubresourceDatas` with `optimalBufferCopyRowPitchAlignment`, 128 on the measured
  Mesa devices) and `CopyTexture`'s buffer addressing consume ALIGNED row pitches, so for any
  subresource whose row bytes are not a multiple of the alignment both copy directions address past
  the end of the allocation. The Khronos validation layer names it exactly
  (`VUID-vkCmdCopyBufferToImage-pRegions-00171` / `VUID-vkCmdCopyImageToBuffer-pRegions-00183`, on
  lavapipe AND on Intel ANV — not a driver quirk), and whether a given shape's round trip actually
  corrupted was decided purely by what the suballocator placed next to the buffer — which is why
  the failing shape set followed the allocation sequence, why isolated single-shape runs passed,
  and why all 13 corpus transfer tests stayed green over a live defect. It also explains
  `WICKED-79`'s measured "two staged copies on one command list interfere": the second staging
  buffer landed inside the first one's out-of-bounds addressed range. The same under-allocation
  covers narrow `Texture2D`/cube-face/small-mip staging (measured: a 5×5 upload staging is 100
  bytes against a 640-byte addressed footprint) — every such transfer was latently exposed.
  **Fix:** `cmake/patches/wicked-staging-footprint.patch` sizes those buffers with exactly the
  footprint `CreateTextureSubresourceDatas` lays out (applies cleanly on the pristine pin after the
  SDL3 and teardown patches; applied/verified by `cna_wicked_check_staging_footprint_fix` in
  `cmake/ThirdPartyWicked.cmake`). **Regression:** `Wicked_Texture3DStagedTransfer` — a byte-exact
  index-encoded matrix over narrow/aligned/boundary volumes, sub-box upload/readback, repeated
  readbacks, plus the WICKED-79 Texture2D, TextureCube-face and small-mip controls; its sequenced
  narrow matrix fails 3/3 deterministically against the pre-fix library and passes 3/3 with the
  patch. Probes, run logs and the pre-fix discriminator evidence are preserved in
  `cmake-build-wicked/wicked-repro/` (`probe_texture3d_staged_transfer.cpp`,
  `probe_raw_wicked_texture3d.cpp`, `README.md`).
- **`WICKED-77` (found and fixed at first execution): the instanced route dropped the geometry
  stream's `VertexOffset`.** That route carries each stream's whole public offset in the stream
  table (the ordinary routes fold it into `baseVertex`), and the single-geometry-stream binding
  ignored it — an offset-selected record rendered nothing while the identical bytes drew correctly
  through the ordinary indexed route. The binding now adds the stream's own offset, which is zero
  on the folded ordinary routes.
- **A declaration this backend's stride table would reinterpret is refused at draw time**
  (`REMED-GFX-DECL-GUARD`). `VariantForStride()` selects the input layout and vertex program from
  the byte stride alone, so a custom `VertexDeclaration` that happens to be one of the eight known
  widths would otherwise be read from the wrong offsets and rendered without any error. The check
  is asymmetric — only what the caller declared is verified, never equality against this backend's
  own template — so a declaration that omits attributes the template carries still draws.
- **No custom `ShaderEffect`** (`WICKED-57`/`68`), refused explicitly and reported through
  `SupportsCapability()`. This is the one remaining feature gap that needs new infrastructure
  rather than another shader: `IEffectBackend`'s uniform setters address constants BY NAME, which
  needs SPIR-V reflection this backend does not do yet.
- **MRT writes only slot 0's colour** (`WICKED-54`). The stock pixel shaders declare one
  `SV_Target`, so slots 1..3 receive nothing beyond what their `ColorWriteChannels` mask lets
  through — the same thing XNA does when a stock effect draws into an MRT set. Writing distinct
  values per slot needs a custom effect, i.e. `WICKED-57`.
- Every stock effect is now implemented: `BasicEffect`, `AlphaTestEffect`, `DualTextureEffect`,
  `EnvironmentMapEffect`, `SkinnedEffect`, `PbrEffect` and `SkinnedPbrEffect`.
- **Instancing is limited to the four narrow strides** (16/20/24/32) and is refused on the wider
  tangent/skinned layouts, which have no instanced entry point.
- **Instancing accepts only `InstanceFrequency == 1`** (`WICKED-53`). Wicked Engine's `InputLayout`
  carries no instance-step-rate field, so any other frequency is refused rather than silently
  drawn at rate 1. The per-instance record must be CNA's 64-byte column-major `Matrix`.
- **Buffer regioning is bounded, not fenced** (`WICKED-32`). A buffer rotates through
  `kWickedBufferRegions` (3) regions, which covers the two frames Wicked keeps in flight plus one
  extra write. A game that rewrites the same buffer more than three times in one frame can still
  outrun it; detecting that needs per-region fences.
- **Mip generation is a CPU box filter at creation** (`WICKED-28`), not a GPU blit chain, and does
  not re-run when `SetData` replaces level 0 afterwards.
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
- The device-teardown patch is destructor-scoped: it releases what the constructor and command-list
  pool already own and adds no behaviour anywhere else, so it too stays mechanical to rebase — and
  drops out entirely if upstream fixes its own teardown.
- The established backends are untouched: every change for this backend is either backend-local or
  in the shared CMake selection/linking lists.
