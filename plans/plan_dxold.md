# Old DirectX Backend Family (DX1/2/3/5/6/7/8/10) — Roadmap

> Short, English-language index for the whole "legacy DirectX" backend line, per the project
> owner's direct instruction (2026-07-20). This file stays short on purpose — each version gets
> its own full `plan_dxN.md` (this repo's standing convention, see `plan_freedirect.md`/`plan_dx9.md`)
> when its turn comes. Background analysis: `docs/directx-legacy-backends-analysis.md`.
>
> **Status (2026-07-21): the entire 1/2/3/5/6/7/8/10 line is done.** All 8 backends in the table
> below are implemented, spiked, CTest-verified, and documented.
> **Update (2026-08-04):** the one item that remained — the `DX3` naming transition — has been
> executed (see its own section below): the `free-direct`-backed backend is now `FREEDIRECT` and
> the real DirectX 3 backend now ships as `DX3`.

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
| 1 | **DX1** | 1995 | DirectDraw v1 only (`IDirectDraw`/`IDirectDrawSurface`/`DDSURFACEDESC`) — no Direct3D exists yet | throw (no code path exists at all) | `DX1` | `plan_dx1.md` | ✅ done (2026-07-20) — 10/10 CTests passing, see `docs/dx1-backend.md` |
| 2 | **DX2** | 1996 | DirectDraw v1 (unchanged); 3D built on `IDirect3D2`/`IDirect3DDevice2`'s `DrawPrimitive`/`DrawIndexedPrimitive` (DX3-SDK addition) rather than the literal DX2-SDK execute-buffer API (`IDirect3D`/`IDirect3DDevice`/`IDirect3DExecuteBuffer`) — **spike-proven non-functional in this Wine environment**, see below | 2D real (ported from DX1); 3D real geometry/Z-test/one-texture/blend/`WireFrame` via `DrawPrimitive`, plus (Phase O9) real CPU-side ambient+directional lighting for normal-bearing vertices; fog/multitexture/stencil/`AnisotropicFiltering` accepted-and-ignored or empirically confirmed absent (owner-confirmed scope) | `DX2` | `plan_dx2.md` | ✅ done (2026-07-21) — all 9 phases complete (O1-O8 baseline + O9 lighting/`WireFrame` improvement), 19/19 dedicated CTests + full 5415-test `CnaTests` regression (19 failed/1 not-run, all pre-existing or documented scope boundaries, zero DX2-caused), real 3D rendering + lighting verified, see `docs/dx2-backend.md` |
| 3 | **DX3 (real)** | 1996 | DirectDraw v2 (`IDirectDraw2`, adds refresh-rate to `SetDisplayMode` — confirmed real via `DX30-0`, but dead code for this backend family's windowed-only design, same as DX1/DX2); 3D built on `IDirect3D2`/`IDirect3DDevice2::DrawPrimitive` (already a DX3-SDK addition, verbatim port of DX2's own post-Phase-O9 3D layer, incl. CPU lighting/`WireFrame`) rather than the execute-buffer surface (proven non-functional, same `DX2-0` finding) | 2D real (mechanical port of DX2, upgraded to `IDirectDraw2`); 3D real geometry/Z-test/one-texture/blend/`WireFrame`/CPU lighting, identical to DX2's own boundary | `DX3` (renamed from the temporary `DX30` on 2026-08-04 — see the naming-transition section) | `plan_dx3.md`, `docs/dx3-backend.md` | ✅ done (2026-07-21) — 19/19 dedicated CTests pass (all green on first run, mechanical port of DX2), see `docs/dx3-backend.md` |
| 4 | *(DX4)* | — | never released | — | — | — | n/a |
| 5 | **DX5** | 1997 | DirectDraw v4 (`IDirectDraw4`/`IDirectDrawSurface4`/`DDSURFACEDESC2`/`DDSCAPS2`, every surface not just the top object) + Direct3D v3 (`IDirect3D3`/`IDirect3DDevice3`/`IDirect3DViewport3`), execute buffers gone entirely — `DrawPrimitive`/`DrawIndexedPrimitive` selected via the `D3DFVF_TLVERTEX` FVF bitmask instead of the old `D3DVERTEXTYPE` enum (a port of `DX30`'s own already-proven 3D layer, incl. CPU lighting/`WireFrame`) | 2D real (mechanical port of `DX30`, upgraded to v4 throughout); 3D real geometry/Z-test/one-texture/blend/`WireFrame`/CPU lighting, identical to `DX2`/`DX30`'s own boundary, plus a real `Clear2`-based depth clear replacing the manual Z-buffer `Lock()` workaround | `DX5` | `plan_dx5.md`, `docs/dx5-backend.md` | ✅ done (2026-07-21) — 19/19 dedicated CTests pass, all green on first run, see `docs/dx5-backend.md` |
| 6 | **DX6** | 1998 | Same `IDirect3D3`/`IDirect3DDevice3`/`IDirect3DViewport3`/`IDirectDraw4` as DX5 -- no new COM interface at all (confirmed via header inspection); new render states only: real stencil buffer (this backend's deliverable), multitexturing (deferred, `D3DFVF_TLVERTEX` carries only one UV pair), DXTn (out of scope, no consumer) | 2D/3D identical to DX5's own boundary, plus a real combined depth+stencil Z-buffer (`DDPF_ZBUFFER\|DDPF_STENCILBUFFER`, 32-bit total) and real `D3DRENDERSTATE_STENCIL*` write/test wiring | `DX6` | `plan_dx6.md`, `docs/dx6-backend.md` | ✅ done (2026-07-21) — 20/20 dedicated CTests pass (19 ported + new `Dx6_Stencil`), all green on first run, see `docs/dx6-backend.md` |
| 7 | **DX7** | 1999 | Genuinely new `IDirectDraw7`/`IDirect3D7`/`IDirect3DDevice7` (via `DirectDrawCreateEx`); the whole viewport object is REMOVED (`SetViewport`/`Clear` direct on the device); texture binding is a direct `SetTexture(stage, surface)` call (no handle indirection); hardware T&L real in this Wine but deliberately not adopted; cube env maps deferred (same `D3DFVF_TEXCOORDSIZE3` reasoning as DX6's multitexture) | 2D/3D identical to DX6's own boundary (stencil unchanged, ported verbatim), plus the 3 architectural changes above -- real finding: legacy `D3DRENDERSTATE_TEXTUREMAPBLEND` is rejected by DX7, replaced with `SetTextureStageState`/`D3DTOP_MODULATE` | `DX7` | `plan_dx7.md`, `docs/dx7-backend.md` | ✅ done (2026-07-21) — 20/20 dedicated CTests pass (19 ported + renamed `Dx7_Stencil`), see `docs/dx7-backend.md` |
| 8 | **DX8** | 2000 | DirectDraw+Direct3D merged; no DirectDraw at all — genuinely new `IDirect3D8`/`IDirect3DDevice8` (via DXVK's D8VK, `Direct3DCreate8`, not Wine's builtin ddraw/d3d8); `D3DTLVERTEX` gone (generic FVF model, hand-defined `Dx8TLVertex` struct); no scaled-blit primitive at all (logical-render-target + letterbox-quad `Present()`); real GPU blending, no preset-detection fallback | 2D real (GPU-quad `SpriteBatch`, a redesign not a port); fixed-function 3D only (owner-confirmed scope — real XNA effects need `ps_2_0`+ regardless of SM1.x, so no real Shader Model 1.x pipeline); `AnisotropicFiltering` real (unlike DX2-DX7, real GPU via DXVK) | `DX8` | `plan_dx8.md`, `docs/dx8-backend.md` | ✅ done (2026-07-21) — 20/20 dedicated CTests pass, see `docs/dx8-backend.md` |
| 9 | *(DX9)* | 2002 | Direct3D 9, Shader Model 2.0/3.0 | full — **already shipping** | `D3D9` | `plan_dx9.md` | ✅ done (pre-existing) |
| 10 | **D3D10** | 2006 | Direct3D 10 — unified shader model 4.0, no fixed-function at all; real HLSL `vs_4_0`/`ps_4_0` shaders everywhere; delivered via Wine's own `d3d10.dll`/`d3d10_1.dll` forwarding to DXVK's `d3d10core.dll` (DXVK ships no `d3d10.dll` of its own) + DXVK's `dxgi.dll`; real state OBJECTS (`ID3D10BlendState`/etc), real MRT | 2D real (GPU-quad `SpriteBatch` via real shaders); 3D real (`DrawColoredPrimitives`, vertex-color only, matching `BasicEffect(VertexColorEnabled=true)`) — owner-confirmed v1 scope; lighting/texturing via `DrawPrimitivesEx`, custom effects, occlusion query all deferred to `IGraphicsBackend`'s own safe defaults | `D3D10` | `plan_d3d10.md`, `docs/d3d10-backend.md` | ✅ done (2026-07-21) — 10/10 dedicated CTests pass, see `docs/d3d10-backend.md` |
| — | *(DX11/12)* | 2009/15 | — | full — **already shipping** | `D3D11`/`D3D12` | `plan_dx.md` | ✅ done (pre-existing) |

Execution order follows the table top to bottom (DX1 → DX2 → real DX3 → DX5 → DX6 → DX7 → DX8 →
D3D10), each gated on its own spike and its own `plan_dxN.md`. DX9/11/12 already exist and are out
of this roadmap's scope.

## The `DX3` naming transition — EXECUTED 2026-08-04

- **Done (2026-07-21)**: the real (Route B) DirectX 3 backend itself — real `ddraw.h`/`IDirectDraw2`
  interfaces, MinGW cross-compile, Wine translation, **no `free-direct`** — was implemented and
  shipped under the temporary name `DX30`, per the project owner's own instruction to build it
  under a temporary name rather than wait for the rename. It is architecturally a mechanical port
  of `DX2`'s own 2D+3D layers, upgraded to `IDirectDraw2`.
- **Done (2026-08-04, owner instruction, dxold integration)**: the transition itself ran, in the
  order this section always prescribed. First the `free-direct`-backed backend gave up the name —
  `CNA_GRAPHICS_BACKEND=DX3` became **`FREEDIRECT`** (`CNA_BACKEND_FREEDIRECT`,
  `FreeDirectGraphicsBackend`, `plan_freedirect.md`, `docs/freedirect-backend.md`; the owner's
  spelling `FREEDIRECT` supersedes this section's earlier `FREE_DIRECT` sketch). Then the real
  DirectX 3 backend took it — `DX30` became **`DX3`** (mechanical `Dx30`→`Dx3` class/file/directory
  renames, `DX30`→`DX3` CMake option/define renames; `plan_dx30.md`→`plan_dx3.md`,
  `docs/dx30-backend.md`→`docs/dx3-backend.md`, `dx30-spike/`→`dx3-spike/`).
- **Task-ID provenance**: the historical `DX30-*` task IDs (this backend) and `DX3-*`/`X*` task IDs
  (the FreeDirect backend, from its DX3-named era) are kept verbatim in their plan files and are
  not renumbered — renaming recorded identifiers would break every citation of them.

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
- `plan_dx2.md`, `docs/dx2-backend.md` — DX2's own full implementation plan, the backend `DX30`
  ports verbatim.
- `plan_dx3.md`, `docs/dx3-backend.md` — the real DirectX 3 backend, temporarily named `DX30`.
- `plan_dx5.md`, `docs/dx5-backend.md` — the real DirectX 5 backend (DirectDraw v4 + Direct3D v3
  FVF `DrawPrimitive`), a further port of `DX30`'s own 2D+3D layers.
- `plan_dx6.md`, `docs/dx6-backend.md` — DX6's real stencil buffer, same interfaces as DX5.
- `plan_dx7.md`, `docs/dx7-backend.md` — DX7's genuinely new `IDirectDraw7`/`IDirect3D7`, viewport
  object removed.
- `plan_dx8.md`, `docs/dx8-backend.md` — DX8, no DirectDraw at all, DXVK-delivered, fixed-function
  3D only.
- `plan_d3d10.md`, `docs/d3d10-backend.md` — D3D10, no fixed-function pipeline at all, real HLSL
  `vs_4_0`/`ps_4_0` shaders everywhere, delivered via Wine's own `d3d10.dll` + DXVK's `d3d10core.dll`.
- `plan_freedirect.md`, `docs/freedirect-backend.md` — the shipping `free-direct`-backed DX3, pending its
  `FREE_DIRECT` rename.
- `plan_dx9.md`, `plan_dx.md` — the shipping D3D9/D3D11/D3D12 plans; their Route-B conventions
  (CTest shape, Wine wrapper scripts, MinGW toolchain file, header-containment discipline) are what
  every backend in this roadmap inherits.
