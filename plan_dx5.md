# DirectX 5 (DirectDraw v4 + Direct3D v3, FVF `DrawPrimitive`) Graphics Backend — Implementation Plan

> **Status (2026-07-21): all phases complete and verified.** 19/19 `DX5`-labeled CTests pass, all
> green on the first run after the port. See §2 for the spike result, the phase tables below for
> the implementation, and `docs/dx5-backend.md` for the full write-up.

---

## 0. TL;DR

- **New backend: `CNA_GRAPHICS_BACKEND=DX5`.** No temporary-naming concern this time (unlike
  `DX3`) — `DX5` is not already claimed by any shipping backend.
- **DX5 (1997) is the release where execute buffers disappear entirely.** `IDirect3DDevice3` only
  ever exposes `DrawPrimitive`/`DrawIndexedPrimitive` — matching `DX2`/`DX3`'s own already-proven
  choice of avoiding execute buffers, except now it's the *only* option the SDK itself offers, not
  a workaround for a broken path.
- **More invasive port than `DX3`'s.** `DX3` upgraded only the top `IDirectDraw` object to v2,
  keeping every surface at v1. DX5 needs *every* surface to be v4 (`IDirectDrawSurface4`,
  `DDSURFACEDESC2`, `DDSCAPS2`) — `IDirectDraw4::CreateSurface` returns `LPDIRECTDRAWSURFACE4`
  directly, and `IDirect3D3::CreateDevice` requires a v4 surface specifically. This touches every
  2D-layer surface (primary, shadow-backbuffer, Z-buffer, textures, render targets), not just the
  top-level object.
- **3D layer: `IDirect3D3`/`IDirect3DDevice3`/`IDirect3DViewport3`.** Texture object stays
  `IDirect3DTexture2` (confirmed no `IDirect3DTexture3` exists). `DrawPrimitive`/
  `DrawIndexedPrimitive`'s vertex-type parameter changes from the `D3DVERTEXTYPE` enum
  (`D3DVT_TLVERTEX`) to a `DWORD` FVF bitmask (`D3DFVF_TLVERTEX`, confirmed to expand to the exact
  same bit layout `D3DTLVERTEX` already has) — same struct, same CPU transform/clip math, same
  Phase O9 CPU lighting, ported from `DX3`, just resubmitted through the new parameter shape.
- **Real capability upgrade used, not just carried forward as a workaround:**
  `IDirect3DViewport3::Clear2` takes explicit color/z/stencil *values* — `DX2-24`/`DX3` had to
  work around `IDirect3DViewport(2)::Clear()`'s complete lack of such a parameter via a manual
  `Lock()`-the-Z-buffer-and-write-raw-values hack. DX5 uses the real `Clear2()` call instead —
  simpler, and no longer relies on an assumed-but-unverified raw Z-buffer memory layout.
- **New existence-gate spike (`DX5-0`, `dx5-spike/`)** confirmed all of the above genuinely works
  in this Wine environment — real Gouraud interpolation via the new FVF path, real Z-test
  occlusion, real texture sampling from a v4-owned surface, and a real, correct `Clear2` (found
  and fixed two spike-authoring bugs along the way, neither a Wine limitation — see
  `dx5-spike/README.md`).

---

## 1. What "DirectX 5" concretely means for this backend

