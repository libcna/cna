# `dx7-spike` — `DX7-0` existence-gate spike findings (2026-07-21)

Run under real Wine `ddraw.dll`/`d3d.dll` (Wine 10.0~repack-6, `WINEPREFIX=$HOME/.wine-cna-dx1`,
`DISPLAY=:99` Xvfb, `WAYLAND_DISPLAY` unset) — same environment `dx1-spike`/`dx2-spike`/
`dx3-spike`/`dx5-spike`/`dx6-spike` used.

## What DX7 concretely means, vs. `DX6`

Unlike `DX6` (no new interface at all vs. `DX5`), **DX7 (1999) is a real architectural change to
the object graph**, confirmed by inspecting the real MinGW headers:

1. **`IDirectDraw7`/`IDirectDrawSurface7` are genuinely new interfaces.** `ddraw.h` itself even
   documents "`IDirectDraw7` cannot derive from `IDirectDraw4`; it is even documented as not
   interchangeable with earlier DirectDraw interfaces" — raising the real question of whether the
   `DirectDrawCreate()`+`QueryInterface()` upgrade chain every `DX2`..`DX6` backend has used still
   works, or whether the new `DirectDrawCreateEx()` entry point is required.
2. **`IDirect3DViewport3` (and the whole `CreateViewport`/`AddViewport`/`SetCurrentViewport`/
   `DeleteViewport`/`Clear2` object) is GONE ENTIRELY.** `IDirect3D7` has no `CreateViewport`
   method at all. `IDirect3DDevice7::SetViewport` takes a plain `D3DVIEWPORT7` struct directly, and
   `Clear()` is now a direct `IDirect3DDevice7` method — the whole per-frame "create a viewport
   object, attach it to the device, clear through it" dance this backend family has done since
   `DX2-0` disappears.
3. **`IDirect3D7::CreateDevice` drops the trailing `IUnknown* outer` parameter** `DX5`/`DX6`'s
   `IDirect3D3::CreateDevice` had (a real signature oscillation: absent on `DX2`/`DX3`'s
   `IDirect3D2::CreateDevice`, added for `DX5`/`DX6`, removed again for `DX7`).
4. **`IDirect3DDevice7::SetTexture(stage, IDirectDrawSurface7*)` binds a texture directly from the
   surface pointer** — no more texture-handle indirection (`D3DRENDERSTATE_TEXTUREHANDLE` +
   `IDirect3DTexture2::GetHandle`, the `QueryInterface(IID_IDirect3DDevice2)` workaround `DX5`'s own
   design decision 6 needed) at all.
5. **`IDirectDrawSurface7` keeps the same `Unlock(LPRECT)` shape `IDirectDrawSurface4` already
   had** — no repeat of the `DX5-0` `Unlock`-signature surprise. It adds `SetPriority`/
   `GetPriority`/`SetLOD`/`GetLOD` (texture/mipmap management), not used by this backend family's
   design.
6. `EnumDevices7` reports Wine genuinely offers a hardware-T&L device class (`IID_
   IDirect3DTnLHalDevice`, name `"Wine D3D7 T&L HAL"`) in this environment — see "Practical
   conclusion" below for why this backend still uses the RGB software device regardless.

## Result: every test passed exactly as predicted, first run — no spike-authoring bugs

