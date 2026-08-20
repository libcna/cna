# DirectX 1 (DirectDraw v1) Graphics Backend — Implementation Plan

> **Status (2026-07-20): Phases O1–O8 are all closed.** Every task in this plan except the
> permanent, explicitly-out-of-scope items (§ Boundaries) is implemented and verified: real
> `IDirectDraw`/`IDirectDrawSurface` v1 device bring-up, textures/render targets, the CPU
> `SpriteBatch` compositor (ported from `DX3`), all 4 blend modes, `SpriteFont`, and `ThrowNo3D`
> wiring are all real and pixel-verified — **10/10 `DX1` CTests pass** (`Dx1_V1OnlyDiscipline`,
> `Dx1_Smoke`, `Dx1_TextureRenderTarget`, `Dx1_SpriteBatch`, `Dx1_Blend`, `Dx1_AddressMode`,
> `Dx1_SpriteFont`, `Dx1_No3D`, `Dx1_GraphicsCapability`, `Dx1_LogicalTransform`), built via a real
> MinGW-w64 cross-compile and run through a real Wine `ddraw.dll` (`scripts/run-wine-dx1.sh`,
> ddraw-engagement-gated). Two real bugs were found and fixed during this session's own first test
> run (both in the ported test file, not the compositor — see Phase O4's table): a rotation-check
> sample point 2px inside the default bilinear sampler's blend zone, and a zero-alpha check that
> used the premultiplied `AlphaBlend` preset with non-premultiplied source data. A pre-existing,
> not-DX1-specific `cmake/Harnesses.cmake` gap (two audio harnesses missing an `SDL3::SDL3` link,
> latent under every Windows-only backend, never previously exercised by a real MinGW build) was
> also found and fixed along the way, plus a deeper `CNA_FFMPEG_AVAILABLE`/`CNA_ENABLE_NET` test-glob
> gating gap (see `DX1-88`'s own row for the full list — this exact from-scratch MinGW + full
> `CnaTests` configuration had simply never been exercised before). Full regression:
> **5336 passed, 11 skipped, 48 failed** — every one of the 48 confirmed pre-existing and unrelated
> to DX1 (mostly the same structural "3D content load under a 2D-only backend" gap `plan_freedirect.md`
> already documented). See `docs/dx1-backend.md` for the full completeness table.
>
> **Post-ship: two more real bugs found by actually running `examples/demo_2d` live (not just the
> CTest suite) and fixed the same day, both reported directly by the project owner:**
> 1. **Pixel channel order swap (a yellow texture rendered blue/cyan).** `CreateOffscreenSurface`
>    never specified an explicit `DDPIXELFORMAT`, so real Wine `ddraw.dll` defaulted every offscreen
>    surface's byte layout to the current display mode's own native format — confirmed to be
>    `(B,G,R,X)` byte order in this environment, not the `(R,G,B,A)` order every pixel-manipulation
>    helper in this file assumed. A first fix attempt (explicitly requesting a `DDPF_RGB` format with
>    `(R,G,B,A)`-matching masks) was itself wrong — real Wine `ddraw.dll` rejected it with
>    `DDERR_INVALIDPIXELFORMAT` (confirmed empirically). The real fix: never assume a fixed byte
>    order at all — query the actual negotiated `DDPIXELFORMAT` from the first surface created
>    (`DetectChannelLayout`) and remap every raw pixel read/write through those real offsets. Correct
>    regardless of which native format a given Wine version/environment negotiates, and no format
>    request is ever rejected since none is made.
> 2. **Visible stutter with the demo's 50–100 independently-rotating sprites.** Every single
>    `SpriteBatch.Draw()` call was doing its own `Lock()`/`Unlock()` round-trip on both the shared
>    destination surface and the (usually shared) source texture — each a real COM call Wine has to
>    translate. Fixed via `LockedSurfaceCache`: the lock is acquired once and reused across a whole
>    run of consecutive general-path draws, released only when a real `Blt`/`BltFast` call needs the
>    surface unlocked or at `End()`. Also found along the way: this session's own `cmake-build-dx1`
>    had `CMAKE_BUILD_TYPE` empty (zero compiler optimization, effectively `-O0`) for every prior
>    build and test run — reconfigured with `-DCMAKE_BUILD_TYPE=Release` (`-O3`). This does not
>    invalidate any correctness result (CTest pixel assertions are optimization-independent), but
>    every performance impression prior to this point understated real performance; **always build
>    this backend (and its demo) in Release for any perf judgment.**
>
> All 10 `DX1` CTests re-verified passing after both fixes.
>
> Owner's own words (translated from Czech): *"Add a new graphics backend, DirectX 1, to CNA. It'll
> be 2D only; for 3D it throws an exception. Create `plan_dx1.md` and then implement it — ask if
> anything's unclear. Do not use `free-direct` — it must use real DirectX 1, or Wine, or Proton."*
> This plan, and the roadmap it belongs to (`plan_dxold.md`), are the direct product of that
> instruction.
>
> **Status legend** (matches this repo's convention, e.g. `plan_freedirect.md`): ✅ implemented *and*
> verified against its stated acceptance criteria; 🟨 code/doc exists but hasn't met that bar yet;
> ⬜ not implemented.

---

## 0. TL;DR

- New backend: `CNA_GRAPHICS_BACKEND=DX1`.
- **Route B, not Route A** — real MinGW-w64 `ddraw.h`, genuine COM `IDirectDraw`/
  `IDirectDrawSurface` **v1 interfaces only** (never `IDirectDraw2+`/`IDirectDrawSurface2+`/
  `DDSURFACEDESC2`), cross-compiled the same way `D3D9`/`D3D11`/`D3D12` already are, run under Wine
  (or Proton) on this Linux dev machine. **No `free-direct` anywhere in this backend.**
- **Zero Direct3D, by construction, not by policy.** DirectX 1 (1995) shipped DirectDraw +
  DirectSound + DirectInput + DirectPlay — Direct3D did not exist yet (added in DX2, 1996). So
  unlike `DX3` (which throws on 3D by *choice*, even though `free-direct`'s underlying DirectDraw
  generation technically has an execute-buffer Direct3D sibling it doesn't use), `DX1` throws
  because **there genuinely is no Direct3D COM interface reachable from a real DirectX-1-era
  `ddraw.h`/`d3d.h` pairing to even call**. `IGraphicsBackend`'s full 3D method set uses
  `ThrowNo3D`.
- **Architecturally, this is the same backend as `DX3`, wearing a different surface layer.** The
  `IGraphicsBackend` 2D-subset contract `DX3` already fully implements (Clear/Present, textures,
  render targets, `SpriteBatch` with rotation/scale/tint/flip, 4 blend modes, `Wrap`/`Mirror`
  sampling, `SpriteFont`) is identical here — `IDirectDrawSurface::Blt`/`BltFast` has **never**
  supported rotation, in any DirectX version, so the same CPU quad-compositor architecture and the
  same already-proven pivot/blend-formula math `DX3` built get **ported, not re-derived**. The only
  real delta is *how the surface layer is obtained*: real Win32 COM + Wine, instead of
  `free-direct`'s SDL3 reimplementation.
- **Existence-gate spike first** (`DX1-0`), same discipline `plan_dx9.md`'s `D9-0` and `plan_freedirect.md`
  design decision 2 both used: prove the MinGW header + a real Wine `ddraw.dll` run actually work in
  this environment *before* writing backend code, not after.

---

## 1. What "real DirectX 1" concretely means for this backend

DirectX version numbers name SDK *releases*, not distinct COM interfaces — the interfaces
themselves are versioned independently (`IDirectDraw` → `IDirectDraw2` → `IDirectDraw4` →
`IDirectDraw7`; `IDirectDrawSurface` → `...Surface2` → `...Surface3` → `...Surface4` →
`...Surface7`). The literal, checkable technical definition this plan uses for "this is DirectX 1,
not DirectX 3 wearing a MinGW badge" is:

| Symbol | DX1 backend uses | Introduced in | Never used here |
|---|---|---|---|
| DirectDraw object interface | `IDirectDraw` (`IID_IDirectDraw`) | DX1 (1995) | `IDirectDraw2`/`4`/`7` |
| Surface interface | `IDirectDrawSurface` | DX1 | `IDirectDrawSurface2`/`3`/`4`/`7` |
| Surface descriptor struct | `DDSURFACEDESC` | DX1 | `DDSURFACEDESC2` (DX3+) |
| `SetDisplayMode` arity | 3-arg: `(dwWidth, dwHeight, dwBPP)` | DX1 | the 5-arg `IDirectDraw2::SetDisplayMode` (adds refresh rate + flags, DX3+) |
| Any Direct3D symbol (`IDirect3D`, `d3d.h`) | **never referenced** | Direct3D itself is DX2+ | n/a — genuinely nothing to call |

Confirmed present in this environment's MinGW-w64 headers before writing this plan (not assumed):
`IID_IDirectDraw`, the `IDirectDraw` v1 vtable (`Compact`/`CreateSurface`/`SetCooperativeLevel`/
`SetDisplayMode(DWORD,DWORD,DWORD)`/…), the `IDirectDrawSurface` v1 vtable (`Blt`/`BltFast`/`Flip`/
`Lock`/`Unlock`/`GetSurfaceDesc`/…), and `DDSURFACEDESC`'s v1 layout (`lpSurface`/`lPitch` present,
no `DDSURFACEDESC2`-only fields) all live in `/usr/x86_64-w64-mingw32/include/ddraw.h`, and
`libddraw.a`/`libdxguid.a` both exist in the same sysroot. `DirectDrawCreate()` (not
`DirectDrawCreateEx()`, which is DX7+) is the v1 factory entry point.

A code-review discipline (not a runtime check — there is nothing to assert at runtime once it
compiles) enforces this: `Dx1GraphicsBackend.cpp` may only name `IDirectDraw`/`LPDIRECTDRAW`/
`IDirectDrawSurface`/`LPDIRECTDRAWSURFACE`/`DDSURFACEDESC`/`LPDDSURFACEDESC` — never a `2`/`3`/`4`/
`7`-suffixed interface or struct, never anything from `d3d.h`. `DX1-1` adds a one-line grep-based
CTest (mirrors `GraphicsBackendCompileDefinitionsTest`'s own style) asserting no `2`/`4`/`7`-suffixed
DirectDraw symbol or `d3d.h`/`IDirect3D` token appears anywhere under
`src/CNA/Internal/Backends/Dx1/` — a real, automated proof this backend never silently drifts into
DX3+ territory, not just a comment promising it.

---

## 2. Existence-gate spike — `DX1-0` (run before any backend code)

Mirrors `plan_dx9.md`'s `D9-0` and `plan_freedirect.md` design decision 2's own "prove it empirically,
don't assume" bar.

| # | Spike | What it proves |
|---|---|---|
| `DX1-0a` | Throwaway MinGW-w64 `.cpp` including only `<ddraw.h>`, calling `DirectDrawCreate`, naming only v1 symbols (§1's table); compiles + links against `libddraw.a`/`libdxguid.a` with `x86_64-w64-mingw32-g++` | The header/import-lib pair genuinely exists and the v1-only symbol set is real, not a guess |
| `DX1-0b` | Extend `DX1-0a` into a real, running program: create an SDL3 (mingw-built) window → real `HWND` via `SDL_PROP_WINDOW_WIN32_HWND_POINTER` → `DirectDrawCreate` → `SetCooperativeLevel(hwnd, DDSCL_NORMAL)` → `CreateSurface` (`DDSCAPS_PRIMARYSURFACE`) → `Lock()` the primary → write one solid color → `Unlock()` → observe, run for real under Wine (a fresh, vanilla prefix — **no DXVK, no `free-direct`, nothing else in the process**) | Whether `Lock()` on the *primary* surface in windowed (`DDSCL_NORMAL`) mode is genuinely writable under Wine's `ddraw.dll` — the exact question `DX3` got burned by with `free-direct` (see `plan_freedirect.md`'s "Real, confirmed finding" note) and must not assume away here just because the library is different |
| `DX1-0c` | Confirm, by reading `ddraw.h` plus Microsoft's own historical DirectDraw programming-model documentation (not assumed): a windowed (`DDSCL_NORMAL`) `IDirectDraw` app legitimately never calls `SetDisplayMode` at all (that call is exclusive-fullscreen-only in the real historical API) | Settles design decision 4 below — confirms the windowed present path needs no display-mode change, only `CreateSurface`+`Blt` |

**`DX1-0` result (run 2026-07-20, all three spikes passed):**

- `DX1-0a`: a throwaway MinGW-w64 `.cpp` naming only `IDirectDraw`/`IDirectDrawSurface`/
  `DDSURFACEDESC`/`DirectDrawCreate` compiled and linked cleanly against `-lddraw -ldxguid`
  (`x86_64-w64-mingw32-g++`, zero warnings/errors) — confirms §1's symbol table is real, not a
  guess.
- `DX1-0b`: run for real under a fresh, vanilla `~/.wine-cna-dx1` prefix (`wineboot --init`, no
  DXVK, no `free-direct` anywhere in the process), against Xvfb `:99`. **Every step succeeded**,
  including `Lock()` on the primary surface — a genuinely *better* result than `DX3` got from
  `free-direct` (`plan_freedirect.md`'s own "Lock() on primary never returns a writable pointer" finding
  does **not** reproduce here; real Wine `ddraw.dll` genuinely honors it). One real, load-bearing
  wrinkle found along the way, **not anticipated by design decision 4's original wording**: with no
  `SetDisplayMode` call (windowed `DDSCL_NORMAL`, per `DX1-0c`), the primary surface Wine hands
  back represents the **whole screen** (`lPitch=3200` for Xvfb's real `800x480` mode — `800*4`
  bytes/row — not the 64×64 window), exactly matching real historical DirectDraw semantics (the
  primary surface is always the *display*, not "your window"; windowed apps position their `Blt`
  destination rect at their own client area's *screen* coordinates). Consequence for `DX1-15`
  (`Present()`): the shadow→primary `Blt()` destination rect must be computed via
  `ClientToScreen(hwnd, &topLeft)` on the window's real client-area origin, not a `{0,0,w,h}`
  rect assumed relative to the window — this plan's Phase O2 table is updated accordingly. The
  shadow-backbuffer design (decision 4) stays exactly as planned regardless — it is still the
  correct destination for `Clear()`/the compositor even though `Lock()`-on-primary itself works
  here, since the primary is desktop-sized, not window-sized, and every offscreen `Lock()` in the
  spike returned a real, writable, correctly-sized (64×64, `dwRGBBitCount=32`) buffer with no
  caveats.
- `DX1-0c`: confirmed by inspection of `ddraw.h` (no forced-mode-set requirement anywhere in the
  `IDirectDraw` v1 vtable for `DDSCL_NORMAL` cooperative level) and empirically — the spike never
  calls `SetDisplayMode` and every subsequent call still succeeds. Windowed mode needs no display
  mode change, exactly as expected.

**Net effect**: Route B is fully viable for `DX1` in this environment, verified empirically, not
assumed. Phase O1 is now unblocked.

---

## 3. Design decisions (recorded before implementation)

1. **Platform gate, same as `D3D9`/`D3D11`/`D3D12`.** `ddraw.h`'s real Windows-only content means
   `CNA_GRAPHICS_BACKEND=DX1` needs the exact same `cmake/BackendSelection.cmake` `FATAL_ERROR`
   gate those three backends already use (native Windows build, or MinGW cross-compile via
   `cmake/toolchains/mingw-w64.cmake`) — **unlike `DX3`**, which is genuinely native-Linux-buildable
   because `free-direct` is SDL3-backed. This is the direct, structural consequence of "must use
   real DirectX 1" instead of a reimplementation.

2. **v1-interface-only discipline is binding**, per §1's table and its grep-based CTest
   (`DX1-1`). If a task ever seems to need `IDirectDraw2`+ behavior (e.g. a refresh-rate-aware
   `SetDisplayMode`), that is out of scope for `DX1` by definition — it belongs to a later
   `plan_dxN.md` in the `plan_dxold.md` roadmap, not a quiet upgrade here.

3. **`HWND`: real Win32 handle, same mechanism `D3D9GraphicsBackend.cpp` already uses** —
   `SDL_GetPointerProperty(SDL_GetWindowProperties(window_), SDL_PROP_WINDOW_WIN32_HWND_POINTER,
   nullptr)` on CNA's already-existing `SDL_Window*` (the MinGW/Wine build's SDL3 uses its real
   win32 video backend, so this is a genuine `HWND`, not `free-direct`'s
   `reinterpret_cast<HWND>(sdlWindow)` hack). No custom window or message-loop code — same
   `GraphicsBackendCreateArgs::window` every other backend already receives.

4. **Windowed present via a CPU-owned shadow-backbuffer surface, adopted proactively from `DX3`'s
   own proven design** (its design decision 5 / the "Real, confirmed finding" note in
   `plan_freedirect.md`), rather than re-discovering the same class of problem from scratch: real
   `IDirectDraw::SetCooperativeLevel(hwnd, DDSCL_NORMAL)` (windowed — not exclusive fullscreen, so
   this stays scriptable under Wine/CTest with no display-mode switch, confirmed by `DX1-0c`). An
   offscreen `DDSCAPS_OFFSCREENPLAIN` surface is what `Clear()`/the `SpriteBatch` compositor always
   target; `Present()` is a `Blt()` from that shadow surface onto the real primary. `DX1-0b`'s
   actual finding (§2) confirms whether this indirection is strictly load-bearing here or just a
   safe-by-default match to `DX3`'s own precedent — ship it as the default regardless, since it
   costs nothing when `Lock()`-on-primary happens to work and avoids a repeat of `DX3`'s discovered
   bug when it doesn't.

5. **CPU compositor for `SpriteBatch` — port `DX3`'s already-verified formulas, do not re-derive.**
   `IDirectDrawSurface::Blt`/`BltFast` has never supported rotation in any DirectX version (v1
   through v7 all share this limitation) — the identity fast path (position-only, `scale=1`,
   `tint=White`, `blend=Opaque`, no rotation/flip) uses a real `BltFast`/`Blt`; everything else goes
   through `Lock()` on both source and destination surfaces and a per-pixel edge-function
   rasterizer, ported verbatim from `DX3`'s `CompositeQuad` (pivot/rotation-around-`origin` math,
   `SpriteEffects` flip, per-source-pixel `Wrap`/`Mirror`/`TextureFilter` sampling — `plan_freedirect.md`
   design decisions 5/7, `DX3-32`/`DX3-33`/`DX3-45`/`DX3-46`).

6. **Blend-mode math — port `DX3`'s 4 formulas verbatim** (`plan_freedirect.md` design decision 6,
   `DX3-40`..`DX3-44`): `Opaque` direct overwrite; `AlphaBlend` premultiplied `out = src +
   dst*(1-srcAlpha)`; `NonPremultiplied` straight `out = src*srcAlpha + dst*(1-srcAlpha)`;
   `Additive` `out = src*srcAlpha + dst` (saturating); any other custom `BlendState` factor/op
   combo falls back to `AlphaBlend`, matching both preset factors *and* `BlendFunction::Add`
   (`DX3-44`'s own real bug-fix — do not repeat the "factors only, ignoring the blend equation"
   mistake here).

7. **32-bit surfaces only** — XNA's `Texture2D` has no palette-texture concept (`plan_freedirect.md`
   design decision 4). `DX1`'s `CreateSurface` calls always request 32bpp RGB (`DDPF_RGB`);
   `SetPalette`/`CreatePalette`/8-bit `Lock`/`GetDC` paths are never called.

