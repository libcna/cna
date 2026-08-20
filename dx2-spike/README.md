# `dx2-spike` — `DX2-0` existence-gate spike findings (2026-07-20)

Everything here has actually been run under real Wine `ddraw.dll`/`d3d.dll` (Wine 10.0~repack-6,
`WINEPREFIX=$HOME/.wine-cna-dx1`, `DISPLAY=:99` Xvfb, `WAYLAND_DISPLAY` unset). None of it is a
sketch. Kept checked in per the same rationale as `dx9-spike/README.md`: rewriting working,
proven spike code from a plan's prose later would be a waste.

## Result (updated): 2D proven; execute-buffer (v1) 3D fails, but `IDirect3DDevice2::DrawPrimitive` (v2) 3D WORKS

**Resolution found (see `dx2_spike6_v2.cpp`).** Following a suggestion to isolate the failure
more precisely rather than conclude "old D3D is broken in Wine" wholesale: the ancient
execute-buffer opcode-interpreter path (`IDirect3DDevice::Execute`, `D3DOP_TRIANGLE`) is the
specific thing that's broken here — the separate immediate-mode `DrawPrimitive` API, added in
the very next interface revision (`IDirect3D2`/`IDirect3DDevice2`, DirectX 3 SDK), **renders
correctly**: real Gouraud-interpolated per-pixel color, reproducible across repeated runs,
using the exact same render target setup (offscreen `DDSCAPS_3DDEVICE` surface + attached
`DDSCAPS_ZBUFFER`), the same `D3DTLVERTEX` data, and the same disabled cull/z-test/lighting
render states as the failing execute-buffer variants above. Only the draw call mechanism
changed: `device->DrawPrimitive(D3DPT_TRIANGLELIST, D3DVT_TLVERTEX, verts, 3, 0)` via
`IDirect3DDevice2` instead of building an execute-buffer instruction stream and calling
`IDirect3DDevice::Execute` via `IDirect3D`/`IDirect3DDevice` (v1).

**Practical conclusion:** real, verified 3D rendering under Wine for this backend family is
achievable, but only through the `IDirect3DDevice2`+ `DrawPrimitive`/`DrawIndexedPrimitive` API
surface (`IDirect3D2`, `IDirect3DViewport2`, `IDirect3DDevice2` — first shipped in the DirectX 3
SDK), not through the pure DirectX 2 SDK's `IDirect3D`/`IDirect3DDevice` execute-buffer-only
API. The original all-black findings below (rounds 1-5) remain valid and are kept as the record
of what was ruled out before this was found.

**Full validation (see `dx2_spike7_full.cpp`), both passing:**

- **`DrawIndexedPrimitive` + Z-test genuinely enabled** (`D3DRENDERSTATE_ZENABLE = D3DZB_TRUE`,
  `D3DRENDERSTATE_ZFUNC = D3DCMP_LESS`): a farther green full-viewport quad (`z=0.8`) drawn
  first, then a nearer red full-viewport quad (`z=0.2`) drawn second — every sampled pixel
  reads pure `(255,0,0)`, confirming real depth-test occlusion, not just "didn't crash."
- **Texture sampling via `DrawIndexedPrimitive`** (not execute-buffer): a real 2x2 texture
  (top-left/bottom-right red, top-right/bottom-left blue) bound via
  `IDirect3DTexture2::GetHandle` + `D3DRENDERSTATE_TEXTUREHANDLE`, sampled across a
  white-vertex-colored quad — the readback shows the exact four-quadrant texture layout with no
  color-key or blend artifacts. This also lays to rest the `DrawPrimitive`-path theory of
  "unbound texture stage samples black" that was a live hypothesis under the execute-buffer
  path — texturing behaves correctly here.

This is now a fully de-risked design: **DX2's 3D layer will be implemented via
`IDirect3D2`/`IDirect3DDevice2`/`IDirect3DViewport2`/`IDirect3DTexture2`'s `DrawPrimitive`/
`DrawIndexedPrimitive` immediate-mode API**, confirmed by the project owner as the intended
approach over staying strictly within the literal DirectX-2-SDK execute-buffer-only surface.

