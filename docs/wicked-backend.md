# Wicked Engine graphics backend (`CNA_GRAPHICS_BACKEND=WICKED`)

CNA's Wicked Engine backend implements `CNA::Internal::Backends::IGraphicsBackend` on top of
[Wicked Engine](https://github.com/turanszkij/WickedEngine)'s render hardware interface,
`wi::graphics::GraphicsDevice`. That interface dispatches to a native Vulkan device on
Linux/Windows and to D3D12 on Windows.

Only the RHI layer is used. `wi::renderer`, `wi::scene`, `wi::Application`, `wi::input`, the Jolt
physics integration and the Lua scripting layer are part of the same static library but are never
called — CNA owns the XNA 4.0 programming model above this boundary and needs a device abstraction
below it, not a second engine.

The full task list and status is `plan_wicked.md`.

---

## Building

Wicked Engine is a large source dependency, so CNA does not clone it during a plain configure.
Clone it once and point CNA at it:

```bash
git clone https://github.com/turanszkij/WickedEngine.git
git -C WickedEngine checkout 27c0df160d738925474a2181d3f88bfd59edaefe

cmake -S . -B cmake-build-wicked \
      -DCNA_GRAPHICS_BACKEND=WICKED \
      -DCNA_WICKED_ROOT=/path/to/WickedEngine
cmake --build cmake-build-wicked -j4
```

`-DCNA_WICKED_AUTO_FETCH=ON` lets CMake clone the pinned revision itself instead; it is off by
default because the repository carries its sample `Content/` assets and the clone is measured in
gigabytes.

No Vulkan SDK is required: Wicked Engine vendors the Vulkan headers and loads the loader through
volk at run time. A Vulkan **loader and driver** (`libvulkan.so.1` plus an ICD) must be present on
the machine that runs the game.

### The SDL3 patch

Upstream Wicked Engine's Unix platform layer is SDL2-only: `wiPlatform.h` calls SDL2 window
functions unconditionally under `PLATFORM_LINUX`, and `GraphicsDevice_Vulkan::CreateSwapChain` has a
hard `#error PLATFORM NOT SUPPORTED` when neither `_WIN32` nor `SDL2` is defined. CNA is SDL3-only,
and SDL2 and SDL3 cannot be loaded into one process — they export the same symbol names with
different ABIs — so no configuration exists in which unpatched upstream and CNA can share a window.

`cmake/patches/wicked-sdl3-platform.patch` adds a parallel `SDL3` branch everywhere the `SDL2` one
already exists, and is applied automatically to the resolved checkout when it is not already
SDL3-aware. It touches six files (94 added lines) and changes nothing on the SDL2 path:

| File | Change |
|------|--------|
| `CMakeLists.txt` | `WICKED_USE_SDL3` option; sets `PLATFORM`/`SDL3=1` on Unix |
| `WickedEngine/CMakeLists.txt` | skips `find_package(SDL2)`, links `SDL3::SDL3`, defines `SDL3=1` |
| `WickedEngine/Utility/CMakeLists.txt` | builds FAudio with `BUILD_SDL3` / `FAUDIO_SDL3_PLATFORM` |
| `WickedEngine/wiPlatform.h` | SDL3 include block, `window_type`, window size/DPI, fullscreen |
| `WickedEngine/wiGraphicsDevice_Vulkan.cpp` | SDL3 instance extensions and `SDL_Vulkan_CreateSurface` |
| `WickedEngine/wiInput.cpp` | inert cursor table when no cursor backend is compiled in |

`wi::input` is not used by CNA — CNA drives SDL3 input itself — so the last entry deliberately
leaves Wicked's cursor management inert rather than porting it to SDL3.

Set `-DCNA_WICKED_APPLY_SDL3_PATCH=OFF` to apply it by hand instead. The patch is authored against
the pinned `CNA_WICKED_COMMIT`; a different revision may need it rebasing.

### The device-teardown patch (`WICKED-78`)

At the pinned revision, `GraphicsDevice_Vulkan`'s destructor leaks in two complementary ways:

- The three **null images** created beside `nullBuffer` (and their views) are never destroyed, so
  they are still allocated when `vmaDestroyAllocator` runs and VMA's *"Some allocations were not
  freed before destruction of this memory block"* assertion **aborts the process** — on exactly
  the devices that never rendered, because those are the ones whose allocator is actually torn
  down.
- The **pool-allocated `CommandList_Vulkan` objects** are never freed (`cmd_allocator` placement-
  allocates them; the destructor destroys their Vulkan pools but never runs their destructors), so
  once any command list has touched its per-frame linear allocator, the retained `GPUBuffer` keeps
  the whole allocation handler — `VmaAllocator`, `VkDevice` **and** `VkInstance` — alive forever.
  A device that has drawn therefore *looks* like it tears down cleanly while actually leaking all
  of it, which is also what masks the assertion above.

`cmake/patches/wicked-device-teardown.patch` releases both in the destructor and is applied to the
resolved checkout automatically, exactly like the SDL3 patch; set
`-DCNA_WICKED_APPLY_TEARDOWN_PATCH=OFF` to apply it by hand. The `Wicked_DeviceLifecycle` test
pins the fixed behaviour: it is plain device create/destroy cycles, and before the patch the first
one aborts the binary inside `vk_mem_alloc.h`.

### Run-time requirement: `libdxcompiler.so` in the working directory

CNA compiles its shaders at run time through `wi::shadercompiler`, which loads
`./libdxcompiler.so` (`dxcompiler.dll` on Windows) **relative to the process's current working
directory**, not to the executable. The build copies the library next to the backend's own build
output; a packaged game must ship it next to the binary *and* be launched from that directory.
When it is missing, backend construction throws with a message that says so.

---

## Frame structure

Wicked Engine's swap-chain render pass acquires an image and always clears, so it can be entered
exactly once per frame. This backend therefore renders every frame into an off-screen "scene"
colour target sized to CNA's virtual resolution and blits that target into the swap chain at
`Present()`:

- switching between the back buffer and a `RenderTarget2D` mid-frame costs nothing but a render
  pass boundary;
- the letterbox / overscan / stretch presentation CNA's `CnaPresentationMode` asks for is the blit
  rectangle, and `TransformWindowToLogical` is its inverse;
- `ReadBackbuffer` reads the scene target, so it observes exactly what the game drew, independent
  of the window size.

A clear issued before anything has been drawn into the scene target becomes a render-pass `CLEAR`
load operation. A clear issued mid-pass becomes a full-target quad with the requested colour, depth
and stencil writes — the same fallback the other render-pass-based CNA backends use.

## Shaders

One HLSL source (`src/CNA/Internal/Backends/Wicked/WickedShaderSources.hpp`) is compiled at device
creation into whatever binary format `GraphicsDevice::GetShaderFormat()` reports — SPIR-V for the
Vulkan device, DXIL for D3D12. Because `wi::shadercompiler::Compile()` reads from a file path, the
source is written to a private temporary directory for the lifetime of the backend, so a CNA game
needs no shader-asset deployment step.

Vertex entry points cover the stock vertex layouts, selected by byte stride exactly as the
D3D11/D3D12/Vulkan backends select theirs. Where two are listed, the second is the `SkinnedEffect`
program for that layout; the ordinary one declares the blend attributes without consuming them, so
the same geometry can also be drawn unskinned:

| Stride | Layout | Entry point |
|--------|--------|-------------|
| 16 | `VertexPositionColor` | `Basic16VS` |
| 20 | `VertexPositionTexture` | `Basic20VS` |
| 24 | `VertexPositionColorTexture` (also `SpriteBatch` and every internal quad) | `Basic24VS` |
| 32 | `VertexPositionNormalTexture` | `Basic32VS` |
| 48 | `VertexPositionNormalTangentTexture` | `Basic48VS` |
| 52 | `VertexPositionNormalTextureSkinned` | `Basic52VS` / `Skinned52VS` |
| 56 | stride 52 with a per-vertex `Color` | `Basic56VS` / `Skinned56VS` |
| 68 | `VertexPositionNormalTangentTextureSkinned` | `Basic68VS` / `Skinned68VS` |

A stride outside this set throws rather than being rendered through a layout that merely has the
same byte width. A stride *inside* the set is not accepted on its width alone either: the caller's
`VertexDeclaration` is remembered and checked at draw time (`REMED-GFX-DECL-GUARD`), so a custom
layout that happens to be, say, 32 bytes is refused with `System::NotSupportedException` instead of
being read through `VertexPositionNormalTexture`'s offsets. The check is asymmetric — only what the
caller actually declared is verified, never equality against this backend's own template.

The four narrow variants have an instanced sibling (`Basic16InstVS` … `Basic32InstVS`) that reads a per-instance
world matrix from a second input slot. The per-instance record is CNA's established 64-byte
column-major `Matrix` — the same layout the Vulkan backend's instanced pipeline consumes — so an
unmodified XNA `Matrix` in the instance buffer means the same thing on both backends. On these entry
points the constant buffer's transform holds `view * projection` alone; the world transform comes
from the instance.

---

## Capability boundary

Implemented and reported as supported:

| Area | Notes |
|------|-------|
| Device, swap chain, present | VSync from `PresentInterval`; swap chain recreated on resize |
| Virtual resolution / presentation modes | Letterbox, Overscan, Stretch, NativeBackBuffer, FixedHeightDynamicWidth |
| Clears | Colour, depth, stencil and every combination |
| `Texture2D` | Creation with a CPU-generated mip chain, `SetData` (level 0 and explicit mip levels), `GetData` |
| Vertex / index buffers | 16- and 32-bit indices; `SetDataOptions` honoured by region orphaning |
| `SpriteBatch` | Batched quads, tint, rotation, origin, flip, layer depth, per-batch sampler |
| 3D draws | `BasicEffect`, `AlphaTestEffect`, `DualTextureEffect`, `EnvironmentMapEffect`, `SkinnedEffect`, `PbrEffect`, `SkinnedPbrEffect`, fog, three directional lights, specular |
| `RenderTarget2D` | Colour + optional depth/stencil, `RenderTargetUsage`, readback |
| `RenderTargetCube` | Per-face rendering, whole-cube sampling, per-face upload and readback |
| MRT | Up to 4 colour targets, shared depth from slot 0; stock shaders write slot 0 only |
| Multi-stream vertex input | A split `VertexDeclaration` is re-slotted per buffer; one per-instance stream only |
| Render state | Blend (incl. per-slot write masks and sample mask), depth/stencil (incl. two-sided), rasterizer (incl. wireframe and depth bias), samplers |
| `TextureCube` | Six-face upload and readback, sampled by `EnvironmentMapEffect` |
| MSAA render targets | `RenderTarget2D` resolves to a single-sample texture used for both sampling and readback |
| `Texture3D` | Volume upload and readback, real GPU storage |
| Instanced draws | Per-instance 64-byte column-major `Matrix` stream, `InstanceFrequency == 1` only |
| MSAA | On the scene target and on `RenderTarget2D`, with resolve; device-clamped |
| Occlusion queries | Real `GPUQueryHeap` + readback buffer |

Not implemented; each is refused explicitly at the call site and reported by
`GraphicsDevice::SupportsCapability()` rather than silently approximated:

| Area | `GraphicsCapability` |
|------|----------------------|
| Custom `ShaderEffect` / `SpriteBatch.Begin(effect)` | `CustomEffects` |
| `InstanceFrequency` other than 1 | — (the draw throws; Wicked's `InputLayout` has no step-rate field) |
| D3D12 device selection | — (needs Wicked's root-signature macro in CNA's HLSL) |

---

## Verification status

As of 2026-08-05 the backend builds against the patched Wicked Engine, creates a real
`GraphicsDevice_Vulkan`, and compiles all 22 of its shader entry points at device creation. The
pipeline-cache-key unit suite, the `Wicked_DeviceLifecycle` regression suite and the 2D demo
smoke run pass on a **software** Vulkan device
(llvmpipe/lavapipe under Xvfb). **It has not yet been executed on real GPU hardware with a real
display** (`plan_wicked.md` `WICKED-18` / `WICKED-74`); do not describe this backend as
hardware-verified until that has happened.
