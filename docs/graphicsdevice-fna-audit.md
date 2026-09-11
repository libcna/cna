# GraphicsDevice FNA Audit

**Date:** 2026-09-11 (adversarial refresh, `SOFTWARE-329`)
**FNA reference:** `FNA/src/Graphics/GraphicsDevice.cs` (1820 lines)  
**CNA implementation:** `modules/graphics/include/Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp`

This document compares CNA's `GraphicsDevice` against FNA's public XNA 4.0 API surface.

---

## 1. Constructors

| FNA signature | CNA status |
|---|---|
| `GraphicsDevice(GraphicsAdapter, GraphicsProfile, PresentationParameters)` | ✅ Present |
| `GraphicsDevice()` (default, for headless use) | ✅ Present and marked CNAEXT (`SOFTWARE-329`) |

FNA has exactly one public constructor. CNA's no-arg constructor is a legitimate CNAEXT addition.

---

## 2. Events

| Event | Type in FNA | CNA status |
|---|---|---|
| `Disposing` | `EventHandler<EventArgs>` | ✅ |
| `DeviceLost` | `EventHandler<EventArgs>` | ✅ |
| `DeviceReset` | `EventHandler<EventArgs>` | ✅ |
| `DeviceResetting` | `EventHandler<EventArgs>` | ✅ |
| `ResourceCreated` | `EventHandler<ResourceCreatedEventArgs>` | ✅ |
| `ResourceDestroyed` | `EventHandler<ResourceDestroyedEventArgs>` | ✅ |

All events present and correctly typed.

---

## 3. Properties

| FNA property | Access | CNA getter/setter | Status |
|---|---|---|---|
| `IsDisposed` | get | `getIsDisposedProperty()` | ✅ |
| `GraphicsDeviceStatus` | get | `getGraphicsDeviceStatusProperty()` | ✅ |
| `Adapter` | get | `getAdapterProperty()` | ✅ |
| `GraphicsProfile` | get | `getGraphicsProfileProperty()` | ✅ |
| `PresentationParameters` | get | `getPresentationParametersProperty()` | ✅ |
| `DisplayMode` | get | `getDisplayModeProperty()` | ✅ |
| `Textures` | get | `getTexturesProperty()` | ✅ |
| `SamplerStates` | get | `getSamplerStatesProperty()` | ✅ |
| `VertexTextures` | get | `getVertexTexturesProperty()` | ✅ |
| `VertexSamplerStates` | get | `getVertexSamplerStatesProperty()` | ✅ |
| `BlendState` | get/set | `getBlendStateProperty()` / `setBlendStateProperty()` | ✅ |
| `DepthStencilState` | get/set | `getDepthStencilStateProperty()` / `setDepthStencilStateProperty()` | ✅ |
| `RasterizerState` | get/set | `getRasterizerStateProperty()` / `setRasterizerStateProperty()` | ✅ |
| `ScissorRectangle` | get/set | `getScissorRectangleProperty()` / `setScissorRectangleProperty()` | ✅ |
| `Viewport` | get/set | `getViewportProperty()` / `setViewportProperty()` | ✅ |
| `BlendFactor` | get/set | `getBlendFactorProperty()` / `setBlendFactorProperty()` | ✅ |
| `MultiSampleMask` | get/set | `getMultiSampleMaskProperty()` / `setMultiSampleMaskProperty()` | ✅ |
| `ReferenceStencil` | get/set | `getReferenceStencilProperty()` / `setReferenceStencilProperty()` | ✅ |
| `Indices` | get/set | `getIndicesProperty()` / `setIndicesProperty()` | ✅ |

All 19 properties present.

---

## 4. Core Methods

### Present

| FNA signature | CNA status |
|---|---|
| `Present()` | ✅ |
| `Present(Rectangle? src, Rectangle? dst, IntPtr windowHandle)` | N/A to Software parity; absent on every CNA renderer |

The three-argument overload is classic XNA and remains a repository-wide public-surface omission.
It presents into a foreign physical window and optionally scales between source/destination
rectangles; neither EasyGL nor another CNA renderer exposes the required renderer contract. A stub
that merely called `Present()` would silently discard all three observable arguments and is not an
implementation. Foreign-window/swapchain behavior is explicitly outside the headless Software
parity target, so this audit records it without claiming support.

### Reset

| FNA signature | CNA status |
|---|---|
| `Reset()` | ✅ |
| `Reset(PresentationParameters)` | ✅ |
| `Reset(PresentationParameters, GraphicsAdapter)` | ✅ |

