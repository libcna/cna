# `dx8-spike` — `DX8-0` existence-gate spike findings (2026-07-21)

Run under DXVK 2.6.0, `DISPLAY=:99` Xvfb, `WAYLAND_DISPLAY` unset. `dx8_spike1`/`dx8_spike2` below
were run against `WINEPREFIX=$HOME/.wine-cna-d3d11` (the same prefix D3D9/D3D11/D3D12 already use)
with `d3d8` added as a native DLL override pointing at DXVK's own `d3d8.dll.so` (the packaged
`dxvk-setup` script's hardcoded DLL list predates D8VK's merge into DXVK and never installs `d3d8`
on its own). **The real backend and its own CTest suite use a separate, dedicated
`~/.wine-cna-dx8` prefix instead** — see "Two further runtime bugs" below for why.

## Scope decision (made with the project owner before any code was written)

DX8 targets **fixed-function 3D only**, matching `DX1`..`DX7`'s own CPU-transform-and-submit
shape — **not** real Shader Model 1.x programmable shaders. Real XNA effects
(`BasicEffect`/`SkinnedEffect`/etc.) need `ps_2_0`+ regardless of SM1.x support
(`docs/directx-legacy-backends-analysis.md` §3.1), so building a real SM1.x pipeline would not make
`CreateEffectBackend` usable for any actual XNA content — it would still have to throw. Fixed
function keeps this backend's scope proportionate to what it can actually deliver.

## What DX8 concretely means, vs. `DX1`..`DX7`

DX8 (2000) is architecturally very different from every backend in this family so far:

1. **No DirectDraw at all.** "DirectDraw+Direct3D merged" (`plans/plan_dxold.md`'s own DX8 row) — a
   single `IDirect3D8::CreateDevice(adapter, type, hFocusWindow, behaviorFlags, &presentParams,
   &device)` call creates BOTH the device and its own swap chain. No separate `ddraw` object, no
   manual "shadow backbuffer + `Blt` to primary" trick this whole family needed since `DX2-0`.
2. **mingw-w64's x86_64 target ships NO real `d3d8` import library** — only `libd3d8thk.a` (an
   unrelated internal "thunk" library) exists for x86_64; only the i686/32-bit target has a real
   `libd3d8.a`. **DXVK's own `/usr/lib/dxvk/wine64/d3d8.dll.a` exports the real `Direct3DCreate8`
   symbol** and was linked against directly (`-L/usr/lib/dxvk/wine64 -ld3d8`) — no 32-bit
   cross-compile needed.
3. **`D3DTLVERTEX`/`D3DFVF_TLVERTEX` are GONE from the real headers entirely.** D3D8 introduced the
   generic FVF/vertex-declaration model — the caller defines its own struct matching the FVF byte
   layout. The FVF *values* (`D3DFVF_XYZRHW`/`DIFFUSE`/`SPECULAR`/`TEX1`) still exist unchanged, so
   this spike's `Dx8TLVertex` struct reproduces the exact same byte layout the old macro implied.
4. **Render state names changed from `D3DRENDERSTATE_*` to `D3DRS_*`** (the underlying
   `D3DRENDERSTATETYPE` enum *values* are unchanged, e.g. `D3DRS_ZENABLE == 7`, same as
   `D3DRENDERSTATE_ZENABLE` in every prior header) — a naming-convention break, not a behavior one.
5. **`DrawPrimitive`/`DrawIndexedPrimitive` require a bound vertex buffer** (`SetStreamSource`) —
   the CPU-memory-pointer submission `DX2`..`DX7` all used is `DrawPrimitiveUP`/
   `DrawIndexedPrimitiveUP` instead, which additionally require the FVF to be set via
   `SetVertexShader(fvfValue)` FIRST — passing a raw FVF `DWORD` directly as if it were a real
   vertex-shader handle (D3D9 later split this into a separate `SetFVF()` method, a real historical
   D3D8-only idiom, confirmed to actually work here, not just compile).
6. **No `GetRenderTargetData`** (a D3D9-only addition) — readback here uses D3D8's own
   `CreateImageSurface` (a lockable system-memory surface) + `CopyRects` from the real back buffer.
7. **Stencil is real** (`D3DRS_STENCILENABLE`/etc., unchanged in shape from `DX6`/`DX7`) against a
   `D3DFMT_D24S8` auto-depth-stencil surface attached directly via `D3DPRESENT_PARAMETERS`.

