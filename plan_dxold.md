# Old DirectX Backend Family (DX1/2/3/5/6/7/8/10) — Roadmap

> Short, English-language index for the whole "legacy DirectX" backend line, per the project
> owner's direct instruction (2026-07-20). This file stays short on purpose — each version gets
> its own full `plan_dxN.md` (this repo's standing convention, see `plan_dx3.md`/`plan_dx9.md`)
> when its turn comes. Background analysis: `docs/directx-legacy-backends-analysis.md`.

## Owner's instruction (translated from Czech, verbatim intent)

> Add a new graphics backend, DirectX 1, to CNA. 2D only; throws for 3D. Create `plan_dx1.md` and
> then implement it (ask if anything is unclear). **Do not use `free-direct`** — it must use real
> DirectX 1, or Wine, or Proton. We will later also do DirectX 2, 3, 5, 6, 7, 8, 10. Regarding
> DirectX 3: it is currently covered by `free-direct` (the `DX3` backend), but that backend will
> later be renamed in CNA to `FREE_DIRECT`, and a *real* DX3 (not on `free-direct`) will be
> implemented separately under the `DX3` name at that time. Write the plan for the whole
> 1/2/3/5/6/7/8/10 line, briefly, in English, into `plan_dxold.md`.

## Ground rule for every backend in this family

**Route B only — real Windows SDK headers (MinGW-w64) + genuine COM interfaces of the correct
version + Wine/Proton translation.** This is the same delivery mechanism the shipping
`D3D9`/`D3D11`/`D3D12` backends already prove works in this environment (`docs/
directx-legacy-backends-analysis.md` §4, Route B). **No reimplementation library** (no
`free-direct`, no hand-rolled DirectDraw/Direct3D clone) is used for any backend opened under this
roadmap — that is what makes these "real DirectX", the owner's explicit bar. Each backend's own
`plan_dxN.md` runs its own existence-gate spike (MinGW header check + a real Wine run) before any
backend code is authorized, mirroring `plan_dx9.md`'s `D9-0` discipline.

## Version-by-version plan

| # | Version | Year | Graphics API surface | 3D policy | CMake backend name | Plan doc | Status |
|---|---|---|---|---|---|---|---|
| 1 | **DX1** | 1995 | DirectDraw v1 only (`IDirectDraw`/`IDirectDrawSurface`/`DDSURFACEDESC`) — no Direct3D exists yet | throw (no code path exists at all) | `DX1` | `plan_dx1.md` | 🟡 in progress (this session) |
| 2 | **DX2** | 1996 | DirectDraw v1 (unchanged) + first Direct3D, **execute buffers** (`IDirect3D`/`IDirect3DDevice`/`IDirect3DExecuteBuffer`), single texture, no `DrawPrimitive` | 2D real; 3D optional/best-effort later (own decision when opened) — v1 scope is 2D-only + `ThrowNo3D`, matching DX1 | `DX2` | `plan_dx2.md` (not yet written) | ⬜ not started |
| 3 | **DX3 (real)** | 1996 | DirectDraw v2 (`IDirectDraw2`, adds refresh-rate to `SetDisplayMode`) + execute-buffer Direct3D, matured | throw for v1 (2D-only baseline), same optional-3D question as DX2 | `DX3` (⚠ reassigned — see rename note) | `plan_dx3_real.md` (not yet written) | ⬜ not started — blocked on the `FREE_DIRECT` rename below |
| 4 | *(DX4)* | — | never released | — | — | — | n/a |
| 5 | **DX5** | 1997 | DirectDraw v4 + Direct3D `DrawPrimitive` (modern draw-call model begins) | 2D real (HW quads); fixed-function 3D optional/best-effort, own decision when opened | `DX5` | `plan_dx5.md` (not yet written) | ⬜ not started |
| 6 | **DX6** | 1998 | + multitexturing, DXTn, stencil, `IDirect3DVertexBuffer` | same shape as DX5 | `DX6` | `plan_dx6.md` (not yet written) | ⬜ not started |
| 7 | **DX7** | 1999 | + hardware T&L, cube env maps — peak fixed-function | same shape as DX5/6; per `docs/directx-legacy-backends-analysis.md` §5, DX5/6/7 are strong candidates to share one fixed-function implementation core even if each stays its own selectable `CNA_GRAPHICS_BACKEND` value | `DX7` | `plan_dx7.md` (not yet written) | ⬜ not started |
| 8 | **DX8** | 2000 | DirectDraw+Direct3D merged; Shader Model 1.x | 2D real; fixed-function 3D + SM1.x shaders — most feasible legacy 3D target (DXVK ships a `d3d8` runtime, same DXVK toolchain as shipping `D3D9`) | `D3D8` | `plan_d3d8.md` (not yet written) | ⬜ not started |
| 9 | *(DX9)* | 2002 | Direct3D 9, Shader Model 2.0/3.0 | full — **already shipping** | `D3D9` | `plan_dx9.md` | ✅ done (pre-existing) |
| 10 | **DX10** | 2006 | Direct3D 10 — unified shader model 4.0, no fixed-function at all | full 3D expected (no legacy fixed-function fallback exists in DX10 itself) — needs its own existence-gate spike (MinGW `d3d10.h` availability + Wine/DXVK `d3d10` coverage) before any capability claim | `D3D10` | `plan_d3d10.md` (not yet written) | ⬜ not started |
| — | *(DX11/12)* | 2009/15 | — | full — **already shipping** | `D3D11`/`D3D12` | `plan_dx.md` | ✅ done (pre-existing) |

Execution order follows the table top to bottom (DX1 → DX2 → real DX3 → DX5 → DX6 → DX7 → D3D8 →
D3D10), each gated on its own spike and its own `plan_dxN.md`. DX9/11/12 already exist and are out
of this roadmap's scope.

## The `DX3` naming transition (owner-authorized, not yet executed)

- **Today**: `CNA_GRAPHICS_BACKEND=DX3` is the existing, shipped `free-direct`-backed 2D DirectDraw
  backend (`plan_dx3.md`, `docs/dx3-backend.md`). It stays exactly as-is until the rename below is
  actually carried out as its own task — this roadmap does not touch it.
- **Future task**: rename that backend's `CNA_GRAPHICS_BACKEND` value (and matching
  `CNA_BACKEND_*`/library-target/file-path identifiers) from `DX3` to `FREE_DIRECT`, freeing up the
  `DX3` name for a *real* (Route B) DirectX 3 backend, built the same way `DX1` is being built now —
  real `ddraw.h`/`IDirectDraw2` interfaces, MinGW cross-compile, Wine translation, **no
  `free-direct`**. That real-DX3 work is row 3 above and gets its own `plan_dx3_real.md` (working
  name; the final filename may reuse `plan_dx3.md` once the rename lands and the old content is
  archived — decide at that task's own start, not here).
- Until the rename task actually runs, do not reuse the bare `DX3` `CMakeLists.txt`/CMake-option
  identifiers for anything else — that would collide with the still-shipping backend.

## What's shared across every backend in this family

- **2D via `SpriteBatch`**: every version's `IDirectDrawSurface::Blt`/`BltFast` has never supported
  rotation (true in every DirectX release, v1 through v7) — every backend needs the same CPU
  quad-compositor architecture DX3 already built and proved (pivot/rotation math, per-pixel blend
  formulas for `Opaque`/`AlphaBlend`/`NonPremultiplied`/`Additive`, `Wrap`/`Mirror` sampling). Port
  that code, don't re-derive it, for DX1/DX2/real-DX3/DX5/DX6/DX7. DX5+ *can* additionally use
  hardware `DrawPrimitive` quads for the identity/no-rotation fast path if a later task chooses to
  add it — optional, not required for parity.
- **HWND**: real Win32 `HWND` via `SDL_GetPointerProperty(SDL_GetWindowProperties(window),
  SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr)` on CNA's already-existing `SDL_Window*` — same
  pattern `D3D9GraphicsBackend.cpp` already uses. No custom window/message-loop code in any of
  these backends.
- **Header containment**: real Windows headers (`ddraw.h`, `d3d.h`, …) only inside each backend's
  own `src/CNA/Internal/Backends/<Name>/*.cpp` + a private pimpl header — never in a public CNA
  header, matching `D3D11`/`D3D12`/`DX3`'s existing discipline.
- **DirectSound/DirectInput/DirectPlay stay permanently out of scope** for every backend in this
  family — CNA's existing audio/input stack is untouched, same as `DX3` design decision 3.
- **No backend in this family targets the `D3D9` oracle's byte-exact-vs-real-XNA bar.** They are
  retro/alternative backends (peer to `DX3`/`ASCII`/`CANVAS`), validated by their own pixel checks,
  not by indistinguishability from real XNA 4.0.
- **Each backend's existence-gate spike is mandatory and its own task** — confirm the MinGW header
  exists, confirm Wine/DXVK/Proton actually translates that DirectX version's API on this machine,
  *before* writing backend code. Do not assume from the analysis doc; it explicitly says its
  Route-B feasibility marks are unverified.

## See also

- `docs/directx-legacy-backends-analysis.md` — the feasibility analysis this roadmap turns into
  actual tasks.
- `plan_dx1.md` — DX1's own full implementation plan (this session).
- `plan_dx3.md`, `docs/dx3-backend.md` — the shipping `free-direct`-backed DX3, pending its
  `FREE_DIRECT` rename.
- `plan_dx9.md`, `plan_dx.md` — the shipping D3D9/D3D11/D3D12 plans; their Route-B conventions
  (CTest shape, Wine wrapper scripts, MinGW toolchain file, header-containment discipline) are what
  every backend in this roadmap inherits.