CNA also has a fourth overload `Reset(const PresentationParameters&, GraphicsAdapter*)` (pointer variant) — this is a CNAEXT convenience overload; it should be tagged `CNAEXT`.

### Clear

| FNA signature | CNA status |
|---|---|
| `Clear(Color color)` | ✅ (`SOFTWARE-333`: selects only real active attachments) |
| `Clear(ClearOptions, Color, float depth, int stencil)` | ✅ (`SOFTWARE-333`: Microsoft missing-attachment exception, intentionally stricter than FNA masking) |
| `Clear(ClearOptions, Vector4 color, float depth, int stencil)` | ✅ (`SOFTWARE-327/332/333`) |

The `Vector4` overload is XNA 4.0 API, but Microsoft and FNA differ in its implementation.
Recovered Microsoft XNA constructs `Color(color)` and calls the packed-color overload, so even a
floating-point target receives clamped, eight-bit-quantized components. CNA follows that measured
contract (`SOFTWARE-332`); FNA's direct native float clear is recorded as a divergence. CNA also has
two non-XNA convenience overloads, explicitly tagged `CNAEXT`:

- `Clear(float r, float g, float b, float a)`
- `Clear(const Color& color, float depth)`

### Dispose

| FNA signature | CNA status |
|---|---|
| `Dispose()` | ✅ |

---

## 5. Back-Buffer Readback

FNA uses generics; CNA maps them to constrained pointer templates and retains a Color-object
specialization:

| FNA signature | CNA equivalent | Status |
|---|---|---|
| `GetBackBufferData<T>(T[] data)` | `GetBackBufferData(T* data, int count)` | ✅ (`SOFTWARE-328`) |
| `GetBackBufferData<T>(T[] data, int start, int count)` | `GetBackBufferData(T* data, int start, int count)` | ✅ (`SOFTWARE-328`) |
| `GetBackBufferData<T>(Rectangle? rect, T[] data, int start, int count)` | `GetBackBufferData(const Rectangle* rect, T* data, int start, int count)` | ✅ (`SOFTWARE-328`) |

All three overloads now accept trivially-copyable element types whose width divides the applied
backbuffer texel and whose exact byte total covers the requested region. `Color` retains a separate
object-aware unpack because CNA's C++ type has a vtable. Software and EasyGL normalize their applied
backbuffer to Color, for which byte, 16-bit and four-byte application-defined values are covered.
Raw generic transfer from a renderer that retains a non-Color native backbuffer requires extending
the currently RGBA8-only renderer readback seam.

---

## 6. Render Target Methods

| FNA signature | CNA status |
|---|---|
| `SetRenderTarget(RenderTarget2D)` | ✅ |
| `SetRenderTarget(RenderTargetCube, CubeMapFace)` | ✅ |
| `SetRenderTargets(params RenderTargetBinding[])` | ✅ (`std::vector` instead of params array) |
| `GetRenderTargets()` | ✅ |
| `GetRenderTargetsNoAllocEXT(RenderTargetBinding[] output)` | ❌ Missing |

`GetRenderTargetsNoAllocEXT` is a FNA extension that copies current bindings into a caller-provided buffer (zero allocation). It is not core XNA but is part of the public FNA API surface.

---

## 7. Vertex / Index Buffer Methods

| FNA signature | CNA status |
|---|---|
| `SetVertexBuffer(VertexBuffer)` | ✅ |
| `SetVertexBuffer(VertexBuffer, int offset)` | ✅ |
| `SetVertexBuffers(params VertexBufferBinding[])` | ✅ |
| `GetVertexBuffers()` | ✅ |

CNA adds three non-XNA helpers, explicitly marked `CNAEXT` by `SOFTWARE-329`:

- `SetIndexBuffer(const IndexBuffer*)` — XNA uses the `Indices` property setter; this is an alias
- `GetIndexBuffer()` — XNA uses the `Indices` property getter; this is an alias
- `GetVertexBuffer()` — returns only the first bound buffer; not in FNA API

CNA also exposes `Indices()` / `Indices(const IndexBuffer*)` as named methods alongside
`getIndicesProperty/setIndicesProperty`; these non-convention duplicates are marked `CNAEXT`.

---

## 8. Draw Methods

### Buffer-backed draw