8. **`DirectSound`/`DirectInput`/`DirectPlay` are entirely out of scope**, same as `DX3` design
   decision 3 — CNA's existing audio/input stack is untouched; this backend never references
   `IDirectSound*`/`IDirectInput*`/`IDirectPlay*` even though they're part of the real DirectX 1
   SDK.

9. **Header containment.** `<ddraw.h>` (pulls in real `<windows.h>`) is included **only** inside
   `src/CNA/Internal/Backends/Dx1/*.cpp` plus a private pimpl header — never from any public CNA
   header, matching `D3D11`/`D3D12`/`DX3`'s own discipline (`plan_freedirect.md` design decision 9).
   `IGraphicsBackend.hpp` gains no `ddraw`-shaped forward declarations.

10. **CMake integration**: add `"DX1"` to `CNA_GRAPHICS_BACKEND`'s `STRINGS` property and a
    `CNA_BACKEND_DX1` option; a `cna_backend_graphics_dx1` static library target under
    `src/CNA/Internal/Backends/Dx1/`, gated behind the same Windows-only `FATAL_ERROR` block
    `D3D9`/`D3D11`/`D3D12` already share (design decision 1). Link set: `ddraw` + `dxguid` (GUID
    storage) + `SDL3::SDL3` (window/HWND access) — **no `free-direct`, no DXVK, no
    `d3dcompiler`, no `d3d11`/`dxgi`**. Confirm the exact minimal link set empirically at `DX1-1`,
    same "spike the link set, don't guess" bar `plan_dx9.md` design decision 16 set for `D3D9`.