**Phase O3 pre-work finding (`dx2_spike8_zclear.cpp`):** `IDirect3DViewport`/`IDirect3DViewport2`'s
`Clear()` has no depth/color *value* parameter at all (only `IDirect3DViewport3::Clear2`, DX5+,
takes explicit `dwColor`/`dvZ`/`dwStencil`) — so it cannot implement `IGraphicsBackend::ClearDepth`/
`ClearColorAndDepth`'s arbitrary caller-requested depth value. Spiked the alternative directly:
`Lock()` the `DDSCAPS_ZBUFFER` surface itself and write raw 16-bit values by hand (confirmed layout:
`lPitch = width*2`, a plain unsigned 16-bit value per pixel, 0=near/0xFFFF=far). A far quad
(`sz=0.9`) was then submitted with `D3DRENDERSTATE_ZENABLE=D3DZB_TRUE`/`ZFUNC=D3DCMP_LESS` against
a Z-buffer manually pre-filled with a near value (`0x1000`), with **no** `viewport->Clear()` call
touching it at all — the far quad was correctly rejected everywhere (readback stayed the baseline
red, never showed green), proving a **manually-written** Z-buffer is genuinely respected by the
real depth test, not just Z values a real draw itself writes (already proven separately in
`dx2_spike7_full.cpp` test A). `Dx2GraphicsBackend`'s `ClearDepth`/`ClearColorAndDepth` therefore
mirror `Clear(r,g,b,a)`'s own existing direct-Lock()-and-fill approach exactly, just targeting the
Z-buffer surface instead of the color surface — no dependence on `viewport->Clear()`'s ambiguous
default-value semantics for this.

**Phase O4 pre-work finding (`dx2_spike9_dualcap_texture.cpp`):** `Dx2TextureBackend`'s existing
surfaces (used for both regular XNA `Texture2D`s and `RenderTarget2D`s) only had `DDSCAPS_
OFFSCREENPLAIN` — no `DDSCAPS_TEXTURE` — since Phase O2/O3 only needed plain 2D `Lock`/`Blt`
access. 3D texture sampling requires `QueryInterface(IID_IDirect3DTexture2)`, which real
DirectDraw only allows on a surface created with `DDSCAPS_TEXTURE`, and surface caps are immutable
after `CreateSurface` — so this had to be spiked before deciding to add the cap to every texture's
creation call. Confirmed: a surface created with **both** `DDSCAPS_OFFSCREENPLAIN | DDSCAPS_
TEXTURE` together (no explicit pixel format, matching the existing "let Wine pick the native
format" convention) supports plain 2D `Lock()`-style writes/`Blt` **and** real `IDirect3DTexture2`
sampling via `DrawIndexedPrimitive` on the *same surface instance* — verified by writing a 2x2
texture via a direct 2D-style `Lock()`, then sampling it correctly (exact 4-quadrant readback) via
a 3D draw. `CreateOffscreenSurface` now requests both caps unconditionally.

## Original result before the v2 breakthrough: 2D layer proven; 3D execute-buffer rendering NOT achieved

**2D (DirectDraw v1):** not re-spiked here — DX1's spike and full backend already prove this
API surface works end-to-end (`plans/plan_dx1.md` `DX1-0`), and DX2's roadmap (`plans/plan_dxold.md`) calls
DirectDraw v1 unchanged between DX1 and DX2. No new risk here.

**3D (Direct3D v1 execute buffer):** every API call in the pipeline succeeds
(`DirectDrawCreate`, `CreateSurface` for an offscreen `DDSCAPS_3DDEVICE` target and a
`DDSCAPS_ZBUFFER`, `QueryInterface(IID_IDirect3DRGBDevice)`, `CreateViewport`/`SetViewport`,
`CreateExecuteBuffer`/`Lock`/`Unlock`, `BeginScene`/`Execute`/`EndScene`), and Wine's own
`+d3d,+ddraw` debug trace confirms a mechanically correct pipeline all the way through: a real
complete FBO (`WINED3DFMT_B8G8R8X8_UNORM` color + `WINED3DFMT_D16_UNORM` depth, 64x64), correct
`POSITIONT`/`COLOR`/`TEXCOORD` vertex stream parsing at stride 32 (matching `D3DTLVERTEX`
exactly), the execute-buffer instruction stream parsed correctly (`TRIANGLE (1) v1:0 v2:1
v3:2`), and `draw_primitive ... Draw completed`. **The final pixel readback is unconditionally
black (0,0,0) in every variant tried.**

### What was tried and ruled out (each one a real rebuild + real Wine run, not a guess left
untested)

