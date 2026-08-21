# `dx6-spike` — `DX6-0` existence-gate spike findings (2026-07-21)

Run under real Wine `ddraw.dll`/`d3d.dll` (Wine 10.0~repack-6, `WINEPREFIX=$HOME/.wine-cna-dx1`,
`DISPLAY=:99` Xvfb, `WAYLAND_DISPLAY` unset) — same environment `dx1-spike`/`dx2-spike`/
`dx3-spike`/`dx5-spike` used.

## What DX6 concretely means, vs. `DX5`

Unlike `DX2`→`DX3`→`DX5`'s progression (each a genuine new COM interface revision), **DX6
introduces NO new `IDirect3D`/`IDirect3DDevice`/`IDirectDraw` interface at all** — confirmed by
inspecting the real MinGW headers: there is no `IDirect3D4`/`IDirect3DDevice4`. `IDirect3D3`/
`IDirect3DDevice3`/`IDirect3DViewport3`/`IDirectDraw4` (the exact interfaces `DX5` already uses)
are still the correct ones for DX6. DX6 (1998)'s own delta is purely new render
states/capabilities on the *same* interface: real stencil buffer operations
(`D3DRENDERSTATE_STENCILENABLE`/`STENCILFUNC`/`STENCILFAIL`/`STENCILZFAIL`/`STENCILPASS`/
`STENCILREF`/`STENCILMASK`/`STENCILWRITEMASK`, all confirmed present in `d3dtypes.h`),
multitexturing (`SetTextureStageState`/`D3DTSS_*`, also confirmed present), and DXTn compression
(not spiked — see `plans/plan_dx6.md` for why it's out of scope).

**This spike tests only stencil** — the one DX6-era capability `DX2`/`DX3`/`DX5` have all
explicitly documented as "no real stencil buffer exists at this DirectX era (DX6+)." Multitexture
was investigated but deferred (see `plans/plan_dx6.md` design decision 6): the header-defined
`D3DTLVERTEX`/`D3DFVF_TLVERTEX` vertex format carries only a single texture-coordinate pair
(`D3DFVF_TEX1`), so genuine two-independent-UV multitexture would require a second vertex layout
(`D3DFVF_TEX2`) and extending the whole CPU transform/clip pipeline to carry a second UV channel —
a disproportionate scope increase for this plan, deferred rather than half-implemented.

## Result: real stencil write + test, confirmed on the first try

| # | Test | What it proves | Result |
|---|---|---|---|
| A | `CreateSurface` with `DDPF_ZBUFFER\|DDPF_STENCILBUFFER`, `dwZBufferBitDepth=32`, `dwStencilBitDepth=8` (a D24S8-equivalent combined depth+stencil format), then `AddAttachedSurface`+`CreateDevice` off it | Whether a stencil-capable Z-buffer surface and device are real, not stubs | ✅ Works |
| B | `Clear2` with an explicit stencil value (0), then a left-half-only quad with `STENCILFUNC=ALWAYS`, `STENCILPASS=REPLACE`, `STENCILREF=1` | Whether a real stencil WRITE happens | ✅ Works — left half reads the drawn color, right half stays the Clear2 background |
| C | A full-screen quad with `STENCILFUNC=EQUAL`, `STENCILREF=1` drawn on top | Whether a real stencil TEST correctly gates the draw per-pixel based on the buffer state Test B wrote | ✅ Works — left half (stencil==1) shows the new color, right half (stencil==0) correctly rejects the draw and keeps the old background |

Every readback matched its predicted value exactly, on the first run — no spike-authoring bugs
this time (unlike `dx5-spike`'s `Clear2`-rect and diagonal-seam gotchas, both already known from
that prior spike and correctly avoided here).

## Practical conclusion

DX6's 3D layer is `DX5`'s own architecture (same `IDirect3D3`/`IDirect3DDevice3`/
`IDirect3DViewport3`/v4 surfaces) with real stencil operations added: a stencil-capable Z-buffer
surface, and `ApplyDepthStencilState`'s stencil parameters wired to the real
`D3DRENDERSTATE_STENCIL*` render states instead of being accepted-and-ignored.

## Files

- `dx6_spike1_stencil.cpp` — the spike above.

Build (MinGW cross, ccache-wrapped):

```bash
ccache x86_64-w64-mingw32-g++ -O0 -g -c dx6_spike1_stencil.cpp -o dx6_spike1_stencil.o
x86_64-w64-mingw32-g++ -O0 -g -o dx6_spike1_stencil.exe dx6_spike1_stencil.o -lddraw -ldxguid -luser32 -lgdi32 -lkernel32
```

Run:

```bash
export WAYLAND_DISPLAY=       # unset -- Wine prefers Wayland over X11 if this is set at all
export DISPLAY=:99
export WINEPREFIX="$HOME/.wine-cna-dx1"
wine dx6_spike1_stencil.exe
```