| Layer | Symbol(s) used | Introduced in | Never used here |
|---|---|---|---|
| DirectDraw (2D) | `IDirectDraw4`, `IDirectDrawSurface4`, `DDSURFACEDESC2`, `DDSCAPS2` | DX5 SDK | `IDirectDraw`/`IDirectDraw2`/`IDirectDraw3` (skipped, `DX3` already used v2 — DX5 goes straight to v4, the real DX5-SDK-introduced revision), `IDirectDrawSurface`/`IDirectDrawSurface2`/`IDirectDrawSurface3`, `IDirectDraw7`+ |
| Direct3D device/object | `IDirect3D3`, `IDirect3DDevice3` | DX5 SDK (skips interface number "3" landing anywhere but DX5 — DX3 SDK used interface number "2"; DX5 is where execute buffers were dropped and the interface number jumped to "3", not to match the SDK version number, which doesn't happen until DX7) | `IDirect3D`/`IDirect3DDevice` (v1, execute-buffer only), `IDirect3D2`/`IDirect3DDevice2` (DX3, still real but superseded here), `IDirect3D7`+ |
| Viewport | `IDirect3DViewport3` | DX5 SDK | `IDirect3DViewport`/`IDirect3DViewport2` |
| Texture | `IDirect3DTexture2` | DX3 SDK (**unchanged** — no `IDirect3DTexture3` exists at all, confirmed by `grep` across the real MinGW headers) | `IDirect3DTexture` (v1) |
| Vertex format | `D3DTLVERTEX` (unchanged struct) | DX2 SDK (`d3dtypes.h`) | `D3DVERTEX`/`D3DLVERTEX` |
| Vertex-type selector | `DWORD` FVF bitmask (`D3DFVF_TLVERTEX`) | DX5 SDK | The old `D3DVERTEXTYPE` enum (`D3DVT_TLVERTEX`) — `IDirect3DDevice3::DrawPrimitive`'s parameter type genuinely changed from the enum to a `DWORD`, confirmed by inspecting both vtables side by side |
| Draw call | `IDirect3DDevice3::DrawPrimitive`/`DrawIndexedPrimitive` | DX5 SDK (execute buffers removed entirely as of this device revision) | `IDirect3DDevice::Execute`/`IDirect3DDevice2::DrawPrimitive` (superseded) |
| Depth/color clear | `IDirect3DViewport3::Clear2` (explicit color/z/stencil values) | DX5 SDK | `IDirect3DViewport(2)::Clear()` (no value parameter at all — `DX2-24`'s finding) |

**On the "IDirect3D3 for DirectX 5" naming**: this was double-checked (not assumed) against the
real MinGW headers before writing this plan, prompted by a direct question about whether
`IDirect3D3` "belongs" to DirectX 3 by its number. It does not — `IDirect3D2`/`IDirect3DDevice2`
(interface number "2") is the genuine DX3-SDK interface (`DX3`'s own plan already established
this); `IDirect3D3`/`IDirect3DDevice3` (interface number "3") is the next revision, introduced by
the DX5 SDK (DirectX 4 was never released on Windows). The interface-revision-number-to-SDK-
version-number offset that begins at DX3 (SDK 3 → interface 2) grows to two at DX5 (SDK 5 →
interface 3), and Microsoft doesn't re-align them until DX7 (SDK 7 → interface 7, from then on
matching exactly through D3D8/D3D9/etc).

Confirmed present in this environment's MinGW-w64 headers before writing this plan:
`IID_IDirectDraw4`, `IID_IDirect3D3`, `IID_IDirect3DDevice3`, `IID_IDirect3DViewport3`,
`IID_IDirect3DVertexBuffer` (exists, but genuinely unneeded here — see decision 8), the full
`IDirectDraw4`/`IDirect3DDevice3`/`IDirect3DViewport3` vtables, `DDSURFACEDESC2`/`DDSCAPS2`'s field
layouts (confirmed `DDSCAPS2.dwCaps` is field-position-compatible with `DDSCAPS.dwCaps`, and
`DDSURFACEDESC2` dropped the old top-level `dwZBufferBitDepth`/`DDSD_ZBUFFERBITDEPTH` in favor of
describing Z-buffer depth via `ddpfPixelFormat`'s `DDPF_ZBUFFER` flag instead — a real, spike-
verified field-migration detail, not assumed).

---

## 2. Existence-gate spike — `DX5-0` (run before any backend code)

| # | Spike | What it proves | Result |
|---|---|---|---|
| `DX5-0a` | `ddV1->QueryInterface(IID_IDirectDraw4, &dd4)`, then `CreateSurface`(`DDSURFACEDESC2`)/`Lock`/`Unlock` via v4 | Whether `IDirectDraw4` is a real, fully-functional 2D surface layer | ✅ Works |
| `DX5-0b` | `dd4->QueryInterface(IID_IDirect3D3, &d3d3)`, `d3d3->CreateDevice(IID_IDirect3DRGBDevice, v4surface, &device, nullptr)` | Whether the v4-surface-rooted Direct3D device chain works | ✅ Works |
| `DX5-0c` | `IDirect3DViewport3` creation, `SetViewport2` (same `D3DVIEWPORT2` struct as `DX2`/`DX3`) | Whether viewport setup is unchanged | ✅ Works |
| `DX5-0d` | `device->DrawPrimitive(D3DPT_TRIANGLELIST, D3DFVF_TLVERTEX, verts, 3, 0)` | Whether FVF-based submission of the same `D3DTLVERTEX` struct still Gouraud-interpolates correctly | ✅ Works |
| `DX5-0e` | `DrawIndexedPrimitive` + real `D3DZB_TRUE`/`D3DCMP_LESS` Z-test, two overlapping quads | Whether depth-test occlusion is real through the new FVF path | ✅ Works — exact `(255,0,0)` readback |
| `DX5-0f` | Real 2×2 texture via `IDirectDrawSurface4`+`IDirect3DTexture2` (unchanged interface, obtained from a v4 surface, `GetHandle` called via a `QueryInterface`'d `IDirect3DDevice2` view of the `device3` object), sampled via `DrawIndexedPrimitive` | Whether texture sampling still works when the owning surface is v4 and the device is v3 | ✅ Works — both sampled corners exact |
| `DX5-0g` | `IDirect3DViewport3::Clear2(1, &fullRect, D3DCLEAR_TARGET\|D3DCLEAR_ZBUFFER, color, z, stencil)`, then a z=0.8 quad against a `Clear2` z=0.75 | Whether `Clear2` is a real, working replacement for the `DX2`/`DX3` manual-Z-buffer-Lock workaround | ✅ Works — exact requested clear color read back, z=0.8 quad correctly Z-rejected |

**Two real bugs found in the spike itself, not in Wine/Direct3D5** (see `dx5-spike/README.md` for
the full detail): `Clear2`'s `count=0, rects=nullptr` does *not* mean "clear the whole surface"
(clears nothing, silently) — an explicit `D3DRECT{0,0,w,h}` with `count=1` is required, same
convention the old `Clear()` already used; and sampling the exact screen-center pixel of a
diagonally-split full-viewport quad lands on the triangle seam and reads a rasterization-edge
value, not either triangle's true interior color.

**Net effect**: DX5's entire 3D layer (CPU transform/clip, `D3DTLVERTEX` packing, lighting) is
architecturally identical to `DX3`'s already-proven one, needing only the FVF-parameter swap and
a real `Clear2`-based clear. Phase Q1 is unblocked.

---

## 3. Design decisions (recorded before implementation)

1. **Platform gate, same as `DX1`/`DX2`/`DX3`.** Same Windows-native-or-MinGW-cross-compile
   `FATAL_ERROR` gate.

2. **2D layer: every surface upgrades to v4** (`IDirectDrawSurface4`, `DDSURFACEDESC2`,
   `DDSCAPS2`), not just the top `IDirectDraw` object (unlike `DX3`'s decision 2, which kept
   surfaces at v1). `Impl::dd` is `LPDIRECTDRAW4` (upgraded via `QueryInterface` immediately after
   `DirectDrawCreate`, releasing the v1 pointer, same pattern as `DX3`); `Impl::primary`,
   `backBuffer`, `zbuffer`, and every `Dx5TextureBackend`/`Dx5RenderTargetBackend`'s owned surface
   become `LPDIRECTDRAWSURFACE4`. Field-level code (`dwFlags`, `dwWidth`, `dwHeight`,
   `ddsCaps.dwCaps`, `lpSurface`, `lPitch`) is otherwise unchanged — `DDSURFACEDESC2`/`DDSCAPS2`
   are field-position-compatible supersets for every field this backend actually uses. Z-buffer
   creation drops `DDSD_ZBUFFERBITDEPTH`/`dwZBufferBitDepth` (removed from `DDSURFACEDESC2`'s
   top level) in favor of `ddpfPixelFormat.dwFlags = DDPF_ZBUFFER` +
   `ddpfPixelFormat.dwZBufferBitDepth = 16` (spike-confirmed, `DX5-0a`'s own follow-up finding
   while building the Z-buffer surface for `DX5-0b`). **One more real signature change, found by
   the compiler while porting, not by the spike (a pure interface-signature difference, not a
   runtime behavior difference — no new spike was needed for it): `IDirectDrawSurface4::Unlock`
   takes an `LPRECT` (conventionally `nullptr`, meaning "the whole surface"), not the `LPVOID`
   locked-memory pointer `IDirectDrawSurface`/`IDirectDrawSurface2`/`IDirectDrawSurface3::Unlock`
   all take** — every `surface->Unlock(desc.lpSurface)` call ported from `DX3` had to become
   `surface->Unlock(nullptr)`.

3. **3D device bring-up: `IDirect3D3`/`IDirect3DDevice3`/`IDirect3DViewport3`, verbatim port of
   `DX3`'s bring-up sequence otherwise.** `IDirect3D3::CreateDevice` takes an extra trailing
   `IUnknown* outer` parameter vs. `IDirect3D2::CreateDevice` (always `nullptr` here, spike-
   confirmed sufficient). `IDirect3DViewport3::SetViewport2` takes the *same* `D3DVIEWPORT2`
   struct as `DX3`'s `IDirect3DViewport2::SetViewport2` — no new viewport struct exists.

4. **CPU transform/clip pipeline, `D3DTLVERTEX` packing, and Phase O9 CPU lighting: verbatim port
   of `DX3`'s own (itself a verbatim port of `DX2`'s post-Phase-O9 code), with exactly one
   change**: every `DrawPrimitive`/`DrawIndexedPrimitive` call site passes `D3DFVF_TLVERTEX`
   (a `DWORD`) instead of `D3DVT_TLVERTEX` (a `D3DVERTEXTYPE` enum value) as the vertex-type
   parameter — spike-confirmed (`DX5-0d`/`DX5-0e`) to render identically, since
   `D3DFVF_TLVERTEX`'s bit layout matches `D3DTLVERTEX`'s actual memory layout exactly.

5. **Real `Clear2`-based depth clearing, replacing `DX2`/`DX3`'s manual Z-buffer `Lock()`
   workaround.** `ClearDepth` routes through `viewport->Clear2(1, &fullRect, D3DCLEAR_ZBUFFER, 0,
   depth, 0)` — a real, direct API call with an explicit depth value, not an assumed raw Z-buffer
   memory layout. `fullRect` is always `{0,0,activeWidth,activeHeight}`. **Spike-confirmed gotcha,
   must not be reintroduced**: `count=0, rects=nullptr` clears nothing at all (silently) — always
   pass a real rect with `count=1`. `Clear(r,g,b,a)`'s own COLOR clear stays exactly as `DX2`/`DX3`
   already do it (a direct 2D `Lock()`+fill on `ActiveSurface()`, unrelated to Direct3D) —
   deliberately not switched to `Clear2`, since a custom-bound `RenderTarget2D` has no Direct3D
   device/viewport of its own to `Clear2` against (decision 4 already scopes 3D drawing, and
   therefore any real `Clear2` call, to the default backbuffer only). `ClearColorAndDepth` is
   therefore still `Clear(r,g,b,a)` + `ClearDepth(depth)`, matching `DX2`/`DX3`'s own structure —
   only `ClearDepth`'s own internal mechanism changed. `ClearStencil`/`ClearDepthAndStencil`/
   `ClearColorAndStencil`/`ClearColorDepthAndStencil` are unchanged from `DX2`/`DX3` (no real
   stencil buffer exists until DX6, decision 8 — the stencil value is still silently ignored, not
   routed through `Clear2` either).

6. **Texture creation: `IDirectDrawSurface4`-owned, `IDirect3DTexture2`-typed (unchanged
   interface), `GetHandle` called via a `QueryInterface`'d `IDirect3DDevice2` view of the
   `device3` object.** `IDirect3DTexture2::GetHandle` only ever accepted an `IDirect3DDevice2*`
   parameter (never gained an overload for `IDirect3DDevice3*`) — spike-confirmed
   (`DX5-0f`) that querying an `IDirect3DDevice2` interface off the same `device3` object works
   correctly (real historical COM aggregation), rather than assuming this would fail and needing
   a different texture-handle mechanism.

7. **`VertexBuffer`/`IndexBuffer`: plain CPU-side storage, unchanged from `DX3`.** A real
   `IDirect3DVertexBuffer` object exists at this DirectX era (confirmed present in the headers,
   `IID_IDirect3DVertexBuffer`, `IDirect3D3::CreateVertexBuffer`), but this backend's CPU-transform
   architecture (decision 4) never needs one — the CPU-side `std::vector<uint8_t>` storage
   `DX2`/`DX3` already use is reused verbatim, matching their own decision 8/7 reasoning: the
   transform happens on the CPU regardless of what GPU-side vertex-buffer object exists at this
   DirectX era.

8. **Lighting/fog/multitexture/envMap/skinning/stencil/MRT/instancing/occlusion-query/volume-cube
   textures/custom-effects: identical boundary to `DX3`/`DX2` post-Phase-O9.** Fixed-function
   lighting is real for the two normal-bearing vertex strides (32/52); fog, multitexture,
   environment mapping, and skinning remain accepted-and-ignored; the rest are genuinely
   unavailable at this DirectX era, same as `DX2`/`DX3`.

9. **32-bit surfaces only, `DirectSound`/`DirectInput`/`DirectPlay` out of scope, header
   containment, CMake integration shape** — identical to `plan_dx1.md`/`plan_dx2.md`/
   `plan_dx3.md`'s equivalent decisions, ported without change.

10. **CMake integration**: add `"DX5"` to `CNA_GRAPHICS_BACKEND`'s `STRINGS` property + a
    `CNA_BACKEND_DX5` option; a `cna_backend_graphics_dx5` static library target under
    `src/CNA/Internal/Backends/Dx5/`, same Windows-only `FATAL_ERROR` gate. Link set: `ddraw` +
    `dxguid` + `SDL3::SDL3` — identical to `DX1`/`DX2`/`DX3`.

11. **Testing: `scripts/run-wine-dx5.sh`**, modeled on `scripts/run-wine-dx3.sh` — same
    `~/.wine-cna-dx1` prefix is reusable (confirmed by this plan's own spike, which ran against
    it).

12. **No execute-buffer code anywhere in this backend.** Same grep-based discipline CTest as
    `DX1-1`/`DX2-1`/`DX30-9` (`DX5-1`), adapted to allow `IDirectDraw4`/`IDirectDrawSurface4`
    (this backend's own real, intended v4 surface) while still forbidding `IDirectDraw7`+/
    `IDirectDrawSurface[23567]`+ (DX5 is v4-only, not v3 and not v7+) and the literal execute-buffer
    surface.

13. **Full CTest suite is a straight rename/port of `DX3`'s own 19 `DX3`-labeled CTests to
    `DX5`-labeled equivalents**, plus updated expectations anywhere `Clear2`'s real depth/color
    values are now directly verifiable (where `DX2`/`DX3`'s own tests could only verify the Z-test
    *behavior*, not the exact clear color, due to the manual-Lock workaround's own limitations).

---

## 4. Active execution order

1. **`DX5-0`** (existence-gate spike, §2) — done, unblocks everything else.
2. **Phase Q1** (CMake integration + skeleton) — same shape as `DX30-1`..`DX30-6`.
3. **Phase Q2** (2D layer: port from `Dx3GraphicsBackend`, upgraded to v4 throughout, decision 2).
4. **Phase Q3** (3D device bring-up: verbatim port from `Dx3GraphicsBackend`, upgraded to
   `IDirect3D3`/`IDirect3DDevice3`/`IDirect3DViewport3`, decision 3, plus real `Clear2`-based
   clearing, decision 5).
5. **Phase Q4** (CPU transform/clip pipeline + Phase-O9 lighting: verbatim port, FVF parameter
   swap, decision 4).
6. **Phase Q5** (`VertexBuffer`/`IndexBuffer` backends: verbatim port, decision 7).
7. **Phase Q6** (state mapping: verbatim port).
8. **Phase Q7** (remaining `IGraphicsBackend` defaults: verbatim port).
9. **Phase Q8** (tests + `docs/dx5-backend.md`).

For every task: build the affected target (`-DCNA_GRAPHICS_BACKEND=DX5`, MinGW cross-compile), run
the relevant CTest through `scripts/run-wine-dx5.sh`, and do not mark a task ✅ without both
actually passing.

---

## Phase Q1 — CMake integration and skeleton

| # | Task | Status | Notes |
|---|---|---|---|
| `DX5-1` | Add `"DX5"` to `CNA_GRAPHICS_BACKEND`'s `STRINGS` property + `CNA_BACKEND_DX5` option; extend the Windows-only `FATAL_ERROR` gate; add the execute-buffer-discipline grep CTest (design decision 12) | ✅ | `Dx5_ExecuteBufferDiscipline` passing. |
| `DX5-2` | `cna_backend_graphics_dx5` static library target; confirm minimal link set empirically | ✅ | Same link set as `DX1`/`DX2`/`DX3` (`SDL3::SDL3 ddraw dxguid`). |
| `DX5-3` | `include/CNA/Internal/Backends/Dx5/Dx5GraphicsBackend.hpp` + `src/CNA/Internal/Backends/Dx5/Dx5GraphicsBackend.cpp`: port of `Dx3GraphicsBackend`'s files, upgraded to v4/`IDirect3D3` throughout | ✅ | |
| `DX5-4` | Factory dispatch for `DX5` in `CreateGraphicsBackend()` | ✅ | |
| `DX5-5` | `scripts/run-wine-dx5.sh` (design decision 11) | ✅ | Reuses `~/.wine-cna-dx1` prefix. |
| `DX5-6` | Confirm `CnaTests`/the new MinGW test binaries link cleanly against the new backend target under cross-compilation | ✅ | `cmake-build-dx5` configures/builds clean (MinGW cross, Release, ccache). Proactively added the matching `Dx5` entry to every `CNA_BACKEND_*` registry file found via `grep -rl "CNA_BACKEND_DX3\b"` (the exact lesson `DX30-83` recorded) before the first build attempt. One real signature difference the compiler itself caught (not proactively known, not spiked separately since it's a pure interface-signature difference): `IDirectDrawSurface4::Unlock` takes `LPRECT` (conventionally `nullptr`), not the `LPVOID` locked-memory pointer `IDirectDrawSurface`/`2`/`3::Unlock` all take — every ported `surface->Unlock(desc.lpSurface)` call had to become `surface->Unlock(nullptr)` (4 call sites). Also initially forgot to add `include(cmake/Tests/Dx5Tests.cmake)` to the root `CMakeLists.txt` (caught immediately by "no rule to make target" on the first test build attempt). |

## Phase Q2 — 2D layer (port from `Dx3GraphicsBackend`, upgraded to v4 throughout)

| # | Task | Status | Notes |
|---|---|---|---|
| `DX5-10` | Device/window bring-up: `DirectDrawCreate` → `QueryInterface(IID_IDirectDraw4)` upgrade (decision 2) → `SetCooperativeLevel`/primary surface/shadow-backbuffer/`Clear`/`Present`, all via `LPDIRECTDRAWSURFACE4`/`DDSURFACEDESC2` | ✅ | |
| `DX5-11` | Texture/render-target backends, `LPDIRECTDRAWSURFACE4`-owned | ✅ | |
| `DX5-12` | `SpriteBatch` CPU compositor, all rotation/scale/flip/blend/sampling paths | ✅ | Port unchanged in logic, re-typed to v4. |
| `DX5-13` | `SpriteFont` | ✅ | Port unchanged in logic, re-typed to v4. |
| `DX5-14` | `TransformWindowToLogical`/`TransformLogicalToWindow`/letterbox present math | ✅ | Port unchanged from `DX3`. |
| `DX5-15` | Renamed 2D CTests passing, pixel-verified | ✅ | |

## Phase Q3 — Direct3D v3 device bring-up + real `Clear2`

| # | Task | Status | Notes |
|---|---|---|---|
| `DX5-20`..`DX5-25` | `IDirect3D3`/`IDirect3DDevice3`/viewport/Z-buffer bring-up (decisions 2/3), `Dx5_Device3DSmoke` CTest | ✅ | Z-buffer creation uses `DDPF_ZBUFFER`/`dwZBufferBitDepth` on `ddpfPixelFormat` (decision 2's spike-found field migration), not the old top-level field. |
| `DX5-26` | Real `Clear2`-based `ClearDepth` (decision 5), replacing `DX2`/`DX3`'s manual Z-buffer `Lock()`/`FillZBuffer16` workaround (removed as dead code) | ✅ | Always passes an explicit `D3DRECT{0,0,w,h}` with `count=1` — the spike-found `count=0` gotcha, deliberately never reintroduced. `Clear(r,g,b,a)`'s own color clear is deliberately unchanged (still the 2D `Lock()`+fill on `ActiveSurface()`) — see decision 5's own narrower-scope note for why. `ClearStencil`/`ClearDepthAndStencil`/`ClearColorAndStencil`/`ClearColorDepthAndStencil` are likewise unchanged (still no real stencil buffer at this era). |

## Phase Q4 — CPU transform/clip pipeline + `DrawPrimitive` submission (verbatim port, FVF swap)

| # | Task | Status | Notes |
|---|---|---|---|
| `DX5-30`..`DX5-39` | CPU clip/transform math, `D3DTLVERTEX` packing (unchanged), `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives`/`DrawPrimitivesEx`/`DrawIndexedPrimitivesEx`, depth-test/texture/clipping CTests, all submitting via `D3DFVF_TLVERTEX` instead of `D3DVT_TLVERTEX` (decision 4) | ✅ | Verbatim port of `DX30-30`..`DX30-39`/`DX2-30`..`DX2-39`. |
| `DX5-40` | CPU-side BasicEffect lighting (Phase O9's `Dx2ComputeVertexLighting`, specular via `D3DRENDERSTATE_SPECULARENABLE`) | ✅ | Verbatim port of `DX30-40`/`DX2-91`..`DX2-96`'s implementation. `Dx5_Lighting` CTest (4/4, same checks). |

## Phase Q5 — `VertexBuffer`/`IndexBuffer` backends (verbatim port, decision 7)

| # | Task | Status | Notes |
|---|---|---|---|
| `DX5-50`, `DX5-51` | `Dx5VertexBufferBackend`/`Dx5IndexBufferBackend`, `SetData`/`GetData` round-trip test | ✅ | Verbatim port of `DX30-50`/`DX30-51`. |

## Phase Q6 — State mapping (verbatim port)

| # | Task | Status | Notes |
|---|---|---|---|
| `DX5-60`..`DX5-64` | `ApplyDepthStencilState`/`ApplyRasterizerState`/`ApplyBlendState`/`ApplySamplerState`, `SetDepthTestEnabled`/`SetBlendEnabled`/`SetDepthWriteEnabled` | ✅ | Verbatim port of `DX30-60`..`DX30-64`. `WireFrame` reports `true` (inherited finding); `AnisotropicFiltering` stays `false` (inherited finding). |

## Phase Q7 — Remaining `IGraphicsBackend` defaults (verbatim port)

| # | Task | Status | Notes |
|---|---|---|---|
| `DX5-70`..`DX5-76` | Occlusion query/volume-cube-textures/custom-effects/instancing/remaining clears/`DebugSimulateContextLoss` | ✅ | Verbatim port of `DX30-70`..`DX30-76`. |

## Phase Q8 — Tests and documentation

| # | Task | Status | Notes |
|---|---|---|---|
| `DX5-80` | Full renamed 2D+3D CTest suite passing | ✅ | 19/19 `DX5`-labeled CTests. |
| `DX5-81` | `docs/dx5-backend.md`: mirror `docs/dx3-backend.md`'s structure | ✅ | |
| `DX5-82` | Update `CMakeLists.txt`'s `CNA_GRAPHICS_BACKEND` STRINGS docstring, `README.md`, and `plan_dxold.md`'s DX5 row | ✅ | |
| `DX5-83` | Full `DX5`-labeled CTest suite regression + targeted cross-backend test re-run (mirroring `DX30-83`'s scope decision) | ✅ | |

---

## Boundaries — explicitly out of scope for `DX5`

Identical to `plan_dx3.md`'s own Boundaries section: execute-buffer Direct3D (permanently
excluded, doesn't exist at this device revision anyway); fog (lighting itself is real for
normal-bearing strides); multitexture/env-mapping/skinning (accepted-and-ignored); stencil
operations (no real stencil buffer/ops confirmed usable until later — `D3DRENDERSTATE`-level
stencil support exists in the header but was not part of this plan's scope to verify, matching
`DX2`/`DX3`'s own identical stencil boundary); `IDirectDraw7`+/`IDirectDrawSurface[23567]`+
features; `DirectSound`/`DirectInput`/`DirectPlay`; MRT/instancing/occlusion query/volume-cube
textures/custom programmable effects; real vertex-buffer GPU objects (decision 7 — genuinely
unneeded, not a gap); real Windows/macOS hardware verification (MinGW cross-compile + Wine on
Linux only, same caveat every Route-B CNA backend carries).

---

## See also

- `plan_dxold.md` — the roadmap this plan is row 5 of.
- `plan_dx3.md`, `docs/dx3-backend.md` — the backend this plan ports (2D layer upgraded further
  to v4; 3D layer upgraded to `IDirect3D3`/FVF but otherwise identical, including Phase O9
  lighting/`WireFrame`).
- `plan_dx2.md`, `docs/dx2-backend.md` — the original source of the CPU transform/clip pipeline and
  Phase O9 lighting math, two generations back.
- `dx5-spike/README.md` — the full `DX5-0` spike record, including the two spike-authoring bugs
  found and fixed (`Clear2`'s rect requirement, diagonal-seam sample point).
