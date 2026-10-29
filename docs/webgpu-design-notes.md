# WebGPU renderer: design notes

The choices below follow from the WebGPU / `wgpu-native v29.0.1.1` API surface
and are deliberate. The per-feature detail lives in `docs/webgpu-renderer.md`; the
`WEBGPU-nnn` task rows are in Git history.

## Per-draw data in uniform buffers

**WebGPU has no push constants.** Each draw's effect data (the WVP matrix,
diffuse colour, fog, light parameters) is written into a uniform `WGPUBuffer`
via `wgpuQueueWriteBuffer` and bound as a UBO (`FillExtUniforms()` and the
`Uniforms`/`LitLightParams`/`Transform` UBO shapes). Stock-effect fog widens the
shared primary uniform block (128→160 bytes) to carry `fogColor` and the FNA
view-space `fogVector` (`WEBGPU-145`–`148`). A per-frame ring-buffer variant is a
deferred performance optimisation only (`WEBGPU-12`).

## Wireframe: emulated by edge expansion, not by a polygon mode

WebGPU's core `GPUPrimitiveState` has no polygon mode. Since `WEBGPU-153` the
renderer draws a wireframe the way the reference renderer (EasyGL) always has:
at queue time each triangle's three edges become three two-index lines in a
32-bit index buffer, and the draw is submitted as a line list.
`SupportsCapability(GraphicsCapability::WireFrame)` reports **true**, and the
shared `WireFrameTriangleOracle` measures it — interior `0/1089` with all three
edges present, against Solid's `1089/1089`.

The pinned wgpu-native does carry `WGPUPrimitiveStateExtras::polygonMode` with
`WGPUPolygonMode_Line`, behind `WGPUNativeFeature_PolygonModeLine`. CNA
deliberately does not use it: it is native-only, and the expansion is the one
route that also works in the browser.

A wireframe pipeline is a **line topology**, so WebGPU forbids a depth bias on it
and the renderer drops `RasterizerState.DepthBias` for that draw — a depth bias
is an offset along a polygon's depth slope, which a line does not have. Shared
vertices stay shared, so an interior edge between two triangles is drawn twice;
that matches the reference renderer exactly. Line and point topologies are left
alone: `Solid` and `WireFrame` produce byte-identical frames
(`WebGpuWireFrameContract.OnlyPolygonTopologiesAreExpanded`).

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

**WebGPU has no B8G8R8A8 vertex format.** The renderer maps XNA's `Color` vertex
element to `WGPUVertexFormat_Unorm8x4` and stores colour as RGBA in its packed
layouts, so the shader reads it straight with no swizzle
(`WebGPUVertexFormatFromVEF()`, `WEBGPU-116`). All other `VertexElementFormat`
values map to the obvious WebGPU equivalent.

## Fullscreen, input, windowing: not a renderer concern

`IsFullScreen` and the rest of windowing are handled by the shared
`CNA::Platform` layer (`Sdl3Window::SetFullscreenMode` →
`SDL_SetWindowFullscreen`), identically for every renderer including WebGPU
(`WEBGPU-109`). The renderer sees only the surface it is handed.