| # | Variant | Result |
|---|---|---|
| 1 | `D3DLVERTEX` + `D3DOP_PROCESSVERTICES(D3DPROCESSVERTICES_TRANSFORM)` + explicit identity `D3DOP_STATETRANSFORM` (world/view/projection) + `D3DOP_TRIANGLE` (`dx2_spike.cpp`) | black |
| 2 | Pre-transformed `D3DTLVERTEX` submitted directly, bypassing `PROCESSVERTICES` entirely (`dx2_spike2.cpp`) | black |
| 3 | `D3DRENDERSTATE_CULLMODE = D3DCULL_NONE` | black |
| 4 | `D3DRENDERSTATE_ZENABLE = D3DZB_FALSE` | black |
| 5 | `D3DRENDERSTATE_LIGHTING = FALSE` (in case the FF-emulation still re-lit pre-lit TLVERTEX) | black |
| 6 | `D3DCOLOR` alpha fixed to `0xFF......` (was `0x00......`, fully transparent — a real bug, fixed, but not the root cause) | black |
| 7 | Triangle deliberately oversized to `(-200,-200)-(400,400)`, guaranteeing full-viewport coverage regardless of any coordinate-scale mistake | black |
| 8 | Triangle re-scaled to NDC-like `(-1,-1)-(3,3)` range, in case Wine's execute-buffer path double-applies the viewport scale/offset transform even to already-transformed `TLVERTEX` | black |
| 9 | Readback via `IDirectDrawSurface::Lock()` directly on the render-target surface | black |
| 10 | Readback via `BltFast()` into a fresh plain sysmem surface first (rules out a Lock()-specific stale-shadow-copy sync bug) | black |
| 11 | Real 2x2 opaque white texture created, bound via `D3DRENDERSTATE_TEXTUREHANDLE` (rules out an unbound-texture-stage-samples-black theory, since `D3DTLVERTEX` always carries `tu`/`tv`) | black |
| 12 | Render target as offscreen `DDSCAPS_OFFSCREENPLAIN \| DDSCAPS_3DDEVICE` surface (`dx2_spike2.cpp`) | black |
| 13 | Render target as the **primary surface**, `DDSCL_EXCLUSIVE \| DDSCL_FULLSCREEN` (the classic DX2/DX3 SDK "Direct3D immediate mode" tutorial pattern) (`dx2_spike3.cpp`) | black |
| 14 | `QueryInterface(IID_IDirect3DHALDevice)` instead of `IID_IDirect3DRGBDevice` (`dx2_spike5_hal.cpp`) | black (Wine appears to route both to the same internal path) |

