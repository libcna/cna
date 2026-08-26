# WebGPU renderer: deliberate deviations from the Vulkan renderer

Both `VULKAN` and `WEBGPU` are explicit-GPU CNA renderers, and the WebGPU
implementation mirrors the Vulkan one wherever the two APIs agree. Where they do
not, the WebGPU renderer takes the choices below **on purpose**. Each is a
consequence of the WebGPU / `wgpu-native v29.0.1.1` API surface, not an
oversight. This document is the single place those choices are collected; the
per-feature detail lives in `docs/webgpu-renderer.md` and the task rows in
`plans/plan_webgpu.md`.

## Per-draw data: push constants → uniform buffers

Vulkan carries small per-draw effect data (the WVP matrix, diffuse colour, fog,
light parameters) in a `VkPushConstantRange`. **WebGPU has no push constants.**
The WebGPU renderer instead writes each draw's data into a uniform `WGPUBuffer`
via `wgpuQueueWriteBuffer` and binds it as a UBO (`FillExtUniforms()` and the
`Uniforms`/`LitLightParams`/`Transform` UBO shapes). The functional per-draw
uniform buffer is correct and GPU-verified; a per-frame ring-buffer variant is a
deferred performance optimisation only (`WEBGPU-12`).

## Wireframe: refused, not emulated

Vulkan honours `FillMode::WireFrame` through `VK_POLYGON_MODE_LINE`. **WebGPU has
no line polygon mode** — a render pipeline rasterizes filled triangles only.
Rather than silently draw filled geometry when wireframe was asked for, the
WebGPU renderer *refuses*: `SupportsCapability(GraphicsCapability::WireFrame)`
returns `false` and a wireframe draw throws (`WEBGPU-115`, enforced by
`WebGpuWireFrameContractTests`). Real wireframe would require CPU-side
index-expansion of triangles into line topology; it is intentionally not done.

## Async completion → synchronous pumping

`wgpu-native`'s adapter/device requests and buffer maps complete through
callbacks. CNA's renderer contract is synchronous, so the WebGPU renderer drives
those callbacks to completion itself:

- **Native:** `WaitForCompletion()` pumps `wgpuInstanceProcessEvents()` until the
  callback fires (bounded by a timeout).
- **Browser (Emscripten):** there is no such pump, so the same wait yields to the
  JavaScript event loop via `emscripten_sleep` (Asyncify) while the callback
  fires spontaneously (`WGPUCallbackMode_AllowSpontaneous`).

This is the one place the native and browser builds of the *same* renderer differ
in control flow (`WEBGPU-119`–`122`).

## `VertexElementFormat::Color` → `Unorm8x4`, not a BGRA format

Vulkan maps XNA's `Color` vertex element to `VK_FORMAT_B8G8R8A8_UNORM` so the
BGRA-in-memory bytes are swizzled to RGBA during the fetch. **WebGPU has no
B8G8R8A8 vertex format.** The WebGPU renderer maps `Color` to
`WGPUVertexFormat_Unorm8x4` and stores colour as RGBA in its packed layouts, so
the shader reads it straight with no swizzle (`WebGPUVertexFormatFromVEF()`,
`WEBGPU-116`). All other `VertexElementFormat` values map to the obvious WebGPU
equivalent.

## Debug string markers: no-op

Vulkan implements `SetStringMarkerEXT()` through `VK_EXT_debug_utils`. In
`wgpu-native` a debug marker attaches to a *live* command encoder or render pass
(`wgpuRenderPassEncoderInsertDebugMarker` and friends); there is no device-level
"insert a marker into the stream at an arbitrary time" call. Because this
renderer records draws into a deferred command list and has no encoder open at
the moment `GraphicsDevice.SetStringMarkerEXT()` is called, the WebGPU
implementation inherits the interface's **no-op** default (`WEBGPU-106`). No game
behaviour depends on it; it only affects external GPU-capture tooling.

## Multiple render targets and occlusion queries: unimplemented, and honest about it

Vulkan supports both. The WebGPU renderer does not implement MRT
(`WEBGPU-85`/`86`/`87`) or occlusion queries (`WEBGPU-84`) yet. Rather than claim
them, `SupportsCapability(MultipleRenderTargets)` and
`SupportsCapability(OcclusionQuery)` both return `false`, a >1-target bind throws
a `System::NotSupportedException` that names the capability query, and
`CreateOcclusionQuery()` is left as the base `nullptr` (`WEBGPU-134`/`135`,
enforced by `WebGpuMrtOcclusionContractTests`).

## Fullscreen, input, windowing: not a renderer concern

`IsFullScreen` and the rest of windowing are handled by the shared
`CNA::Platform` layer (`Sdl3Window::SetFullscreenMode` →
`SDL_SetWindowFullscreen`), identically for every renderer including WebGPU
(`WEBGPU-109`). The renderer sees only the surface it is handed.
