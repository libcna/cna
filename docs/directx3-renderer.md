# DIRECTX3 (real DirectX 3 — DirectDraw v2 + Direct3D v2 `DrawPrimitive`) Renderer — Completeness Status

> **Naming: rename executed 2026-08-04 (owner instruction, dxold integration).** This is CNA's
> real, Route-B "DirectX 3" renderer per `plans/plan_dxold.md`'s roadmap (row 3). It originally shipped
> under the temporary CMake name `DX30` while the `../free-direct`-backed 2D renderer still owned
> `DIRECTX3`; that renderer is now `FREEDIRECT` (`docs/freedirect-renderer.md`) and this one owns its
> final `CNA_GRAPHICS_RENDERER=DIRECTX3` identity. Historical `DX30-*` task IDs are unchanged.

`DIRECTX3` is architecturally `DIRECTX2` (`docs/directx2-renderer.md`) plus exactly one upgrade: the
`IDirectDraw` object obtained from `DirectDrawCreate` is immediately upgraded via
`dd->QueryInterface(IID_IDirectDraw2, &dd2)`, and every subsequent DirectDraw call goes through
the resulting `LPDIRECTDRAW2` instead of the v1 pointer. Per `plans/plan_dxold.md`'s roadmap, "DIRECTX3
(real)" = *"DirectDraw v2 (`IDirectDraw2`, adds refresh-rate to `SetDisplayMode`) + execute-buffer
Direct3D, matured."* The execute-buffer half of that description is already known non-functional
in this environment (`dx2-spike/README.md`'s 14-variant finding) — `DIRECTX2` already resolved the 3D
question by using the DX3-SDK's own `IDirect3D2`/`IDirect3DDevice2::DrawPrimitive` immediate-mode
API instead, which genuinely works. Since that API is *already* a DX3-SDK addition, **`DIRECTX3`'s 3D
layer needed no new spike or new code at all** — it is `DIRECTX2`'s already-proven 3D layer (including
Phase O9's CPU lighting and `WireFrame` support), ported forward unchanged.

**This document only covers what's specific to `DIRECTX3`.** Every capability row, boundary, and test
result in `docs/directx2-renderer.md` applies identically here (same 2D compositor, same 3D pipeline,
same lighting math, same known permanent limitations) — refer to it for the full completeness
table. This document covers: the `DX30-0` spike, the one real code delta (the `IDirectDraw2`
upgrade), and the DX3-specific CTest/regression results.

---

## 1. Existence-gate spike (`DX30-0`)

| # | Spike | What it proves | Result |
|---|---|---|---|
| `DX30-0a` | `dd->QueryInterface(IID_IDirectDraw2, &dd2)` on a real `DirectDrawCreate()`'d object | Whether `IDirectDraw2` is reachable at all | ✅ Works |
| `DX30-0b` | `dd2->GetAvailableVidMem(&caps, &total, &free)` (a genuinely v2-exclusive method) | Whether v2 is really implemented, not an `E_NOTIMPL` stub | ✅ Works — real non-zero data |
| `DX30-0c` | `dd2->CreateSurface(...)` + `Lock()`/`Unlock()` through the v2 pointer | Whether v2 is a fully-functional drop-in for every v1 call `DIRECTX1`/`DIRECTX2` already rely on | ✅ Works — identical to v1 |
| `DX30-0d` | `dd2->SetDisplayMode(640, 480, 32, 60, 0)` in windowed `DDSCL_NORMAL` mode | Whether the new refresh-rate parameter is usable | ❌ `E_NOTIMPL` — expected: `DX1-0`/`DX2-0` already established this renderer family never calls `SetDisplayMode` in windowed mode at all |
| `DX30-0e` | `dd2->QueryInterface(IID_IDirect3D2, &d3d2)` **from the v2 pointer**, then `CreateSurface`+`d3d2->CreateDevice(IID_IDirect3DRGBDevice, rt, &device)` | Whether `DIRECTX2`'s entire 3D bring-up chain still works when the DirectDraw object itself is v2 | ✅ Works — identical to `DIRECTX2`'s v1-rooted chain |

Full record: `dx3-spike/README.md` (`dx3_spike1_ddraw2.cpp`).

## 2. The one code delta vs. `DIRECTX2`

`DirectX3Renderer`'s constructor:

```cpp
LPDIRECTDRAW ddV1 = nullptr;
HRESULT hr = DirectDrawCreate(nullptr, &ddV1, nullptr);
if (FAILED(hr)) ThrowHr("DirectDrawCreate", hr);
hr = ddV1->QueryInterface(IID_IDirectDraw2, reinterpret_cast<void**>(&impl_->dd));
ddV1->Release();
if (FAILED(hr)) ThrowHr("IDirectDraw::QueryInterface(IID_IDirectDraw2)", hr);
```

`Impl::dd` is `LPDIRECTDRAW2` (was `LPDIRECTDRAW` in `DIRECTX2`); every other method on the renderer
(2D and 3D alike) is byte-identical to `DIRECTX2`'s post-Phase-O9 source, mechanically renamed
(`DirectX2`→`DirectX3`). `GetAvailableVidMem` is confirmed real (`DX30-0b`) but not exposed through
`IGraphicsRenderer` — no existing capability slot, no current consumer, out of scope (plans/plan_dx3.md
design decision 4).

## 3. CTest results

**19/19 `DIRECTX3`-labeled CTests pass**, all green on the first run after the mechanical port (no
transcription errors found):

`DirectX3_ExecuteBufferDiscipline`, `DirectX3_Smoke`, `DirectX3_TextureRenderTarget`, `DirectX3_SpriteBatch`,
`DirectX3_Blend`, `DirectX3_AddressMode`, `DirectX3_SpriteFont`, `DirectX3_GraphicsCapability`,
`DirectX3_LogicalTransform`, `DirectX3_Device3DSmoke`, `DirectX3_VertexIndexBuffer`,
`DirectX3_ColoredPrimitives`, `DirectX3_IndexedPrimitives`, `DirectX3_ZTest`, `DirectX3_Texture3D`,
`DirectX3_Clipping`, `DirectX3_RemainingDefaults`, `DirectX3_Lighting`, `DirectX3_WireframeAniso`.

`SupportsCapability(GraphicsCapability::WireFrame)` reports `true` (inherited from `DIRECTX2`'s Phase
O9 finding — same software RGB device, same confirmed-real `D3DFILL_WIREFRAME` distinctness);
`AnisotropicFiltering` stays `false` (same confirmed-absent finding).

Targeted cross-renderer regression (mirroring `DX2-98`'s scope decision — a full multi-hour
`CnaTests` regression is out of proportion for a mechanical port): `GraphicsRendererCompileDefinitionsTest.ExactlyOneGraphicsRendererIsSelected`,
`GraphicsDeviceValidationTest.SetRenderTargets_*`, and `GraphicsDeviceCapabilityTest.*` all pass
except the same 3 pre-existing, already-documented ungated-test-class failures `DX2-84` found
(`SupportsMultipleRenderTargets`/`SupportsOcclusionQuery`/`SupportsCustomEffects` — that test
asserts `true` unconditionally with no renderer gate at all, a pre-existing gap unrelated to
`DIRECTX3`).

## 4. Everything else

See `docs/directx2-renderer.md` sections 1–9 and 11 for the full completeness table (device bring-up,
`Texture2D`/`RenderTarget2D`, `SpriteBatch`, `SpriteFont`, 3D device bring-up, CPU transform/clip
pipeline, per-draw state mapping, CPU-side BasicEffect lighting, remaining defaults, known
permanent limitations) — every row applies to `DIRECTX3` identically, since its own code is a verbatim
port with only the `IDirectDraw2` upgrade described above.

## See also

- `plans/plan_dx3.md` — this renderer's own implementation plan; its status note records the executed
  `DX30`→`DIRECTX3` naming transition.
- `plans/plan_dx2.md`, `docs/directx2-renderer.md` — the renderer this one ports verbatim.
- `dx3-spike/README.md` — the full `DX30-0` spike record.
- `plans/plan_dxold.md` — the roadmap this renderer is row 3 of.