## Result: every test passed, after one real fix — no further spike-authoring bugs

| # | Test | What it proves | Result |
|---|---|---|---|
| A | `Direct3DCreate8` + `IDirect3D8::CreateDevice` via the DXVK-installed `d3d8.dll` | Whether real device creation works through DXVK | ✅ Works (log confirms `D3D9DeviceEx`/DXVK genuinely handling it — "The D3D9 interface is now operating in D3D8 compatibility mode", the real DXVK/D8VK architecture) |
| B | `Clear(rect_count=0, rects=nullptr, TARGET\|ZBUFFER\|STENCIL, ...)` | Whether `count=0` clears the WHOLE target (D3D9 convention) or nothing (the `DX5`-`DX7` `IDirect3DViewport::Clear2` gotcha) | ✅ Clears everything — exact `(10,10,10)` readback, confirming the D3D9-style convention, NOT the Viewport-Clear2 gotcha |
| C | `SetVertexShader(rawFvfValue)` (not a real shader handle) + `DrawIndexedPrimitiveUP` — Gouraud quad, no vertex buffer at all | Whether the FVF-via-SetVertexShader idiom actually works | ✅ Works — real Gouraud interpolation confirmed at both diagonal corners |
| D | `DrawPrimitiveUP`, far(blue)-then-near(red) overlapping triangles, `D3DRS_ZENABLE=TRUE` | Whether real Z-test occlusion works | ✅ Works — exact red readback (near wins) |
| E | `CreateTexture` + `SetTexture` + `SetTextureStageState(D3DTSS_COLOROP, D3DTOP_MODULATE)` — pre-empting `DX7-0`'s own real finding that the legacy blend-mode render state got rejected outright on that device revision | Whether the modern stage-state texture mechanism works from the start | ✅ Works — exact solid-red readback, first try, no rejected-render-state surprise this time |
| F | Stencil write (`STENCILFUNC=ALWAYS,STENCILPASS=REPLACE,STENCILREF=1`, left-half quad) then test (`STENCILFUNC=EQUAL`, full-screen quad) against the auto depth-stencil surface | Whether real stencil write+test works | ✅ Works — exact green/black after write, red/black after test |
| G | `Present()` with a real window | Whether the real DXVK-backed swap chain presents without error | ✅ Works — real Vulkan swapchain confirmed in the log (`VK_FORMAT_B8G8R8A8_SRGB`, `VK_PRESENT_MODE_IMMEDIATE_KHR`) |

**One real bug found and fixed** (not a DXVK/Wine limitation): the first `CreateDevice` attempt
failed with `D3DERR_INVALIDCALL` (`hr=0x8876086c`) because `D3DPRESENT_PARAMETERS.
FullScreen_PresentationInterval` was set to `D3DPRESENT_INTERVAL_IMMEDIATE` while `Windowed=TRUE` —
that field is documented as meaningful only when `Windowed=FALSE`; DXVK's own D3D8-compat validation
genuinely rejects it in windowed mode (real API behavior, not a Wine bug). Fixed by using
`D3DPRESENT_INTERVAL_DEFAULT` (0) for windowed mode. After that one fix, every remaining test passed
on the very next run with zero further issues.

## Practical conclusion

- **Link against DXVK's own `/usr/lib/dxvk/wine64/d3d8.dll.a`** for `Direct3DCreate8` (no MinGW
  x86_64 import library exists for real d3d8).
