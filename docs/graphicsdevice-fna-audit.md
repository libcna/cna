# GraphicsDevice FNA Audit

**Date:** 2026-06-26  
**FNA reference:** `FNA/src/Graphics/GraphicsDevice.cs` (1820 lines)  
**CNA implementation:** `include/Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp` (664 lines)

This document compares CNA's `GraphicsDevice` against FNA's public XNA 4.0 API surface.

---

## 1. Constructors

| FNA signature | CNA status |
|---|---|
| `GraphicsDevice(GraphicsAdapter, GraphicsProfile, PresentationParameters)` | ✅ Present |
| `GraphicsDevice()` (default, for headless use) | ✅ Present (CNAEXT) |

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
| `Present(Rectangle? src, Rectangle? dst, IntPtr windowHandle)` | ❌ Missing |

The three-argument `Present` allows rendering into a sub-rectangle of a foreign window handle. Rarely used on desktop but is part of the XNA 4.0 public API. Stub needed.

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
| `Clear(Color color)` | ✅ |
| `Clear(ClearOptions, Color, float depth, int stencil)` | ✅ |
| `Clear(ClearOptions, Vector4 color, float depth, int stencil)` | ❌ Missing |

The `Vector4` overload is XNA 4.0 API. CNA instead has two non-XNA convenience overloads that are missing the `CNAEXT` tag:

- `Clear(float r, float g, float b, float a)` — not in XNA, missing `CNAEXT`
- `Clear(const Color& color, float depth)` — not in XNA, missing `CNAEXT`

### Dispose

| FNA signature | CNA status |
|---|---|
| `Dispose()` | ✅ |

---

## 5. Back-Buffer Readback

FNA uses generics; CNA uses `Color*` raw pointers (Color-only port):

| FNA signature | CNA equivalent | Status |
|---|---|---|
| `GetBackBufferData<T>(T[] data)` | `GetBackBufferData(Color* data, int count)` | ⚠️ Color-only |
| `GetBackBufferData<T>(T[] data, int start, int count)` | `GetBackBufferData(Color* data, int start, int count)` | ⚠️ Color-only |
| `GetBackBufferData<T>(Rectangle? rect, T[] data, int start, int count)` | `GetBackBufferData(const Rectangle* rect, Color* data, int start, int count)` | ⚠️ Color-only |

All three overloads exist with the correct shape. The lack of generics is an intentional C++ deviation; the Color specialisation covers the main use case. Other element types (`Vector4`, `byte[]`, custom structs) are unsupported until a template or additional overloads are added.

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

CNA adds three non-XNA helpers that are **missing the `CNAEXT` tag**:

- `SetIndexBuffer(const IndexBuffer*)` — XNA uses the `Indices` property setter; this is an alias
- `GetIndexBuffer()` — XNA uses the `Indices` property getter; this is an alias
- `GetVertexBuffer()` — returns only the first bound buffer; not in FNA API

CNA also exposes `Indices()` / `Indices(const IndexBuffer*)` as named methods alongside `getIndicesProperty/setIndicesProperty`. The method forms are non-convention duplicates and should be marked `CNAEXT`.

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
| `DrawUserPrimitives<T>(..., VertexDeclaration) where T : struct` | ⚠️ No `VertexDeclaration` variant — callers with custom vertex structs must use the raw `void*` overload |

### User-array draw (indexed)

FNA has four generic overloads (short/int index × with/without VertexDeclaration). CNA:

| FNA overload | CNA coverage |
|---|---|
| `DrawUserIndexedPrimitives<T>(..., short[], ...) where T : IVertexType` | ✅ 4 typed overloads (uint16_t indices) |
| `DrawUserIndexedPrimitives<T>(..., short[], ..., VertexDeclaration) where T : struct` | ⚠️ No VertexDeclaration variant |
| `DrawUserIndexedPrimitives<T>(..., int[], ...) where T : IVertexType` | ✅ 4 typed overloads (uint32_t indices) |
| `DrawUserIndexedPrimitives<T>(..., int[], ..., VertexDeclaration) where T : struct` | ⚠️ No VertexDeclaration variant |

The `VertexDeclaration` variants allow callers to pass a custom vertex layout with a non-`IVertexType` struct. The raw `void*` overloads in CNA serve as a substitute.

---

## 9. FNA Extension Methods

| FNA EXT method | CNA status |
|---|---|
| `SetStringMarkerEXT(string)` | ✅ Present, correctly tagged `CNAEXT` |
| `GetRenderTargetsNoAllocEXT(RenderTargetBinding[])` | ❌ Missing |

---

## 10. Summary of Gaps

### Missing XNA 4.0 API (must add for compliance)

| Priority | Item | Notes |
|---|---|---|
| Medium | `Present(Rectangle?, Rectangle?, IntPtr)` | Rarely used; stub that calls the no-arg Present is sufficient |
| Medium | `Clear(ClearOptions, Vector4, float, int)` | Convert Vector4 to Color internally |
| Low | `GetRenderTargetsNoAllocEXT(RenderTargetBinding[])` | FNA extension; low priority |

### Incorrect visibility / missing CNAEXT tags (should fix)

| Item | Issue |
|---|---|
| `Clear(float, float, float, float)` | Not in XNA API — add `CNAEXT` |
| `Clear(const Color&, float)` | Not in XNA API — add `CNAEXT` |
| `Reset(const PresentationParameters&, GraphicsAdapter*)` | Pointer overload not in XNA — add `CNAEXT` |
| `SetIndexBuffer(const IndexBuffer*)` | Alias for `setIndicesProperty` — add `CNAEXT` |
| `GetIndexBuffer()` | Alias for `getIndicesProperty` — add `CNAEXT` |
| `GetVertexBuffer()` | Not in FNA API — add `CNAEXT` |
| `Indices()` / `Indices(const IndexBuffer*)` | Duplicate of property methods — add `CNAEXT` |

### Intentional C++ deviations (acceptable)

| Item | Reason |
|---|---|
| `GetBackBufferData` uses `Color*` instead of `T[]` | C++ lacks runtime generics; Color covers primary use case |
| `DrawUserPrimitives` / `DrawUserIndexedPrimitives` use concrete typed overloads | C++ adaptation of C# generics |
| `SetRenderTargets` takes `std::vector` instead of `params` array | Idiomatic C++ |
| `SetVertexBuffers` takes `std::vector` instead of `params` array | Idiomatic C++ |
| `GetRenderTargets` returns `std::vector` instead of allocating a new array | Idiomatic C++ |
| `GetVertexBuffers` returns `std::vector` | Idiomatic C++ |
| No `DrawUserPrimitives`/`DrawUserIndexedPrimitives` VertexDeclaration variants | Use raw `void*` overload as substitute |
