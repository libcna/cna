# `dx5-spike` — `DX5-0` existence-gate spike findings (2026-07-21)

Run under real Wine `ddraw.dll`/`d3d.dll` (Wine 10.0~repack-6, `WINEPREFIX=$HOME/.wine-cna-dx1`,
`DISPLAY=:99` Xvfb, `WAYLAND_DISPLAY` unset) — same environment `dx1-spike`/`dx2-spike`/
`dx3-spike` used.

## What DX5 concretely needs, vs. `DX2`/`DX3`

DX5 (1997) is the first DirectX release where Direct3D drops execute buffers entirely —
`IDirect3DDevice3` only ever exposes `DrawPrimitive`/`DrawIndexedPrimitive` (matches `DX2`/`DX3`'s
own already-proven choice of avoiding execute buffers, but now it's the *only* option, not a
workaround for a broken path). Concretely new vs. `DX2`/`DX3`, all confirmed real by this spike:

- **DirectDraw object AND every surface upgrade to v4**: `IDirectDraw4`, `IDirectDrawSurface4`,
  `DDSURFACEDESC2` (not just `DDSURFACEDESC`), `DDSCAPS2` (not just `DDSCAPS`). Unlike `DX3`'s
  "upgrade only the top DirectDraw object" change, DX5 needs *every* surface to be v4 —
  `IDirectDraw4::CreateSurface` returns `LPDIRECTDRAWSURFACE4`, and `IDirect3D3::CreateDevice`
  requires a v4 surface specifically.
- **Direct3D device/viewport**: `IDirect3D3`/`IDirect3DDevice3`/`IDirect3DViewport3`. Texture stays
  `IDirect3DTexture2` — unchanged, no `IDirect3DTexture3` exists (confirmed by `grep`).
- **`DrawPrimitive`'s vertex-type parameter changes** from the `D3DVERTEXTYPE` enum
  (`D3DVT_TLVERTEX`) to a `DWORD` FVF (Flexible Vertex Format) bitmask. `D3DFVF_TLVERTEX` is a
  predefined macro confirmed to expand to the exact same bit layout `D3DTLVERTEX` already has
  (`D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_SPECULAR|D3DFVF_TEX1`) — the same vertex struct still
  works, submitted through the new parameter shape.
- **`IDirect3DViewport3::Clear2` takes explicit color/z/stencil *values*** — unlike
  `IDirect3DViewport(2)::Clear()`, which `DX2-24` found has no such parameter at all, forcing a
  manual `Lock()`-the-Z-buffer workaround there. This is a real, working capability upgrade DX5
  can use instead of that workaround.

## Result: everything above is real and works, once two of the spike's own bugs were found and fixed

| # | Test | What it proves | Result |
|---|---|---|---|
| A | `ddV1->QueryInterface(IID_IDirectDraw4, &dd4)`, then `CreateSurface`(`DDSURFACEDESC2`)/`Lock`/`Unlock` via v4 | Whether `IDirectDraw4` is a real, fully-functional 2D surface layer | ✅ Works |
| B | `dd4->QueryInterface(IID_IDirect3D3, &d3d3)`, `d3d3->CreateDevice(IID_IDirect3DRGBDevice, v4surface, &device, nullptr)` | Whether the v4-surface-rooted Direct3D device chain works | ✅ Works |
| C | `IDirect3DViewport3` creation, `SetViewport2` (same `D3DVIEWPORT2` struct as `DX2`/`DX3`) | Whether viewport setup is unchanged | ✅ Works |
| D | `device->DrawPrimitive(D3DPT_TRIANGLELIST, D3DFVF_TLVERTEX, verts, 3, 0)` | Whether FVF-based submission of the same `D3DTLVERTEX` struct still Gouraud-interpolates correctly | ✅ Works — non-black, blended readback |
| E | `DrawIndexedPrimitive` + real `D3DZB_TRUE`/`D3DCMP_LESS` Z-test, two overlapping quads | Whether depth-test occlusion is real through the new FVF path | ✅ Works — near (red) quad correctly wins, exact `(255,0,0)` readback |
| F | Real 2×2 texture via `IDirectDrawSurface4`+`IDirect3DTexture2` (unchanged interface, obtained from a v4 surface), sampled via `DrawIndexedPrimitive` | Whether texture sampling still works when the owning surface is v4 | ✅ Works — both sampled corners exact |
| G | `IDirect3DViewport3::Clear2(1, &fullRect, D3DCLEAR_TARGET\|D3DCLEAR_ZBUFFER, color, z, stencil)`, then a z=0.8 quad against a Clear2 z=0.75 | Whether `Clear2` is a real, working replacement for the `DX2`/`DX3` manual-Z-buffer-Lock workaround | ✅ Works — exact requested clear color read back, and the z=0.8 quad is correctly Z-rejected against the Clear2-written 0.75 depth |

**Two real bugs found in the spike itself, not in Wine/Direct3D5, corrected before reaching the
result above (worth remembering for the actual backend implementation):**

1. **`Clear2`'s `count=0, rects=nullptr` does NOT mean "clear the whole surface"** — it clears
   *nothing at all* (silently; the `HRESULT` still reports success). An explicit
   `D3DRECT{0,0,w,h}` with `count=1` is required, exactly matching the old `Clear()`'s own already-
   established calling convention (`DX2-0`/`DX30-0`). The first attempt at Test E/G, using
   `count=0`, produced confusing not-fully-explained pixel values (leftover content from a prior
   draw, since nothing was actually cleared) that looked superficially like a Wine rendering bug
   but were not.
2. **Sampling the exact screen-center pixel of a full-viewport quad built from 2 diagonally-split
   triangles** (the standard `(0,0)-(w,0)-(w,h)-(0,h)` quad, split along the `(0,0)-(w,h)`
   diagonal) **lands exactly on the triangle seam** and can read an averaged/edge-rasterized value
   instead of either triangle's actual interior color. Sample off that diagonal (e.g. `(w/4,
   3h/4)`) for a reliable single-triangle reading.

## Practical conclusion

DX5's 3D layer is architecturally identical to `DX2`/`DX3`'s own CPU-transform-then-submit
pipeline, with two real, mechanical changes: (1) `DrawPrimitive`/`DrawIndexedPrimitive`'s
vertex-type parameter becomes `D3DFVF_TLVERTEX` instead of `D3DVT_TLVERTEX` (same struct,
different selector), and (2) `IDirect3DViewport3::Clear2` can now do a real one-call depth+color
clear with explicit values, replacing `DX2`/`DX3`'s manual Z-buffer `Lock()` workaround. DX5's 2D
layer needs a more invasive port than `DX3`'s (every surface type, not just the top DirectDraw
object, moves to v4), but every v4 call behaves exactly as its v1/v2 predecessor did.

## Files

- `dx5_spike1_ddraw4_d3d3.cpp` — the spike above.

Build (MinGW cross, ccache-wrapped):

```bash
ccache x86_64-w64-mingw32-g++ -O0 -g -c dx5_spike1_ddraw4_d3d3.cpp -o dx5_spike1_ddraw4_d3d3.o
x86_64-w64-mingw32-g++ -O0 -g -o dx5_spike1_ddraw4_d3d3.exe dx5_spike1_ddraw4_d3d3.o -lddraw -ldxguid -luser32 -lgdi32 -lkernel32
```

Run:

```bash
export WAYLAND_DISPLAY=       # unset -- Wine prefers Wayland over X11 if this is set at all
export DISPLAY=:99
export WINEPREFIX="$HOME/.wine-cna-dx1"
wine dx5_spike1_ddraw4_d3d3.exe
```