11. **Testing: cross-compiled MinGW `.exe` run through a new `scripts/run-wine-dx1.sh` wrapper**,
    modeled on `scripts/run-wine-dxvk9.sh` but for vanilla `ddraw.dll` instead of DXVK — a dedicated
    Wine prefix (`~/.wine-cna-dx1` by default, override via `CNA_DX1_WINEPREFIX`), **no DXVK
    install needed** (confirmed in `docs/directx-legacy-backends-analysis.md` §4: DXVK does not
    translate DirectDraw at all, so a vanilla prefix's builtin `ddraw.dll` is exactly what real
    DirectDraw-generation Wine support already is). The wrapper's engagement gate asserts genuine
    `ddraw.dll` engagement via `WINEDEBUG=+ddraw` trace output (a `ddraw:` channel line), the DX1
    equivalent of `run-wine-dxvk9.sh`'s `"DXVK: <version>"` grep — proof this ran through Wine's
    real DirectDraw implementation, not a silent no-op.

---

## 4. Active execution order

1. **`DX1-0`** (existence-gate spike, §2) unblocks everything else — do this first, record the
   real finding.
2. **Phase O1** (CMake integration + skeleton) — same shape as `plan_freedirect.md` Phase X1.
3. **Phase O2** (DirectDraw device/window bring-up: `DirectDrawCreate` →
   `SetCooperativeLevel`(design decision 3) → primary `CreateSurface` → shadow-backbuffer
   `CreateSurface`(design decision 4) → `Clear`(`Blt`/`Lock`+`memset`) → `Present`(`Blt`
   shadow→primary)) must land and be pixel-verified before anything else, same "prove the
   foundation" order `plan_freedirect.md` Phase X2 used.