A real Wine implementation detail note, not a cause: `D3DRENDERSTATE_COLORKEYENABLE` defaults to
enabled at device/scene init, but the trace confirms it's a `state_nop ... nop in current pipe
config` — i.e., genuinely inert for this pipeline configuration, not silently discarding
fragments.

### What this rules IN

The underlying GL/Xvfb stack itself is not broken — DX1's DirectDraw `Blt`-based 2D rendering
in this exact sandbox is proven correct and was visually confirmed by the project owner
(`plans/plan_dx1.md`). The failure is isolated to Direct3D v1's execute-buffer rasterization path
specifically (or possibly Wine's `wined3d` fixed-function shader emulation as invoked through
this specific, rarely-exercised 30-year-old API surface).

### What was NOT available to try

No real Proton runtime is installed in this sandbox (only system Wine 10.0~repack-6) — the
`~/.wine-cna-d3d12-protonrun` prefix name is just a `WINEPREFIX` directory name from prior work,
not an actual Proton install. Testing against Proton's bundled (often Valve-patched) `wined3d`/
`DXVK` stack, a different Wine version, or real Windows was not possible here.

## Phase O9 follow-up spike (2026-07-21): specular/wireframe/anisotropic re-verification

`dx2_spike10_specular_wireframe_aniso.cpp`, run against the same `~/.wine-cna-dx1` prefix, three
questions Phase O9 (`plans/plan_dx2.md` design decision 13) needed answered before writing any CPU
lighting code or flipping any `SupportsCapability` bit:

- **Test C — `D3DRENDERSTATE_SPECULARENABLE` + `D3DTLVERTEX::specular`**: a full-viewport quad with
  vertex `color` (diffuse) set to opaque BLACK and `specular` set to opaque RED. With
  `SPECULARENABLE=TRUE`, readback at the center is pure `(255,0,0)` — the specular channel is
  genuinely, additively composited by real Direct3D fixed-function hardware, independent of the
  diffuse/texture-modulate stage. With `SPECULARENABLE=FALSE` (same vertices, same draw), readback
  is pure `(0,0,0)` — confirming the red wasn't leaking in from the diffuse channel by mistake.
  **Real, working, exactly the mechanism real XNA's shader-based specular-add needs.**
- **Test D — `D3DRENDERSTATE_FILLMODE` (`D3DFILL_SOLID` vs `D3DFILL_WIREFRAME`)**: a triangle
  covering the render target's center. A point well inside the triangle reads the triangle's own
  color (`(255,255,255)`) in `SOLID` mode, and the cleared background color (`(0,0,0)`) in
  `WIREFRAME` mode — **real, confirmed distinctness**, not a silent solid-fill fallback.
- **Test E — `D3DRENDERSTATE_ANISOTROPY`/`D3DTFN_ANISOTROPIC` vs `D3DTFN_LINEAR`/`D3DTFN_POINT`**:
  an 8×8 checkerboard texture heavily minified onto a 64×64 quad (UV range 0..4), sampled at 25
  points across the surface under all four filter configurations. **Every single sampled point was
  byte-identical across all four configurations** — this environment's software RGB Direct3D
  device does not implement point/linear/anisotropic minification filtering distinctly at all (not
  even point-vs-linear differ, let alone anisotropic-vs-linear). Confirms `DX2-95`'s decision to
  keep `SupportsCapability(AnisotropicFiltering)` reporting `false`, now backed by a real negative
  result instead of "never verified."

## Files

- `dx2_spike10_specular_wireframe_aniso.cpp` — Phase O9 follow-up: specular-add, wireframe,
  anisotropic filtering (see section above)
- `dx2_spike.cpp` — round 1: `D3DLVERTEX` + `PROCESSVERTICES(TRANSFORM)` full pipeline
- `dx2_spike2.cpp` — round 2: pre-transformed `D3DTLVERTEX` direct, offscreen `3DDEVICE` target;
  most of the ruled-out variants above were iterated in this file
- `dx2_spike3.cpp` — round 3: primary-surface, fullscreen-exclusive render target variant
- `dx2_spike4.cpp` — round 4: real bound white texture variant
- `dx2_spike5_hal.cpp` — round 5: `IDirect3DHALDevice` variant

Build (MinGW cross, ccache-wrapped):

```bash
ccache x86_64-w64-mingw32-g++ -O0 -g -c dx2_spikeN.cpp -o dx2_spikeN.o
x86_64-w64-mingw32-g++ -O0 -g -o dx2_spikeN.exe dx2_spikeN.o -lddraw -ldxguid -luser32 -lgdi32 -lkernel32
```

Run:

```bash
export WAYLAND_DISPLAY=       # unset — Wine prefers Wayland over X11 if this is set at all
export DISPLAY=:99
export WINEPREFIX="$HOME/.wine-cna-dx1"
wine dx2_spikeN.exe
```
