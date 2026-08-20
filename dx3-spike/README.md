# `dx3-spike` — `DX30-0` existence-gate spike findings (2026-07-21)

Run under real Wine `ddraw.dll` (Wine 10.0~repack-6, `WINEPREFIX=$HOME/.wine-cna-dx1`,
`DISPLAY=:99` Xvfb, `WAYLAND_DISPLAY` unset) — same environment `dx1-spike`/`dx2-spike` used.

## What this backend needs to prove

Per `plans/plan_dxold.md`'s roadmap, "real DX3" (originally landed as `DX30` in this repo, since renamed — see
`plans/plan_dx3.md`'s own status note for why) is DirectDraw **v2** (`IDirectDraw2`, adds a
refresh-rate parameter to `SetDisplayMode` and a new `GetAvailableVidMem` method) plus "execute-
buffer Direct3D, matured." The execute-buffer half of that description is **already known
non-functional** in this environment (`dx2-spike/README.md`'s 14-variant finding) — `DX2` already
resolved the 3D question by using the DX3-SDK's own `IDirect3D2`/`IDirect3DDevice2::DrawPrimitive`
immediate-mode API instead, which genuinely works. Since that API is *already* a DX3-SDK addition,
`DX30`'s 3D layer needs no new spike at all — it is architecturally identical to `DX2`'s already-
proven 3D layer. The only genuinely new surface for this backend is the 2D layer's **DirectDraw
object itself becoming `IDirectDraw2`**, which is what `dx3_spike1_ddraw2.cpp` tests.

## Result: `IDirectDraw2` is real and fully functional as a drop-in for `IDirectDraw` v1

| # | Test | What it proves | Result |
|---|---|---|---|
| A | `dd->QueryInterface(IID_IDirectDraw2, &dd2)` on a real `DirectDrawCreate()`'d object | Whether `IDirectDraw2` is reachable at all in this Wine | ✅ **Works** |
| B | `dd2->GetAvailableVidMem(&caps, &total, &free)` (v2-exclusive method, does not exist on v1) | Whether v2 is a real, implemented interface, not an `E_NOTIMPL` stub | ✅ **Works** — returns a real (if generous, ~4 GiB) non-zero value, not a stub |
| C | `dd2->CreateSurface(...)` + `Lock()`/`Unlock()` through the **v2** pointer | Whether the v2 object is a fully-functional drop-in for every v1 method DX1/DX2 already rely on, not just a `QueryInterface`-only formality | ✅ **Works** — identical to the v1 path |
| D | `dd2->SetDisplayMode(640, 480, 32, 60, 0)` (v2's wider, 5-argument signature) in windowed `DDSCL_NORMAL` mode | Whether the new refresh-rate parameter is usable | ❌ **`E_NOTIMPL` (`0x80004001`)** — but this is *expected*, not a gap: `DX1-0`/`DX2-0` already established that windowed `DDSCL_NORMAL` mode never needs `SetDisplayMode` at all (the primary surface is desktop-sized, not "this window," and no backend in this family ever calls it) — Wine's `ddraw.dll` simply doesn't implement `SetDisplayMode` outside exclusive-fullscreen mode, matching real DirectDraw's own documented behavior. Confirms the extra refresh-rate parameter is dead code for this backend family's windowed-only design, not a newly-discovered engagement gap.
| E | `dd2->QueryInterface(IID_IDirect3D2, &d3d2)` **from the v2 pointer** (not v1, unlike `DX2`'s own code), then `CreateSurface`+`d3d2->CreateDevice(IID_IDirect3DRGBDevice, rt, &device)` | Whether `DX2`'s entire 3D bring-up chain still works unchanged when the DirectDraw object itself is v2 instead of v1 -- the one path `DX2`'s own spike never had reason to test | ✅ **Works** — identical to `DX2`'s v1-rooted chain in every respect.

## Practical conclusion

`DX30`'s 2D layer is `DX2`'s own already-proven 2D layer with one mechanical change: the object
obtained from `DirectDrawCreate` is immediately upgraded via `QueryInterface(IID_IDirectDraw2)`,
and every subsequent DirectDraw call (`CreateSurface`, `SetCooperativeLevel`, etc.) is issued
through the resulting `LPDIRECTDRAW2` instead of the v1 `LPDIRECTDRAW` — confirmed by Test C to
behave identically. `SetDisplayMode` is never called (matching `DX1`/`DX2`'s own established
decision), so its wider v2 signature is accepted-and-unused rather than exercised.
`GetAvailableVidMem` is confirmed real and available, but `IGraphicsBackend` has no existing
video-memory-query capability slot to expose it through — noted as available-but-unused, not
wired to any public API (no scope creep beyond what "DX3 real" needs).

`DX30`'s 3D layer needs **no new spike** — it is architecturally identical to `DX2`'s own already-
verified `IDirect3D2`/`IDirect3DDevice2`/`IDirect3DViewport2`/`IDirect3DTexture2` `DrawPrimitive`/
`DrawIndexedPrimitive` pipeline (see `dx2-spike/README.md`), including Phase O9's CPU lighting and
`WireFrame` support, all ported forward unchanged.

## Files

- `dx3_spike1_ddraw2.cpp` — the spike above.

Build (MinGW cross, ccache-wrapped):

```bash
ccache x86_64-w64-mingw32-g++ -O0 -g -c dx3_spike1_ddraw2.cpp -o dx3_spike1_ddraw2.o
x86_64-w64-mingw32-g++ -O0 -g -o dx3_spike1_ddraw2.exe dx3_spike1_ddraw2.o -lddraw -ldxguid -luser32 -lgdi32 -lkernel32
```

Run:

```bash
export WAYLAND_DISPLAY=       # unset -- Wine prefers Wayland over X11 if this is set at all
export DISPLAY=:99
export WINEPREFIX="$HOME/.wine-cna-dx1"
wine dx3_spike1_ddraw2.exe
```
