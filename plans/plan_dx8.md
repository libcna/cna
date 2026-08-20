# DirectX 8 (`IDirect3D8`/`IDirect3DDevice8`, DXVK-delivered, fixed-function) Graphics Backend — Implementation Plan

> **Status (2026-07-21): DONE.** `DX8-0` existence-gate spike complete; implementation phases
> T1-T8 all complete, built, and verified: 20/20 `DX8`-labeled CTests pass
> (`Dx8_LegacyInterfaceDiscipline` + 19 example-based tests), no new regressions in the
> cross-backend suite (`GraphicsBackendCompileDefinitionsTest`/`GraphicsDeviceValidationTest`/
> `GraphicsDeviceCapabilityTest` — the only 3 failures are the same pre-existing, ungated
> `GraphicsDeviceCapabilityTest.SupportsMultipleRenderTargets`/`SupportsOcclusionQuery`/
> `SupportsCustomEffects` gap shared by every DX2/DX3/DX5/DX6/DX7/DX8 backend equally, not
> introduced here). Two real bugs found only by the full test suite, beyond the `DX8-0` spike's
> own findings, are recorded in `dx8-spike/README.md`'s "Two further runtime bugs" section: (1) a
> real `Dx8GraphicsBackend::SetVirtualResolution` bug (the logical render target was created once
> at construction and never resized to match a later `GraphicsDeviceManager`-requested resolution,
> causing `ReadBackbuffer`'s `CopyRects` to fail with a real size mismatch) -- now fixed by having
> `SetVirtualResolution` actually recreate the logical texture/surface/depth-stencil at the new
> size; (2) two environment-specific Wine/DXVK/AMD-RADV-driver bugs unrelated to this backend's own
> code, both worked around in `scripts/run-wine-dx8.sh` (a dedicated `~/.wine-cna-dx8` Wine prefix
> with `dxgi` deliberately NOT overridden to DXVK, and `DXVK_FILTER_DEVICE_NAME=llvmpipe` to avoid a
> real RADV driver bug on the second consecutive `Present()` call). A real D3D8 XYZRHW half-texel
> alignment fix (`Dx8SpriteBatchBackend::Draw`/`Present`'s own quad corners, matching D3D9
> SpriteBatch's equivalent projection-matrix-baked correction) was also needed and applied.

---

## 0. TL;DR

- **New backend: `CNA_GRAPHICS_BACKEND=DX8`.** No temporary-naming concern.
- **Scope decision, made with the project owner before any code was written: fixed-function 3D
  only, matching `DX1`..`DX7`'s own CPU-transform-and-submit shape — NOT real Shader Model 1.x
  programmable shaders.** `docs/directx-legacy-backends-analysis.md` §3.1 already notes real XNA
  effects (`BasicEffect`/`SkinnedEffect`/etc.) need `ps_2_0`+ regardless of SM1.x support, so
  `CreateEffectBackend` would still have to throw for any real XNA content even with a full SM1.x
  pipeline — building one would not make this backend usable for actual CNA content, only bigger.
- **DX8 is architecturally very different from every backend in this family so far** — see §1 for
  the full delta list. The headline changes: no DirectDraw at all ("DirectDraw+Direct3D merged");
  delivered via **DXVK** (not Wine's own `ddraw.dll`/`d3d8.dll`), the same Route B delivery
  mechanism `D3D9`/`D3D11`/`D3D12` already use, reusing DXVK's real `d3d8.dll` (D8VK, merged into
  DXVK 2.0+); `D3DTLVERTEX`/`D3DFVF_TLVERTEX` no longer exist in the real headers (the FVF *values*
  do); render states renamed `D3DRENDERSTATE_*` → `D3DRS_*` (same underlying enum values);
  `DrawPrimitiveUP`/`DrawIndexedPrimitiveUP` replace the old CPU-pointer submission, gated behind a
  real, confusing D3D8-only idiom (`SetVertexShader(rawFvfValue)`); no `GetRenderTargetData`
  (readback uses `CreateImageSurface`+`CopyRects` instead).
- **A real, empirically found build-environment gap**: mingw-w64's x86_64 target ships NO real
  `d3d8` import library at all (only the unrelated `libd3d8thk.a` "thunk" library) — only the
  32-bit/i686 target has one. **DXVK's own `/usr/lib/dxvk/wine64/d3d8.dll.a` exports the real
  `Direct3DCreate8` symbol** and is linked against directly instead — no 32-bit cross-compile
  needed. The packaged `dxvk-setup` script's hardcoded DLL list also predates D8VK's merge into
  DXVK and never installs `d3d8` — added by hand to `~/.wine-cna-d3d11` (the same prefix
  `D3D9`/`D3D11`/`D3D12` already use), mirroring `dxvk-setup`'s own `install_dll` steps exactly.
- **A real, empirically found API bug, fixed in the spike, not just discovered and left**:
  `D3DPRESENT_PARAMETERS.FullScreen_PresentationInterval` must be `D3DPRESENT_INTERVAL_DEFAULT`
  (0) for windowed mode — `D3DPRESENT_INTERVAL_IMMEDIATE` there causes `CreateDevice` to fail with
  `D3DERR_INVALIDCALL` (real DXVK/D3D8-compat validation, not a Wine bug).
- **The whole 2D `SpriteBatch` compositor is redesigned, not ported.** `DX1`..`DX7` all used
  DirectDraw's `Blt`/`BltFast` for a CPU-side pixel compositor specifically because DirectDraw
  offers no other option and has real historical rotation limits. D3D8 has NO DirectDraw at all —
  every CNA backend that already targets a real GPU device without DirectDraw (`D3D9`/`D3D11`/
  `EasyGL`/`Vulkan`/etc.) implements `SpriteBatch` as real GPU-rendered textured quads with real
  alpha blending instead of a CPU compositor, and `DX8` follows that same established pattern —
  just through the fixed-function `SetTextureStageState`/render-state pipeline instead of pixel
  shaders. This is a genuine, deliberate architectural difference from this family's own
  `DX1`-`DX7` 2D layer, not an oversight.
- **Everything else keeps the same spirit as `DX1`..`DX7`**: CPU-side transform + near-plane clip +
  fixed-function submission for 3D geometry (the exact same math, re-expressed against the new
  `Dx8TLVertex` struct/`DrawIndexedPrimitiveUP` call shape), CPU-side `VertexBuffer`/`IndexBuffer`
  storage (Phase O5's own pattern), real stencil (unchanged in shape from `DX6`/`DX7`), same
  lighting/fog/multitexture/envMap/skinning boundary as the rest of this family post-Phase-O9.

---

## 1. What "DirectX 8" concretely means for this backend

| Layer | Symbol(s) used | Introduced in | Never used here |
|---|---|---|---|
| Device + swap chain | `IDirect3D8`, `IDirect3DDevice8`, `D3DPRESENT_PARAMETERS` (via `Direct3DCreate8`) | **DX8 SDK — new, real, DXVK-delivered** | `IDirectDraw*` (does not exist at all at this era) |
| Delivery mechanism | DXVK's own `/usr/lib/dxvk/wine64/d3d8.dll.a`/`d3d8.dll.so` (D8VK, merged into DXVK 2.0+) | **New for this family** — same Route B pattern `D3D9`/`D3D11`/`D3D12` already use | Wine's own built-in `d3d8.dll` (WineD3D) — explicitly gated against via a DXVK-engagement log check, mirroring `D3D9`'s own `run-wine-dxvk9.sh` gate |
| Vertex submission | `SetVertexShader(rawFvfValue)` + `DrawPrimitiveUP`/`DrawIndexedPrimitiveUP` | **DX8 SDK — new signature/idiom, real** | `DrawPrimitive`/`DrawIndexedPrimitive` (require a bound vertex buffer via `SetStreamSource`, not used for the CPU-submission path this family's design relies on) |
| Vertex format | Hand-defined `Dx8TLVertex` struct (same byte layout the old `D3DTLVERTEX` macro implied) + `D3DFVF_XYZRHW`/`DIFFUSE`/`SPECULAR`/`TEX1` | FVF values unchanged since DX5 SDK; the canned struct/macro is GONE | `D3DTLVERTEX`, `D3DVT_*` (neither exists in the real headers any more) |
| Render states | `D3DRS_*` naming (same `D3DRENDERSTATETYPE` enum values as `D3DRENDERSTATE_*`) | **DX8 SDK — naming convention change only** | `D3DRENDERSTATE_*` names |
| Texture blending | `SetTextureStageState`/`D3DTSS_COLOROP`/`D3DTOP_MODULATE` | Real since DX6 SDK, used here from the start | `D3DRENDERSTATE_TEXTUREHANDLE`/`D3DRENDERSTATE_TEXTUREMAPBLEND` (gone; `DX7-0` already found the latter rejected outright on that device revision) |
| Texture/render-target objects | `IDirect3DTexture8`, `IDirect3DSurface8` (via `CreateTexture`/`CreateRenderTarget`/`CreateDepthStencilSurface`) | **DX8 SDK — real Direct3D resources, no DirectDraw surface involved at all** | `IDirectDrawSurface*` |
| Readback | `CreateImageSurface` (lockable system-memory surface) + `CopyRects` from the real back buffer | DX8 SDK shape (`GetRenderTargetData` is D3D9-only, not used) | `GetRenderTargetData` |
| Stencil | `D3DRS_STENCILENABLE`/etc. against a `D3DFMT_D24S8` auto-depth-stencil surface | Unchanged in shape from `DX6`/`DX7` | Two-sided stencil (still a D3D9-era addition) |
| 2D compositor | Real GPU-rendered textured quads through the SAME fixed-function pipeline as 3D geometry, real `D3DRS_ALPHABLENDENABLE`/`SRCBLEND`/`DESTBLEND` | **Redesigned for this backend** — matches `D3D9`/`EasyGL`/etc.'s own established GPU-quad `SpriteBatch` pattern | DirectDraw `Blt`/`BltFast` (does not exist at all at this era) |
| Shaders | *(none)* | DX8 SDK adds Shader Model 1.x (VS 1.1/PS 1.0-1.4) — **deliberately not used**, scope decision above | `CreateVertexShader`/`SetVertexShaderConstant`/pixel shaders |

Confirmed present in this environment's MinGW-w64 headers + DXVK before writing this plan:
`d3d8.h`/`d3d8types.h`/`d3d8caps.h` (all real, x86_64-buildable), `Direct3DCreate8` (exported by
DXVK's own `d3d8.dll.a`, not by any MinGW x86_64 import library), `D3DRS_*` render states
(identical enum values to `D3DRENDERSTATE_*`), `D3DTSS_COLOROP`/`D3DTOP_MODULATE`,
`DrawPrimitiveUP`/`DrawIndexedPrimitiveUP`, `CreateImageSurface`/`CopyRects`,
`D3DPRESENT_INTERVAL_DEFAULT`/`IMMEDIATE` (in `d3d8caps.h`, not `d3d8types.h`).

---

## 2. Existence-gate spike — `DX8-0` (run before any backend code)

| # | Spike | What it proves | Result |
|---|---|---|---|
| `DX8-0a` | `Direct3DCreate8` + `IDirect3D8::CreateDevice` via the DXVK-installed `d3d8.dll` | Whether real device creation works through DXVK | ✅ Works — DXVK log confirms genuine engagement ("D3D9DeviceEx", "operating in D3D8 compatibility mode") |
| `DX8-0b` | `Clear(rect_count=0, rects=nullptr, TARGET\|ZBUFFER\|STENCIL, ...)` | Whether `count=0` clears the WHOLE target (D3D9 convention) or nothing (the `DX5`-`DX7` `Clear2` gotcha) | ✅ Clears everything — exact `(10,10,10)` readback |
| `DX8-0c` | `SetVertexShader(rawFvfValue)` + `DrawIndexedPrimitiveUP` — Gouraud quad, no vertex buffer | Whether the FVF-via-SetVertexShader idiom actually works | ✅ Works — real Gouraud interpolation confirmed |
| `DX8-0d` | `DrawPrimitiveUP`, far(blue)-then-near(red) overlapping triangles, `D3DRS_ZENABLE=TRUE` | Whether real Z-test occlusion works | ✅ Works — exact red readback (near wins) |
| `DX8-0e` | `CreateTexture` + `SetTexture` + `SetTextureStageState(D3DTSS_COLOROP, D3DTOP_MODULATE)` | Whether the modern stage-state texture mechanism works from the start (pre-empting `DX7-0`'s own legacy-render-state rejection) | ✅ Works — exact solid-red readback, first try |
| `DX8-0f` | Stencil write (left-half quad, `STENCILPASS=REPLACE`) then test (`STENCILFUNC=EQUAL`, full-screen quad) | Whether real stencil write+test works | ✅ Works — exact green/black after write, red/black after test |
| `DX8-0g` | `Present()` with a real window | Whether the real DXVK-backed swap chain presents without error | ✅ Works — real Vulkan swapchain confirmed in the log |

**One real bug found and fixed**: `D3DPRESENT_PARAMETERS.FullScreen_PresentationInterval` must be
`D3DPRESENT_INTERVAL_DEFAULT` for windowed mode — `D3DPRESENT_INTERVAL_IMMEDIATE` there caused
`CreateDevice` to fail with `D3DERR_INVALIDCALL` (real DXVK/D3D8-compat validation). After that one
fix, every remaining test passed on the very next run — no further spike-authoring bugs. See
`dx8-spike/README.md` for the full record.

**Net effect**: fixed-function DX8, delivered via DXVK, is real and fully confirmed. Phase T1 is
unblocked.

---

## 3. Design decisions (recorded before implementation)

1. **Platform gate, same as `D3D9`/`D3D11`/`D3D12`/`DX1`..`DX7`.** Same Windows-native-or-MinGW-
   cross-compile `FATAL_ERROR` gate (`d3d8.h` is equally Windows-only).

2. **Delivery: DXVK, not Wine's own `d3d8.dll`.** Link against DXVK's own
   `/usr/lib/dxvk/wine64/d3d8.dll.a` directly (spike-confirmed, `DX8-0a`) since mingw-w64's x86_64
   target ships no real d3d8 import library. Runtime: `~/.wine-cna-d3d11` (shared with `D3D9`/
   `D3D11`/`D3D12`) with `d3d8` added as a native DLL override by hand (the packaged `dxvk-setup`
   script predates D8VK and never installs it). `scripts/run-wine-dx8.sh` gates on a real
   DXVK-engagement log line, mirroring `run-wine-dxvk9.sh`'s own gate — never silently fall back to
   testing WineD3D's own (different) D3D8 implementation.

3. **Device bring-up: modeled on `D3D9GraphicsBackend`'s own shape, not `DX1`..`DX7`'s
   DirectDraw-based one.** A single `Direct3DCreate8` + `IDirect3D8::CreateDevice(D3DADAPTER_
   DEFAULT, D3DDEVTYPE_HAL, hwnd, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &device)` call, with a
   real Win32 `HWND` (same `SDL_PROP_WINDOW_WIN32_HWND_POINTER` technique every Windows-only
   backend in this project already uses). `D3DCREATE_SOFTWARE_VERTEXPROCESSING` (not hardware) is
   deliberate: this backend does no GPU T&L at all (CPU pre-transform, decision 8), so requesting
   hardware vertex processing would buy nothing and risks failing on a device that only advertises
   software vertex processing.

4. **`D3DPRESENT_PARAMETERS` construction rules, spike-confirmed** (`DX8-0`): `Windowed=TRUE`,
   `SwapEffect=D3DSWAPEFFECT_DISCARD`, `EnableAutoDepthStencil=TRUE` with
   `AutoDepthStencilFormat=D3DFMT_D24S8` (a real combined depth+stencil format, matching `DX6`/
   `DX7`'s own stencil capability), and — the one real bug found —
   `FullScreen_PresentationInterval=D3DPRESENT_INTERVAL_DEFAULT` (never `IMMEDIATE`/`ONE`/etc. in
   windowed mode; that field is fullscreen-only despite compiling fine either way).

5. **`Clear` uses the D3D9-style `count=0`-clears-everything convention** (spike-confirmed,
   `DX8-0b`) — no repeat of `DX5`-`DX7`'s own `IDirect3DViewport::Clear2` "must always pass an
   explicit full-surface `D3DRECT`" gotcha; this device revision's `Clear` doesn't need it.

6. **Vertex submission: `SetVertexShader(rawFvfValue)` once per FVF change, then
   `DrawPrimitiveUP`/`DrawIndexedPrimitiveUP` per draw** — no vertex buffer object involved for the
   CPU-submission draw path (matching this family's own `VertexBuffer`/`IndexBuffer` design,
   decision 10). Spike-confirmed (`DX8-0c`) real. A hand-defined `Dx8TLVertex` struct (`sx,sy,sz,
   rhw` : 4 floats; `color`,`specular` : 2 `D3DCOLOR`s; `tu,tv` : 2 floats — 32 bytes, identical
   byte layout to the now-gone `D3DTLVERTEX` macro) replaces the removed struct.

7. **Texture binding and blending: `SetTexture(stage, texture8)` + `SetTextureStageState(stage,
   D3DTSS_COLOROP, D3DTOP_MODULATE)`** — the same modern mechanism `DX7`'s own design decision 6
   uses, applied here from the start (spike-confirmed, `DX8-0e`, no repeat of `DX7-0`'s own
   legacy-render-state-rejection surprise). Textures are real `IDirect3DTexture8` objects (via
   `CreateTexture`), not DirectDraw surfaces at all — this backend's `Dx8TextureBackend`/
   `Dx8RenderTargetBackend` own real Direct3D resources directly.

8. **CPU transform + near-plane clip + fixed-function submission for 3D geometry: same
   architecture as `DX1`..`DX7`**, re-expressed against `Dx8TLVertex`/`DrawIndexedPrimitiveUP`
   instead of `D3DTLVERTEX`/`DrawIndexedPrimitive`. World*View*Projection computed on the CPU,
   Sutherland-Hodgman near-plane clip (single plane), perspective divide, pack into screen-space
   `Dx8TLVertex`. Same Phase O9 CPU-side `BasicEffect` lighting math (ambient + directional
   Lambertian/Blinn-Phong specular). No GPU T&L at all (decision 3) — matching every backend in
   this family's own design, not a missed opportunity: Shader Model 1.x vertex shaders exist for
   real GPU T&L, but scope decision (TL;DR) already ruled that out.

9. **Stencil: real, same shape as `DX6`/`DX7`, unchanged.** `D3DRS_STENCILENABLE`/`FUNC`/`FAIL`/
   `ZFAIL`/`PASS`/`REF`/`MASK`/`WRITEMASK` against the `D3DFMT_D24S8` auto-depth-stencil surface
   (decision 4). `twoSidedStencilMode`/`ccwStencil*` remain accepted-and-ignored — still a D3D9-era
   addition, still doesn't exist at this DirectX era.

10. **`VertexBuffer`/`IndexBuffer`: plain CPU-side storage** (`Dx8VertexBufferBackend`/
    `Dx8IndexBufferBackend`), matching this family's own Phase O5 pattern (and the `Software`
    backend's identical approach) — the CPU transform pipeline reads directly from these buffers
    each draw, so there is no GPU-side object to upload to for the fixed-function draw path.

11. **2D `SpriteBatch`: REDESIGNED as real GPU-rendered textured quads, not a DirectDraw-Blt-based
    CPU compositor.** `DX1`..`DX7` all used `IDirectDrawSurface*::Blt`/`BltFast` specifically
    because DirectDraw offers no other option and has real historical rotation limits (documented
    per-backend). D3D8 has NO DirectDraw at all — every CNA backend that already targets a real GPU
    device without DirectDraw (`D3D9`/`EasyGL`/`Vulkan`/etc.) implements `SpriteBatch` as real
    GPU-rendered textured quads with real alpha blending, and `DX8` follows that same established
    pattern instead, through the fixed-function pipeline (real `D3DRS_ALPHABLENDENABLE`/
    `SRCBLEND`/`DESTBLEND` state, real `D3DTSS_COLOROP` texture modulation) rather than pixel
    shaders. Rotation, scaling, and all 4 `BlendState` presets are real GPU features here, not a
    CPU-approximated formula per mode — a genuine capability improvement over `DX1`..`DX7`'s own 2D
    layer, not merely a reimplementation.

12. **Readback: `CreateImageSurface` (lockable system-memory surface) + `CopyRects`** from the real
    back buffer — `GetRenderTargetData` is a D3D9-only addition, not available here. Spike-confirmed
    (`DX8-0` throughout — every readback in the spike used exactly this mechanism).

13. **32-bit surfaces only, `DirectSound`/`DirectInput`/`DirectPlay` out of scope, header
    containment (`d3d8.h` confined to `Dx8GraphicsBackend.cpp`, matching `D3D9`'s own convention),
    CMake integration shape** — identical philosophy to earlier plans in this family.

14. **CMake integration**: add `"DX8"` to `CNA_GRAPHICS_BACKEND`'s `STRINGS` property + a
    `CNA_BACKEND_DX8` option; a `cna_backend_graphics_dx8` static library target under
    `src/CNA/Internal/Backends/Dx8/`, same Windows-only `FATAL_ERROR` gate. Link set: DXVK's own
    d3d8 import library (via an explicit path/imported target, NOT the bare `d3d8` name any MinGW
    system library search would resolve to nothing) + `SDL3::SDL3`. No `dxguid` (D3D8 has no COM
    GUIDs requiring it the way `ddraw`/legacy `IDirect3D*` interfaces did — `Direct3DCreate8` is a
    plain exported function, not a `CoCreateInstance`-style GUID lookup).

15. **Testing: `scripts/run-wine-dx8.sh`**, modeled on `scripts/run-wine-dxvk9.sh` — same
    `~/.wine-cna-d3d11` prefix, same DXVK-engagement log-line gate (a `"DXVK: <version>"` line, not
    the `trace:ddraw:` gate `DX1`..`DX7`'s own scripts use, since this backend has no DirectDraw at
    all).

16. **No legacy-interface code, no execute-buffer code, no DirectDraw code, proven by discipline.**
    A new execute-buffer/legacy-interface discipline check
    (`scripts/check-dx8-legacy-interface-discipline.sh`) forbids any `IDirectDraw*` symbol at all (a
    real, automated proof this backend never reaches for the family's own DirectDraw-based
    lineage), any `D3DTLVERTEX`/`D3DVT_*` symbol (doesn't exist in the real headers, but guards
    against a future accidental copy-paste from an earlier backend), and the legacy
    `D3DRENDERSTATE_*` naming (this backend uses only `D3DRS_*`, matching decision 14's header
    containment goal of never mixing eras). A new `Dx8_Stencil` CTest proves real stencil
    write-then-test through the full XNA public API, mirroring `DX6`/`DX7`'s own shape.

---

## 4. Active execution order

1. **`DX8-0`** (existence-gate spike, §2) — done, unblocks everything else.
2. **Phase T1** (CMake integration + skeleton, decisions 2/14).
3. **Phase T2** (device/window bring-up: `Direct3DCreate8`+`CreateDevice`, decisions 3/4).
4. **Phase T3** (2D layer: real GPU-quad `SpriteBatch`, textures/render targets as real Direct3D8
   resources, decisions 7/11/12).
5. **Phase T4** (3D device state: Z-buffer/stencil already attached via `D3DPRESENT_PARAMETERS`,
   `Clear`/`ClearDepth`/etc., decisions 4/5/9).
6. **Phase T5** (CPU transform/clip pipeline + Phase-O9 lighting + fixed-function submission,
   decisions 6/8).
7. **Phase T6** (`VertexBuffer`/`IndexBuffer` backends, decision 10).
8. **Phase T7** (state mapping including real stencil, decision 9; remaining `IGraphicsBackend`
   defaults).
9. **Phase T8** (tests + `docs/dx8-backend.md`, including the new `Dx8_Stencil` CTest).

For every task: build the affected target (`-DCNA_GRAPHICS_BACKEND=DX8`, MinGW cross-compile), run
the relevant CTest through `scripts/run-wine-dx8.sh`, and do not mark a task ✅ without both
actually passing.

---

## Phase T1 — CMake integration and skeleton

| # | Task | Status | Notes |
|---|---|---|---|
| `DX8-1` | Add `"DX8"` to `CNA_GRAPHICS_BACKEND`'s `STRINGS` property + `CNA_BACKEND_DX8` option; extend the Windows-only `FATAL_ERROR` gate; add the legacy-interface-discipline grep CTest (design decision 16) | ✅ | |
| `DX8-2` | `cna_backend_graphics_dx8` static library target linking DXVK's own d3d8 import library directly (decision 14) | ✅ | |
| `DX8-3` | `include/CNA/Internal/Backends/Dx8/Dx8GraphicsBackend.hpp` + `src/CNA/Internal/Backends/Dx8/Dx8GraphicsBackend.cpp`: new backend, not a mechanical port | ✅ | |
| `DX8-4` | Factory dispatch for `DX8` in `CreateGraphicsBackend()` | ✅ | |
| `DX8-5` | `scripts/run-wine-dx8.sh` (design decision 15); one-time Wine-prefix `d3d8` override setup (decision 2) | ✅ | |
| `DX8-6` | Confirm `CnaTests`/the new MinGW test binaries link cleanly against the new backend target under cross-compilation; proactively add a `Dx8` entry to every `CNA_BACKEND_*` registry file | ✅ | |

## Phase T2 — Device/window bring-up (modeled on `D3D9GraphicsBackend`, decisions 3/4)

| # | Task | Status | Notes |
|---|---|---|---|
| `DX8-10` | `Direct3DCreate8` + `IDirect3D8::CreateDevice` against a real `HWND`, `D3DPRESENT_PARAMETERS` construction rules (decision 4) | ✅ | |
| `DX8-11` | `Clear`/`Present`/`GetViewportSize`/`ReadBackbuffer` (via `CreateImageSurface`+`CopyRects`, decision 12) | ✅ | |
| `DX8-12` | `SetVirtualResolution`/`SetPresentationMode`/window-logical-coordinate transforms | ✅ | |

## Phase T3 — 2D layer (real Direct3D8 resources, GPU-quad `SpriteBatch`, decisions 7/11/12)

| # | Task | Status | Notes |
|---|---|---|---|
| `DX8-20` | `Dx8TextureBackend`/`Dx8RenderTargetBackend`: real `IDirect3DTexture8`/`IDirect3DSurface8` objects (`CreateTexture`/`CreateRenderTarget`), `SetRenderTarget2D`/`SetRenderTargets` | ✅ | |
| `DX8-21` | `Dx8SpriteBatchBackend`: real GPU-rendered textured quads through the fixed-function pipeline, real `D3DRS_ALPHABLENDENABLE`/`SRCBLEND`/`DESTBLEND` per `BlendState`, real rotation/scale (decision 11) | ✅ | |
| `DX8-22` | `SpriteFont`/`DrawString` CTest (reuses the same GPU-quad path) | ✅ | |

## Phase T4 — 3D device state (Z-buffer/stencil, `Clear*` family, decisions 4/5/9)

| # | Task | Status | Notes |
|---|---|---|---|
| `DX8-30` | `ClearColorAndDepth`/`ClearDepth`/`ClearStencil`/`ClearDepthAndStencil`/`ClearColorAndStencil`/`ClearColorDepthAndStencil` all via the real device-direct `Clear` (decision 5) | ✅ | |
| `DX8-31` | `SupportsDepthStencil()`/`SupportsCapability()` (`ThreeD`/`DepthStencilBuffer`/`WireFrame` real; `MultiSampleAntiAliasing`/`MultipleRenderTargets`/`OcclusionQuery`/`CustomEffects` genuinely unavailable) | ✅ | |

## Phase T5 — CPU transform/clip pipeline + fixed-function submission (decisions 6/8)

| # | Task | Status | Notes |
|---|---|---|---|
| `DX8-40` | CPU clip/transform math (Sutherland-Hodgman near-plane clip, perspective divide), `Dx8TLVertex` packing, `SetVertexShader(fvf)`+`DrawIndexedPrimitiveUP` submission | ✅ | Same math as `DX7`'s own `Dx7ClipVertex`/`Dx7ClipTriangleNearPlane`/`Dx7BuildPositionColorClipVertex`, re-expressed against the new struct/call shape. |
| `DX8-41` | Phase O9-equivalent CPU-side `BasicEffect` lighting (ambient + directional Lambertian/Blinn-Phong specular) | ✅ | |
| `DX8-42` | Real texture0 sampling via `SetTexture`+`SetTextureStageState` (decision 7) | ✅ | |
| `DX8-43` | Near-plane clipping CTest, `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives`/`DrawPrimitivesEx`/`DrawIndexedPrimitivesEx` | ✅ | |

## Phase T6 — `VertexBuffer`/`IndexBuffer` backends (decision 10)

| # | Task | Status | Notes |
|---|---|---|---|
| `DX8-50`, `DX8-51` | `Dx8VertexBufferBackend`/`Dx8IndexBufferBackend`: plain CPU-side storage | ✅ | |

## Phase T7 — State mapping + remaining `IGraphicsBackend` defaults

| # | Task | Status | Notes |
|---|---|---|---|
| `DX8-60`..`DX8-63` | `ApplyRasterizerState`/`ApplyBlendState`/`ApplySamplerState`, `SetDepthTestEnabled`/`SetBlendEnabled`/`SetDepthWriteEnabled` | ✅ | |
| `DX8-64` | `ApplyDepthStencilState`'s real stencil wiring (decision 9), new `Dx8StencilOperationToD3D` helper | ✅ | |
| `DX8-70`..`DX8-76` | Occlusion query/volume-cube-textures/custom-effects/instancing/remaining clears/`DebugSimulateContextLoss` | ✅ | Genuinely unavailable at this DirectX era or out of this plan's scope, matching this family's own established boundary. |

## Phase T8 — Tests and documentation

| # | Task | Status | Notes |
|---|---|---|---|
| `DX8-80` | Full 2D+3D CTest suite passing (new test set — not a renamed port, since the 2D layer is redesigned) | ✅ | |
| `DX8-81` | New `Dx8_Stencil` CTest: real stencil write-then-test through the full XNA public API (`GraphicsDevice.DepthStencilState`) | ✅ | |
| `DX8-82` | `docs/dx8-backend.md` | ✅ | |
| `DX8-83` | Update `cmake/BackendSelection.cmake`'s `CNA_GRAPHICS_BACKEND` `STRINGS` docstring, `README.md`, and `plan_dxold.md`'s DX8 row | ✅ | |
| `DX8-84` | Full `DX8`-labeled CTest suite regression + targeted cross-backend test re-run | ✅ | |

---

## Boundaries — explicitly out of scope for `DX8`

- **Shader Model 1.x programmable shaders** — explicit scope decision (TL;DR), since real XNA
  effects need `ps_2_0`+ regardless; `CreateEffectBackend` still throws for any real XNA content.
- **Multitexture, DXTn compression** — same reasoning `DX6`/`DX7` already recorded (`D3DFVF_TLVERTEX`-
  equivalent layout only carries one 2D texture-coordinate pair; no consumer for compressed texture
  data in CNA's own content pipeline).
- **Cube environment maps** — same `D3DFVF_TEXCOORDSIZE3`/`DDSCAPS2_CUBEMAP`-shaped reasoning `DX7`
  already recorded (no `DDSCAPS2_CUBEMAP` concept here at all — D3D8 has its own
  `CreateCubeTexture`, but wiring it in is out of this plan's fixed-function-only scope).
- **Two-sided stencil** — still a D3D9-era addition.
- **Occlusion query** — a real DX9-only addition, absent through every version up to and including
  DX8 (`docs/directx-legacy-backends-analysis.md`'s own finding).
- **Volume/cube textures, custom effects, instancing, MRT** — genuinely unavailable at this DirectX
  era or out of this plan's scope, matching every prior backend in this family's own boundary.

---

## See also

- `plan_dxold.md` — the roadmap this plan is row 8 of.
- `plan_dx7.md`, `docs/dx7-backend.md` — the backend whose CPU-transform/clip math and stencil
  design this plan reuses (conceptually, not mechanically — the vertex-submission call shape and
  2D-layer architecture are both new).
- `plan_dx9.md`, `docs/d3d9-backend.md` — the sibling this plan's DEVICE BRING-UP shape (DXVK
  delivery, real HWND, `D3DPRESENT_PARAMETERS`, readback pattern) is modeled on.
- `dx8-spike/README.md` — the full `DX8-0` spike record.
- `docs/directx-legacy-backends-analysis.md` — the feasibility analysis; DX8's own §3.1/§4 rows
  cover the SM1.x-vs-fixed-function tradeoff this plan's scope decision is based on.