4. **Phase O3** (texture/render-target backends: offscreen surfaces + `Lock`/`Unlock`) is the
   storage layer everything else composites into/out of.
5. **Phase O4** (CPU compositor / `SpriteBatch` draw path) — port `DX3`'s `CompositeQuad`
   (design decision 5); verify continuously against Phase O3, not left to the end.
6. **Phase O5** (blend-mode math, design decision 6) builds directly on O4's per-pixel compositor.
7. **Phase O6** (`SpriteFont`) should fall out of O3+O4 almost for free, same finding `DX3-50`/`51`
   made — confirm, don't assume.
8. **Phase O7** (`ThrowNo3D` wiring) can happen any time after O1 but must be complete before this
   backend is feature-complete.
9. **Phase O8** (tests + `docs/dx1-backend.md`) — add test coverage in the same task that
   implements each capability, this family's standing convention, not bolted on afterward.

For every task: build the affected target (`-DCNA_GRAPHICS_BACKEND=DX1`, MinGW cross-compile), run
the relevant CTest through `scripts/run-wine-dx1.sh`, and do not mark a task ✅ without both
actually passing.

---

## Phase O1 — CMake integration and skeleton

| # | Task | Status | Notes |
|---|---|---|---|
| `DX1-1` | Add `"DX1"` to `CNA_GRAPHICS_BACKEND`'s `STRINGS` property + `CNA_BACKEND_DX1` option; extend the existing `D3D9`/`D3D11`/`D3D12` Windows-only `FATAL_ERROR` gate (design decision 1) to include `DX1`; add the v1-only-symbol grep CTest (§1) | ✅ | |
| `DX1-2` | `cna_backend_graphics_dx1` static library target (`elseif(CNA_GRAPHICS_BACKEND STREQUAL "DX1")` block in `cmake/BackendSelection.cmake`/`cmake/BackendLibraries.cmake`); confirm minimal link set empirically (design decision 10) | ✅ | |
| `DX1-3` | `include/CNA/Internal/Backends/Dx1/Dx1GraphicsBackend.hpp` (pimpl-only, no `<ddraw.h>` visible, matching `DX3-5`'s own reasoning: `<ddraw.h>` pulls in `<windows.h>`, which globally `#define`s things a public CNA header must never leak) + `src/CNA/Internal/Backends/Dx1/Dx1GraphicsBackend.cpp` (real `<ddraw.h>` usage lives here): every `IGraphicsBackend` pure virtual implemented — real where O1 can make it real, honest `ThrowNo3D` stubs elsewhere | ✅ | |
| `DX1-4` | Factory dispatch for `DX1` in `CreateGraphicsBackend()` | ✅ | |
| `DX1-5` | `scripts/run-wine-dx1.sh` (design decision 11) + a fresh `~/.wine-cna-dx1` prefix | ✅ | |
| `DX1-6` | Confirm `CnaTests`/the new MinGW test binaries link cleanly against the new backend target under cross-compilation | ✅ | |

## Phase O2 — DirectDraw device/window bring-up

| # | Task | Status | Notes |
|---|---|---|---|
| `DX1-10` | `DirectDrawCreate(nullptr, &dd, nullptr)` at backend construction; verify the returned `IDirectDraw*` is real and `Release()`d correctly on destruction (COM refcounting, no leak) | ✅ | |
| `DX1-11` | `SetCooperativeLevel(reinterpret_cast<HWND>(...real HWND...), DDSCL_NORMAL)` using the real `HWND` from design decision 3 | ✅ | |
| `DX1-12` | Primary `CreateSurface` (`DDSCAPS_PRIMARYSURFACE`) | ✅ | No `SetDisplayMode` call — `DX1-0c` confirms windowed mode doesn't need one. |
| `DX1-13` | Shadow-backbuffer offscreen `CreateSurface` (`DDSCAPS_OFFSCREENPLAIN`, 32bpp, sized to the requested backbuffer) — design decision 4 | ✅ | |
| `DX1-14` | `Clear(r,g,b,a)`: real `Lock()`/write all 4 channels/`Unlock()` against the shadow surface (not `DDBLT_COLORFILL`, which historically hardcodes alpha on some drivers — verify directly rather than assume, `DX3-14`'s own correction is exactly this class of bug) | ✅ | |
| `DX1-15` | `Present()`: `Blt()` shadow → primary. **`DX1-0b` finding**: the primary surface is desktop-sized, not window-sized — the destination rect must be the window's client area translated to screen coordinates via `ClientToScreen(hwnd, &topLeft)`, not `{0,0,w,h}` | ✅ | |
| `DX1-16` | `GetViewportSize()`/`SetVirtualResolution()`/`SetPresentationMode()`: reuse the shared backend-agnostic logical-resolution/letterbox math every other backend shares | 🟨 | `GetViewportSize()`/`SetVirtualResolution()` are fully correct (and, unlike `DX3-16`, `SetVirtualResolution()` has no stale-scale bug — `Present()` recomputes the letterbox fresh every frame). `SetPresentationMode()` stores the requested mode but `Present()` always applies a letterbox-equivalent uniform scale regardless of it — `Stretch`/`Overscan`/`NativeBackBuffer` are not yet distinguished, same honest scope `DX3-16` recorded. See `docs/dx1-backend.md` §1. |
| `DX1-17` | `GetWindowInternal()` returns the real `SDL_Window*`; `GetRendererInternal()` returns `nullptr` | ✅ | |
| `DX1-18` | `Dx1_Smoke` CTest (through `scripts/run-wine-dx1.sh`): construct backend, clear to a known color, present, read back via `Lock()`, assert exact pixel match | ✅ | |

## Phase O3 — Texture and render-target backends

| # | Task | Status | Notes |
|---|---|---|---|
| `DX1-20` | `Dx1TextureBackend : ITextureBackend` — private offscreen `IDirectDrawSurface*` (`DDSCAPS_OFFSCREENPLAIN`, 32bpp); `UpdatePixels` via `Lock()`/`memcpy`/`Unlock()` | ✅ | |
| `DX1-21` | `SetData`/`GetData` round-trip via `Lock`/`Unlock` | ✅ | |
| `DX1-22` | Mip levels (`level>0`): expect throw, same conclusion `DX3-22` reached (no native mip chain in `IDirectDrawSurface`) | ✅ | |
| `DX1-23` | `Dx1RenderTargetBackend : IRenderTargetBackend` — same offscreen-surface mechanism; `BindAsRenderTarget()`/`UnbindAsRenderTarget()` switch the active target surface | ✅ | |
| `DX1-24` | `HasRealDepthBuffer()` → always `false` | ✅ | |
| `DX1-25` | `RenderTargetUsage::DiscardContents` vs `PreserveContents` — expect this comes free from shared `GraphicsDevice.cpp` logic, same finding `DX3-25` made | ✅ | |
| `DX1-26` | `ReadBackbuffer()`/`GetBackBufferData()`: real `Lock()` + `memcpy` from the active surface | ✅ | |
| `DX1-27` | `SetRenderTargets` with 2+ bindings (MRT): throw | ✅ | |
| `DX1-28` | Surface dimension cap: confirm CNA's texture-size validation against whatever cap real `IDirectDraw::CreateSurface` enforces under Wine (spike-confirm, don't assume `DX3`'s `free-direct`-specific 4096 figure applies here) | ✅ | |

## Phase O4 — CPU compositor / `SpriteBatch` draw path

| # | Task | Status | Notes |
|---|---|---|---|
| `DX1-30` | `Dx1SpriteBatchBackend : ISpriteBatchBackend` skeleton; `Begin()`/`End()` | ✅ | |
| `DX1-31` | Identity fast path: real `BltFast`/`Blt` straight copy | ✅ | |
| `DX1-32` | General path: port `DX3`'s `CompositeQuad` edge-function rasterizer verbatim (design decision 5) | ✅ | |
| `DX1-33` | Rotation around `origin` — port `DX3-33`'s verified formula | ✅ | |
| `DX1-34` | `SpriteEffects::FlipHorizontally`/`FlipVertically` | ✅ | |
| `DX1-35` | Scalar / `Vector2` scale overloads | ✅ | Expect free, same finding `DX3-35` made (resolved in shared `SpriteBatch.cpp`). |
| `DX1-36` | `SetTransformMatrix()` (`Begin(transformMatrix)`) | ✅ | |
| `DX1-37` | `SpriteSortMode` handling — expect fully covered by shared `SpriteBatch.cpp`, same finding `DX3-37` made | ✅ | |
| `DX1-38` | Custom `Effect` via `Begin(effect)`: throws (no shader stage exists) | ✅ | |
| `DX1-39` | Source-rectangle cropping | ✅ | |

## Phase O5 — Blend-mode compositing math

| # | Task | Status | Notes |
|---|---|---|---|
| `DX1-40` | `Opaque` | ✅ | Port `DX3-40`. |
| `DX1-41` | `AlphaBlend` (premultiplied) | ✅ | Port `DX3-41`. |
| `DX1-42` | `NonPremultiplied` (straight alpha) | ✅ | Port `DX3-42`. |
| `DX1-43` | `Additive` | ✅ | Port `DX3-43`. |
| `DX1-44` | Custom `BlendState` fallback → `AlphaBlend`, matching factors **and** `BlendFunction::Add` | ✅ | Port `DX3-44`'s bug-fixed logic directly — do not reintroduce the factors-only bug. |
| `DX1-45` | `TextureFilter` → nearest vs. bilinear in the compositor | ✅ | Port `DX3-45`. |
| `DX1-46` | `TextureAddressMode::Wrap`/`Mirror` in the per-source-pixel sampler | ✅ | Port `DX3-46`. |

## Phase O6 — `SpriteFont`

| # | Task | Status | Notes |
|---|---|---|---|
| `DX1-50` | Single glyph — expect zero new backend code beyond Phase O4, confirm via a `Dx1_SpriteFont` CTest, same finding `DX3-50` made | ✅ | |
| `DX1-51` | Multiple glyphs, spacing/kerning | ✅ | |
| `DX1-52` | `\n` newline advance | ✅ | |
| `DX1-53` | Unknown-character fallback (`defaultCharacter`) | ✅ | |
| `DX1-54` | `SpriteEffects` flip + rotation/origin/scale with `DrawString` | ✅ | |

## Phase O7 — `ThrowNo3D` wiring and remaining defaults

| # | Task | Status | Notes |
|---|---|---|---|
| `DX1-60` | `ClearColorAndDepth`/`ClearDepth`/`ClearStencil`/`ClearDepthAndStencil`/`ClearColorAndStencil`/`ClearColorDepthAndStencil` → `ThrowNo3D` | ✅ | |
| `DX1-61` | `SetDepthTestEnabled`/`SetBlendEnabled`/`SetDepthWriteEnabled` → `ThrowNo3D` | ✅ | |
| `DX1-62` | `CreateVertexBuffer`/`CreateIndexBuffer16`/`CreateIndexBuffer32` → `ThrowNo3D` | ✅ | |
| `DX1-63` | `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives`/`DrawPrimitivesEx`/`DrawIndexedPrimitivesEx`/`DrawInstancedPrimitivesEx` → `ThrowNo3D` (mostly free once `DX1-62` blocks buffer construction, same finding `DX3-63` made) | ✅ | |
| `DX1-64` | `CreateTexture3D`/`CreateTextureCube`/`CreateRenderTargetCube` → `nullptr` (inherited default, no override needed) | ✅ | |
| `DX1-65` | `SupportsDepthStencil()` → `false` | ✅ | |
| `DX1-66` | `CreateOcclusionQuery()` → `nullptr` (inherited default — do not repeat `DX3`'s Phase X1/X2 bug of overriding this to throw) | ✅ | |
| `DX1-67` | `CreateEffectBackend()` → `nullptr` (inherited default, no override needed) | ✅ | |
| `DX1-68` | `TransformWindowToLogical`/`TransformLogicalToWindow`: real implementation, ported from `DX3-68`'s letterbox scale+offset computation | ✅ | |
| `DX1-69` | `DebugSimulateContextLoss`/`DebugRestoreContext`: expect no-op, same finding `DX3-69` made | ✅ | |

## Phase O8 — Tests and documentation

| # | Task | Status | Notes |
|---|---|---|---|
| `DX1-80` | `Dx1_Smoke` CTest (see `DX1-18`) | ✅ | |
| `DX1-81` | `Dx1_SpriteBatch` CTest: rotation/scale/tint/flip pixel-verified, same rigor `FreeDirect_SpriteBatch` applied | ✅ | |
| `DX1-82` | `Dx1_Blend` CTest: all 4 blend modes + custom-`BlendState` fallback pixel-verified | ✅ | |
| `DX1-83` | `Dx1_AddressMode` CTest: `Wrap`/`Mirror`/`TextureFilter` sampling pixel-verified | ✅ | |
| `DX1-84` | `Dx1_SpriteFont` CTest | ✅ | |
| `DX1-85` | `Dx1_No3D` CTest: every 3D entry point throws/degrades per Phase O7's table | ✅ | |
| `DX1-86` | `docs/dx1-backend.md`: mirror `docs/freedirect-backend.md`'s table/status-legend structure | ✅ | |
| `DX1-87` | Update `CMakeLists.txt`'s `CNA_GRAPHICS_BACKEND` STRINGS docstring, `README.md` §1/§6, and `plan_dxold.md`'s status row for DX1 | ✅ | |
| `DX1-88` | Full `CnaTests`/DX1 CTest suite regression run under `-DCNA_GRAPHICS_BACKEND=DX1` (MinGW cross-compile) — confirm no unrelated suite breaks | ✅ | **Final: 5336 passed, 11 skipped, 48 failed** (`ctest`/`CnaTests.exe` run through Wine on the virtual display). Getting here required fixing several pre-existing, not-DX1-specific gaps this exact configuration (from-scratch MinGW cross-compile + full `CnaTests`) had never hit before (README.md's own CI caveat: the full suite has never run on Windows CI at all): two audio harnesses missing an `SDL3::SDL3` link (`cmake/Harnesses.cmake`), a POSIX-only (`geteuid`/`<unistd.h>`) media test needing a Windows guard, `CNA_ENABLE_NET=OFF`'s test glob never excluding ENet-dependent test files, and — the deepest one — `CnaLibrary.cmake`'s existing `CNA_FFMPEG_AVAILABLE=OFF` source exclusion (Video/VideoPlayer/VideoDecoder, deliberate on every Windows target) never being mirrored for the corresponding test files or for `VideoContentTypeReader.cpp`/its `XnbBuiltInReaders.cpp` registration call site. All are narrowly-scoped, mirror an already-established pattern in the same file, and are unrelated to DX1's own logic. Of the remaining 48 failures: 34 are 3D-content-loading tests (`SkinnedModelEXTPartTest`, `RuntimeGltfModelTest`, `CnjModelTest`/`CnjEffectTest`/`CnjTexture3DTest`/`CnjStockEffectTest`, `ModelContentTypeReaderTest`, etc.) hitting DX1's correct `ThrowNo3D` via a plain `GraphicsDevice gd;` fixture with no 2D-backend gate — the **identical** structural gap `plan_freedirect.md`'s own regression already documented and left explicitly out of scope; 5 are `GraphicsDeviceCapabilityTest.SupportsThreeD`/`SupportsDepthStencilBuffer`/`SupportsMultipleRenderTargets`/`SupportsOcclusionQuery`/`SupportsCustomEffects`, which assert these capabilities unconditionally true with **no backend gate at all** (would fail identically under `DX3`/`SDL_RENDERER`/`ASCII`/`CANVAS` too, confirmed by reading the test source — not new); 6 are `MediaLibraryTestFixture` song/album duration and genre tests reading `0ms`/wrong metadata, a real consequence of `CNA_FFMPEG_AVAILABLE=OFF` on every Windows target (pre-existing, predates this session); the remaining 4 (`PictureLibraryIndexTest` ×3, `AudioTagParserTest.ReadsNonAsciiVorbisCommentTitleCorrectly`) are Windows/Wine-filesystem or non-ASCII-encoding quirks unrelated to the graphics backend. One real, DX1-relevant test-gate fix was applied: `GraphicsDeviceValidationTest.SetRenderTargets_FourTargets_DoesNotThrow`'s backend gate didn't know about `DX1` either (mirrors `DX3-27`'s own identical fix) — added, confirmed fixed (49→48 failures). Zero DX1-caused failures remain unaccounted for. |

---

## Boundaries — explicitly out of scope for v1

- **`DirectSound`/`DirectInput`/`DirectPlay`** — design decision 8; CNA's existing audio/input
  stack is untouched.
- **No 3D pipeline, ever, for this backend** — not a v1-only gap; DirectX 1 has no Direct3D to call
  (§0). A future *DX2* backend (`plan_dxold.md` row 2) is where an execute-buffer Direct3D question
  first becomes meaningful.
- **8-bit/palette surfaces, `GetDC`/`ReleaseDC`, `SetPalette`/`CreatePalette`** — design decision 7.
- **Mip levels (`level>0` `SetData`)** — expected throw, pending `DX1-22` confirming no cheap real
  option exists.
- **Exclusive fullscreen (`DDSCL_EXCLUSIVE`) / real `SetDisplayMode` mode-switching** — design
  decision 4 scopes v1 to windowed (`DDSCL_NORMAL`) only; exclusive-fullscreen support (with a real
  mode switch) is a plausible future add-on, not required for XNA `SpriteBatch`/`Texture2D`
  parity.
- **Real Windows/macOS hardware verification** — this plan proves the backend via MinGW
  cross-compile + Wine on Linux in this dev environment, same caveat every Route-B CNA backend
  already carries.
- **`IDirectDraw2`+ features of any kind** (refresh-rate-aware display modes, `DDSURFACEDESC2`
  fields, …) — permanently out of scope for the `DX1` name specifically (§1); belongs to a later
  entry in `plan_dxold.md`'s roadmap.

---

## See also

- `plan_dxold.md` — the roadmap this plan is row 1 of.
- `plan_freedirect.md`, `docs/freedirect-backend.md` — the architecture and math this backend ports verbatim
  wherever the surface-layer difference doesn't matter.
- `plan_dx9.md` — the Route-B (MinGW + Wine) discipline this backend's CMake/testing
  infrastructure is modeled on.
- `docs/directx-legacy-backends-analysis.md` — the feasibility analysis that authorized this whole
  backend family.