| FNA signature | CNA status |
|---|---|
| `DrawPrimitives(PrimitiveType, int vertexStart, int primitiveCount)` | ✅ |
| `DrawIndexedPrimitives(PrimitiveType, int baseVertex, int minVertexIndex, int numVertices, int startIndex, int primitiveCount)` | ✅ |
| `DrawInstancedPrimitives(PrimitiveType, int baseVertex, int minVertexIndex, int numVertices, int startIndex, int primitiveCount, int instanceCount)` | ✅ |

### User-array draw (non-indexed)

FNA has two generic overloads. CNA replaces them with concrete typed overloads plus a raw `void*` fallback:

| FNA overload | CNA coverage |
|---|---|
| `DrawUserPrimitives<T>(...) where T : IVertexType` | ✅ 4 typed overloads (VPC, VPCT, VPT, VPNT) + raw `void*` |
| `DrawUserPrimitives<T>(..., VertexDeclaration) where T : struct` | ✅ Raw `void*` plus all built-in typed declaration overloads |

### User-array draw (indexed)

FNA has four generic overloads (short/int index × with/without VertexDeclaration). CNA:

| FNA overload | CNA coverage |
|---|---|
| `DrawUserIndexedPrimitives<T>(..., short[], ...) where T : IVertexType` | ✅ 4 typed overloads (uint16_t indices) |
| `DrawUserIndexedPrimitives<T>(..., short[], ..., VertexDeclaration) where T : struct` | ✅ Raw `void*` plus all built-in typed declaration overloads |
| `DrawUserIndexedPrimitives<T>(..., int[], ...) where T : IVertexType` | ✅ 4 typed overloads (uint32_t indices) |
| `DrawUserIndexedPrimitives<T>(..., int[], ..., VertexDeclaration) where T : struct` | ✅ Raw `void*` plus all built-in typed declaration overloads |

The `VertexDeclaration` variants allow callers to pass a custom vertex layout with a non-`IVertexType` struct. The raw `void*` overloads in CNA serve as a substitute.

---

## 9. FNA Extension Methods

| FNA EXT method | CNA status |
|---|---|
| `SetStringMarkerEXT(string)` | ✅ Present, correctly tagged `CNAEXT` |
| `GetRenderTargetsNoAllocEXT(RenderTargetBinding[])` | ❌ Missing |

---

## 10. Summary of Gaps

### Missing API outside the Software parity target

| Priority | Item | Notes |
|---|---|---|
| Repository-wide | `Present(Rectangle?, Rectangle?, IntPtr)` | Classic XNA physical foreign-window/swapchain operation; no CNA renderer seam; explicitly outside headless Software parity |
| FNA extension | `GetRenderTargetsNoAllocEXT(RenderTargetBinding[])` | Not XNA/Core and therefore outside this campaign |

### Resolved visibility / CNAEXT marking defects

| Item | Issue |
|---|---|
| `Clear(float, float, float, float)` | Fixed by SOFTWARE-327 — tagged `CNAEXT` |
| `Clear(const Color&, float)` | Fixed by SOFTWARE-327 — tagged `CNAEXT` |
| `GraphicsDevice()` | Fixed by SOFTWARE-329 — tagged `CNAEXT` |
| `Reset(const PresentationParameters&, GraphicsAdapter*)` | Fixed by SOFTWARE-329 — tagged `CNAEXT` |
| `SetIndexBuffer(const IndexBuffer*)` | Fixed by SOFTWARE-329 — tagged `CNAEXT` |
| `GetIndexBuffer()` | Fixed by SOFTWARE-329 — tagged `CNAEXT` |
| `GetVertexBuffer()` | Fixed by SOFTWARE-329 — tagged `CNAEXT` |
| `Indices()` / `Indices(const IndexBuffer*)` | Fixed by SOFTWARE-329 — tagged `CNAEXT` |

### Intentional C++ deviations (acceptable)

| Item | Reason |
|---|---|
| `GetBackBufferData` uses constrained `T*` plus a Color specialization instead of `T[]` | C++ pointer/count mapping; restored by SOFTWARE-328 |
| `DrawUserPrimitives` / `DrawUserIndexedPrimitives` use concrete typed overloads | C++ adaptation of C# generics |
| `SetRenderTargets` takes `std::vector` instead of `params` array | Idiomatic C++ |
| `SetVertexBuffers` takes `std::vector` instead of `params` array | Idiomatic C++ |
| `GetRenderTargets` returns `std::vector` instead of allocating a new array | Idiomatic C++ |
| `GetVertexBuffers` returns `std::vector` | Idiomatic C++ |
| Generic user draws use constrained raw-stream and built-in typed overloads | C++ adaptation retains both inferred and explicit-VertexDeclaration shapes |