- **`d3d8` must be added as a native DLL override to the Wine prefix by hand** (backing up the
  existing Wine-builtin `d3d8.dll` to `d3d8.dll.old` and symlinking DXVK's `.dll.so` in its place,
  mirroring `dxvk-setup`'s own `install_dll` steps exactly) since the packaged setup script doesn't
  know about `d3d8` yet.
- **`Clear(0, nullptr, ...)` clears everything** — use the D3D9-style convention, not `DX5`-`DX7`'s
  `Viewport::Clear2` gotcha.
- **`FullScreen_PresentationInterval` must be `D3DPRESENT_INTERVAL_DEFAULT` for windowed mode** —
  a real, load-bearing `D3DPRESENT_PARAMETERS` construction rule for this backend.
- **Fixed-function submission works via `SetVertexShader(fvfValue)` + `DrawPrimitiveUP`/
  `DrawIndexedPrimitiveUP`**, using a hand-defined `Dx8TLVertex` struct matching the old
  `D3DTLVERTEX` byte layout (the macro itself no longer exists in the real headers).
- **Texture blending via `SetTextureStageState`/`D3DTSS_COLOROP` works from the start** — no repeat
  of `DX7-0`'s own `D3DRENDERSTATE_TEXTUREMAPBLEND`-rejection surprise, since this spike used the
  modern mechanism from the beginning rather than the legacy render state.
- **Stencil is real and unchanged in shape** from `DX6`/`DX7`.

## Files

- `dx8_spike1_fixedfunction.cpp` — the `DX8-0` existence-gate spike (device bring-up, Clear,
  Gouraud FVF draw, Z-test, texturing, stencil, Present).
- `dx8_spike2_rt_copyrects.cpp` — follow-up isolating `ReadBackbuffer`'s `CopyRects`-from-a-
  render-target-texture mechanism (all 5 sub-tests pass; that mechanism was never the real bug —
  see `Dx8GraphicsBackend::SetVirtualResolution`'s own fix in the backend source instead, a real
  logical-render-target/virtual-resolution size mismatch, not a Wine/DXVK issue).
- `dx8_spike3_sdl_load_order.cpp` — isolates Bug 1 above (SDL3's own internal `dxgi.dll` probe).
- `dx8_spike4_double_present.cpp` — isolates Bug 2 above (RADV double-`Present()` bug).

Build (MinGW cross, ccache-wrapped, linking DXVK's own d3d8 import library directly):

```bash
ccache x86_64-w64-mingw32-g++ -O0 -g -c dx8_spike1_fixedfunction.cpp -o dx8_spike1_fixedfunction.o
x86_64-w64-mingw32-g++ -O0 -g -o dx8_spike1_fixedfunction.exe dx8_spike1_fixedfunction.o -L/usr/lib/dxvk/wine64 -ld3d8 -luser32 -lgdi32 -lkernel32
```

Run:

```bash
export WAYLAND_DISPLAY=       # unset -- Wine prefers Wayland over X11 if this is set at all
export DISPLAY=:99
export WINEPREFIX="$HOME/.wine-cna-d3d11"
export WINEDEBUG=-all
export DXVK_LOG_LEVEL=info
wine dx8_spike1_fixedfunction.exe
```

One-time prefix setup (only needed once per Wine prefix — already done for `~/.wine-cna-d3d11` in
this environment):

```bash
export WINEPREFIX="$HOME/.wine-cna-d3d11"
wine reg add 'HKEY_CURRENT_USER\Software\Wine\DllOverrides' /v d3d8 /d native /f
SYS32="$WINEPREFIX/drive_c/windows/system32"
mv "$SYS32/d3d8.dll" "$SYS32/d3d8.dll.old"   # back up Wine's own builtin d3d8.dll
ln -s /usr/lib/dxvk/wine64/d3d8.dll.so "$SYS32/d3d8.dll"
```

## Two further runtime bugs found only by the FULL CTest suite (not the DX8-0 spike above)

`DX8-0`'s own spike above only ever exercises raw D3D8 through a bare `CreateWindowExW` window with
no SDL and a single `Present()` per run — the real CNA test suite (SDL-based, calling `Present()`
twice per test: once explicitly in the test's own `Draw()`, once more automatically via the
framework's `EndDraw()`) hit two further real, environment-specific bugs neither spike above
exercised. Both were root-caused with two new, dedicated follow-up spikes.

### Bug 1 — SDL3's own internal dxgi.dll probe crashes under a DXVK-overridden `dxgi`

Running the real (SDL-based) CNA test binaries against the shared `~/.wine-cna-d3d11` prefix
(which has `dxgi` overridden to DXVK/native for the D3D9/D3D11/D3D12 backends) failed at
`SDL_InitSubSystem(SDL_INIT_VIDEO)` with `"No displays available"` — reproduced even in DX7's own
SDL-based smoke test (which uses no DXVK at all) once pointed at that same prefix, and even in a
**minimal SDL3-only binary with zero D3D imports in its own PE import table**
(`dx8_spike3_sdl_load_order.cpp`, built once with `d3d8.dll.a` linked and once as a true `sdl_only`
build with no D3D linkage at all — both fail identically against `~/.wine-cna-d3d11`, both succeed
against DX7's own separate `~/.wine-cna-dx1` prefix which has no `dxgi` override at all).

Root cause: SDL3's own Windows video backend does an internal, **dynamic** `LoadLibrary("dxgi.dll")`
call during `SDL_InitSubSystem(SDL_INIT_VIDEO)` (for HDR/colorimetry capability detection) —
entirely independent of whether the game itself ever touches Direct3D. Under Xvfb (no real monitor
EDID available), DXVK 2.6.0's own EDID/colorimetry-fallback code hits a real bug (confirmed via
`WINEDEBUG=+x11drv,+seh`: an `EXCEPTION_INT_DIVIDE_BY_ZERO`, silently swallowed by Wine's own SEH
`ignoring exception` handler) that corrupts Wine's per-process monitor-enumeration state — so
**any** SDL3 program run in a prefix with `dxgi` overridden to DXVK fails at `SDL_InitSubSystem`,
regardless of whether it ever creates a D3D8/D3D9/D3D10/D3D11 device at all.

**Fix**: DX8 needs its own dedicated prefix (`~/.wine-cna-dx8`), NOT the shared `~/.wine-cna-d3d11`.
Built as a copy of the already-Vulkan/DXVK-proven `~/.wine-cna-d3d11` (to reuse its already-working
Vulkan/registry state rather than `wineboot --init`-ing a bare-fresh prefix, which hit its own
separate, unrelated `dxvk::DxvkError` flakiness when tried), with `d3d8` added as a native override
and **`dxgi` explicitly removed** from the override list (D8VK genuinely needs `d3d9`/`d3d10`/
`d3d10_1`/`d3d10core`/`d3d11` native to function — removing those broke `Direct3DCreate8` itself in
a test — but does NOT need `dxgi` native at all):

```bash
cp -a ~/.wine-cna-d3d11 ~/.wine-cna-dx8
WINEPREFIX=~/.wine-cna-dx8 wine reg add 'HKEY_CURRENT_USER\Software\Wine\DllOverrides' /v d3d8 /d native /f
WINEPREFIX=~/.wine-cna-dx8 wine reg delete 'HKEY_CURRENT_USER\Software\Wine\DllOverrides' /v dxgi /f
SYS32="$HOME/.wine-cna-dx8/drive_c/windows/system32"
mv "$SYS32/d3d8.dll" "$SYS32/d3d8.dll.old"   # if not already a symlink
ln -sf /usr/lib/dxvk/wine64/d3d8.dll.so "$SYS32/d3d8.dll"
```

### Bug 2 — real AMD RADV driver bug: the SECOND consecutive `Present()` call fails

With Bug 1 fixed, the full CTest suite got much further (SDL init + device creation + all
functional pixel checks passed), then crashed at process exit with
`VK_ERROR_SURFACE_LOST_KHR`/`"X connection to :99 broken"`. Isolated with a new, minimal, SDL-free
spike (`dx8_spike4_double_present.cpp`): three consecutive `Clear()`+`Present()` calls on a bare
64x64 `WS_OVERLAPPEDWINDOW` window — the FIRST `Present()` succeeds, the **second** fails with
`VK_ERROR_SURFACE_LOST_KHR` every time, reproducibly, regardless of a real window resize, an
inter-call `Sleep()`, or pumping the Win32 message queue between calls (all ruled out as the cause).
Setting `DXVK_FILTER_DEVICE_NAME=llvmpipe` (forcing DXVK onto the **software** Vulkan device instead
of the real AMD RADV GPU) makes all three `Present()` calls succeed cleanly — proving this is a real
RADV/Mesa driver bug (likely in its `VK_EXT_swapchain_maintenance1`/dynamic-present-mode-switching
path, logged as enabled and "(dynamic: yes)" in every DXVK run here) specific to this sandbox's GPU
+ Xvfb combination, not a defect in `Dx8GraphicsBackend::Present()` itself (which does nothing a
real game wouldn't also do every frame).

**Fix**: `scripts/run-wine-dx8.sh` sets `DXVK_FILTER_DEVICE_NAME=llvmpipe` by default (overridable
via the same env var) for every DX8 test run in this sandbox.