| # | Test | What it proves | Result |
|---|---|---|---|
| A1 | `DirectDrawCreate` (v1) + `QueryInterface(IID_IDirectDraw7)` | Whether the OLD upgrade chain still works despite the header's "cannot derive" note | ✅ Works |
| A2 | `DirectDrawCreateEx(nullptr, &dd7, IID_IDirectDraw7, nullptr)` | Whether the NEW DX7 entry point works | ✅ Works |
| B | Combined depth+stencil Z-buffer surface (`DDPF_ZBUFFER\|DDPF_STENCILBUFFER`, 32-bit total, same shape `DX6-0` proved) via `IDirectDrawSurface7`/`CreateSurface` | Whether the v7 surface layer supports the same stencil-capable Z-buffer | ✅ Works |
| C | `IDirect3D7::CreateDevice(IID_IDirect3DRGBDevice, surface7, &device7)` — no trailing outer param | Whether device creation works with the new (shorter) signature | ✅ Works |
| D | `device7->SetViewport(&D3DVIEWPORT7{...})` — no viewport object created at all | Whether the flattened, no-viewport-object model works | ✅ Works |
| E | `device7->Clear(...)` called directly (no viewport indirection), then a stencil WRITE pass (left-half quad, `STENCILPASS=REPLACE`) | Whether device-direct `Clear` + stencil write survive the API flattening | ✅ Works — left half reads green, right half stays black |
| F | A full-screen quad with `STENCILFUNC=EQUAL` on top of E's result | Whether stencil TEST still correctly gates per-pixel | ✅ Works — left half (stencil==1) shows red, right half (stencil==0) correctly rejects |
| G | `device7->SetTexture(0, texSurface7)` — direct surface bind, no handle at all — then a textured quad | Whether the new no-handle texture-binding mechanism actually samples correctly | ✅ Works — exact solid-red readback |

Every readback matched its predicted value exactly, on the first run, including the added Test G
(appended after the first full pass already succeeded, to settle the texture-binding design
decision empirically before writing any backend code) — no spike-authoring bugs at all this round.

## Practical conclusion

- **Use `DirectDrawCreateEx(nullptr, &dd7, IID_IDirectDraw7, nullptr)`** as the real DX7 entry
  point (matches the recommended DX7-era API even though the old QI chain also empirically works).
- **Remove the whole viewport-object pattern** this backend family has carried since `DX2-0`:
  `IDirect3DDevice7::SetViewport(D3DVIEWPORT7*)` and `IDirect3DDevice7::Clear(...)` replace
  `CreateViewport`/`AddViewport`/`SetCurrentViewport`/`DeleteViewport`/`viewport->SetViewport2`/
  `viewport->Clear2` entirely — a real, positive simplification of this backend's device bring-up
  and per-frame clear code.
- **`CreateDevice` loses its trailing `nullptr` outer argument** vs. `DX5`/`DX6`.
- **Replace the whole `Dx6ResolveTextureHandle`/`D3DRENDERSTATE_TEXTUREHANDLE`/`QueryInterface
  (IID_IDirect3DDevice2)` dance with a direct `SetTexture(stage, surface7)` call** — texture binding
  becomes simpler than at any prior DirectX era in this backend family.
- **Stencil (`D3DRENDERSTATE_STENCIL*`) is completely unchanged** — still real, still works
  identically through `SetRenderState` on the device, ported verbatim from `DX6`.
- **Hardware T&L (`IID_IDirect3DTnLHalDevice`) is real in this Wine but deliberately NOT adopted**:
  this whole backend family submits CPU-pre-transformed-and-lit `D3DTLVERTEX` vertices by design
  (matching XNA/FNA's own CPU-side `BasicEffect` math exactly, keeping behavior deterministic across
  environments) — real hardware T&L would require submitting un-transformed, un-lit vertices and
  delegating to the device's own fixed-function `SetTransform`/`SetLight`/`SetMaterial` pipeline,
  the opposite of this design. `IID_IDirect3DRGBDevice` (the same software device class every prior
  backend in this family uses) is used here too, for the same consistency reasons.

## Files

- `dx7_spike1_flattened_device.cpp` — the spike above.

Build (MinGW cross, ccache-wrapped):

```bash
ccache x86_64-w64-mingw32-g++ -O0 -g -c dx7_spike1_flattened_device.cpp -o dx7_spike1_flattened_device.o
x86_64-w64-mingw32-g++ -O0 -g -o dx7_spike1_flattened_device.exe dx7_spike1_flattened_device.o -lddraw -ldxguid -luser32 -lgdi32 -lkernel32
```

Run:

```bash
export WAYLAND_DISPLAY=       # unset -- Wine prefers Wayland over X11 if this is set at all
export DISPLAY=:99
export WINEPREFIX="$HOME/.wine-cna-dx1"
wine dx7_spike1_flattened_device.exe
```
