# DirectX 7 (`IDirectDraw7` + `IDirect3D7`, flattened device model) Graphics Backend — Implementation Plan

> **Status (2026-07-21): `DX7-0` spike AND implementation phases S1-S8 all complete and verified.**
> 20/20 `DX7`-labeled CTests pass (19 ported + the renamed `Dx7_Stencil`), on the second attempt: the
> `DX7-0` spike didn't exercise texture BLENDING, and Wine's `IDirect3DDevice7::SetRenderState`
> rejects the legacy `D3DRENDERSTATE_TEXTUREMAPBLEND` render state outright ("Render state 0x15 is
> invalid in d3d7") -- fixed by using `SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE)`
> instead (see §3 decision 6's own note). Targeted cross-backend regression matches every prior
> backend in this family's own baseline exactly (same 3 pre-existing, already-documented `DX2-84`
> ungated-test-class failures, zero new ones). See §2 for the spike result and the phase tables
> below for per-task detail.

---

## 0. TL;DR

- **New backend: `CNA_GRAPHICS_BACKEND=DX7`.** No temporary-naming concern.
- **DX7 (1999) is a real architectural change to the object graph** — unlike `DX6` (no new
  interface at all vs. `DX5`), DX7 introduces genuinely new interfaces AND removes a whole object
  category this backend family has carried since `DX2-0`:
  1. **`IDirectDraw7`/`IDirectDrawSurface7` are new interfaces.** `ddraw.h` itself documents
     `IDirectDraw7` as "not interchangeable with earlier DirectDraw interfaces" — spike-confirmed
     the old `DirectDrawCreate()`+`QueryInterface()` chain still empirically works, but this plan
     uses the new, correct-for-this-era `DirectDrawCreateEx()` entry point instead (`DX7-0` Test A2).
  2. **`IDirect3DViewport3` is GONE ENTIRELY.** `IDirect3D7` has no `CreateViewport` method at all.
     `IDirect3DDevice7::SetViewport(D3DVIEWPORT7*)` and `IDirect3DDevice7::Clear(...)` replace the
     whole `CreateViewport`/`AddViewport`/`SetCurrentViewport`/`DeleteViewport`/`Clear2` object
     this backend family has carried since `DX2-0` — spike-confirmed (`DX7-0` Tests D/E/F) working
     with no viewport object created at all.
  3. **`IDirect3D7::CreateDevice` drops the trailing `IUnknown* outer` parameter** `DX5`/`DX6`'s
     `IDirect3D3::CreateDevice` had (present on `DX5`/`DX6`, absent on `DX2`/`DX3`, absent again
     on `DX7` — a real signature oscillation across this backend family's own history).
  4. **`IDirect3DDevice7::SetTexture(stage, IDirectDrawSurface7*)` binds a texture directly from
     the surface pointer** — no more `D3DRENDERSTATE_TEXTUREHANDLE` + `IDirect3DTexture2::GetHandle`
     + the `QueryInterface(IID_IDirect3DDevice2)` workaround `DX5`'s own design decision 6 needed.
     Spike-confirmed (`DX7-0` Test G) this samples correctly.
- **Stencil (`D3DRENDERSTATE_STENCIL*`) is completely unchanged from `DX6`** — ported verbatim,
  spike-confirmed (`DX7-0` Tests E/F) still real and correct through the flattened device API.
- **Hardware T&L is real in this environment's Wine (`EnumDevices7` reports a genuine
  `IID_IDirect3DTnLHalDevice`) but deliberately NOT adopted** — this whole backend family submits
  CPU-pre-transformed-and-lit `D3DTLVERTEX` vertices by design (matching XNA/FNA's own CPU-side
  `BasicEffect` math exactly), the opposite of what real hardware T&L requires (submitting
  un-transformed vertices and delegating to the device's own fixed-function pipeline). Documented
  as an intentional architecture boundary, not a missed opportunity.
- **Cube environment maps deliberately deferred, not implemented** — same class of reason as
  `DX6`'s multitexture deferral: `D3DFVF_TLVERTEX` carries only a `D3DFVF_TEX1` (2D) texture
  coordinate; genuine cube-map sampling needs a 3-component texcoord (`D3DFVF_TEXCOORDSIZE3`) and a
  `DDSCAPS2_CUBEMAP` surface, both requiring a second vertex layout and extending the whole CPU
  pipeline — disproportionate scope, deferred with a documented reason.
- **DXTn compression stays out of scope** — same "no consumer" reasoning as `DX6`.
- **Everything else is a port of `DX6`'s own 2D+3D layers**, upgraded to the v7 interfaces and
  restructured for the flattened (no-viewport) device model: same CPU transform/clip pipeline,
  same `D3DFVF_TLVERTEX` submission, same Phase O9 CPU lighting, same real stencil operations.

---

## 1. What "DirectX 7" concretely means for this backend

| Layer | Symbol(s) used | Introduced in | Never used here |
|---|---|---|---|
| DirectDraw (2D) | `IDirectDraw7`, `IDirectDrawSurface7`, `DDSURFACEDESC2`, `DDSCAPS2` | **DX7 SDK — new interface, real** | `IDirectDraw4` (only used via the `DirectDrawCreate`/QI fallback path, not adopted) |
| DirectDraw creation | `DirectDrawCreateEx(nullptr, &dd7, IID_IDirectDraw7, nullptr)` | **DX7 SDK — new entry point, adopted** | The old `DirectDrawCreate`+`QueryInterface` chain (spike-confirmed to also work, but not used) |
| Direct3D device/object | `IDirect3D7`, `IDirect3DDevice7` | **DX7 SDK — new interface, real** | `IDirect3D3`/`IDirect3DDevice3` |
| Viewport | *(none — removed)* | **DX7 SDK — `IDirect3DViewport3` no longer exists** | `IDirect3DViewport3`, `CreateViewport`, `AddViewport`, `SetCurrentViewport`, `DeleteViewport`, `Clear2` |
| Per-frame clear | `IDirect3DDevice7::Clear(...)` (direct device method) | **DX7 SDK — new, real** | — |
| Viewport transform | `IDirect3DDevice7::SetViewport(D3DVIEWPORT7*)` (plain struct, no object) | **DX7 SDK — new, real** | `D3DVIEWPORT2`/`SetViewport2` |
| Device creation | `IDirect3D7::CreateDevice(rclsid, surface7, &device7)` — no trailing outer param | **DX7 SDK — new signature, real** | — |
| Texture binding | `IDirect3DDevice7::SetTexture(stage, surface7)` — direct surface bind | **DX7 SDK — new, real, simpler than every prior era** | `D3DRENDERSTATE_TEXTUREHANDLE`, `IDirect3DTexture2::GetHandle`, the `QueryInterface(IID_IDirect3DDevice2)` workaround |
| Stencil ops | `D3DRENDERSTATE_STENCILENABLE`/etc. | DX6 SDK (**unchanged from `DX6`**) | Two-sided stencil (D3D9-era) |
| Hardware T&L | `IID_IDirect3DTnLHalDevice` | DX7 SDK — real in this Wine, **deliberately not adopted** (see TL;DR) | — |
| Cube env maps | `DDSCAPS2_CUBEMAP`, `D3DFVF_TEXCOORDSIZE3` | DX7 SDK — real per the headers, **deliberately deferred** | — |
| Draw call | `IDirect3DDevice7::DrawPrimitive`/`DrawIndexedPrimitive` | DX5 SDK shape (**unchanged**) | — |

Confirmed present in this environment's MinGW-w64 headers before writing this plan: `IID_IDirectDraw7`,
`IDirectDrawSurface7` (same `Unlock(LPRECT)` shape as v4, adds `SetPriority`/`GetPriority`/`SetLOD`/
`GetLOD`, not used), `DirectDrawCreateEx`, `IID_IDirect3D7`, `IDirect3D7::CreateDevice` (3 params,
no outer), `IDirect3DDevice7::SetViewport`/`Clear`/`SetTexture`, `IID_IDirect3DRGBDevice`/
`IID_IDirect3DHALDevice`/`IID_IDirect3DTnLHalDevice`, `D3DDEVICEDESC7`.

---

## 2. Existence-gate spike — `DX7-0` (run before any backend code)

| # | Spike | What it proves | Result |
|---|---|---|---|
| `DX7-0a1` | `DirectDrawCreate` (v1) + `QueryInterface(IID_IDirectDraw7)` | Whether the OLD upgrade chain still works despite `ddraw.h`'s "cannot derive" note | ✅ Works |
| `DX7-0a2` | `DirectDrawCreateEx(nullptr, &dd7, IID_IDirectDraw7, nullptr)` | Whether the NEW DX7 entry point works | ✅ Works |
| `DX7-0b` | Combined depth+stencil Z-buffer surface (same shape `DX6-0` proved) via `IDirectDrawSurface7` | Whether the v7 surface layer supports the same stencil-capable Z-buffer | ✅ Works |
| `DX7-0c` | `IDirect3D7::CreateDevice(IID_IDirect3DRGBDevice, surface7, &device7)` — no trailing outer param | Whether device creation works with the new (shorter) signature | ✅ Works |
| `DX7-0d` | `device7->SetViewport(&D3DVIEWPORT7{...})` — no viewport object created at all | Whether the flattened, no-viewport-object model works | ✅ Works |
| `DX7-0e` | `device7->Clear(...)` called directly, then a stencil WRITE pass (left-half quad) | Whether device-direct `Clear` + stencil write survive the API flattening | ✅ Works — left half green, right half black |
| `DX7-0f` | A full-screen quad with `STENCILFUNC=EQUAL` on top of `DX7-0e`'s result | Whether stencil TEST still correctly gates per-pixel | ✅ Works — left half red, right half correctly rejected |
| `DX7-0g` | `device7->SetTexture(0, texSurface7)` — direct surface bind, no handle at all — then a textured quad | Whether the new no-handle texture-binding mechanism actually samples correctly | ✅ Works — exact solid-red readback |

Every readback matched its predicted value exactly, on the first run — no spike-authoring bugs at
all this round (Test G was appended after the first full pass already succeeded, specifically to
settle the texture-binding design decision empirically before writing any backend code). See
`dx7-spike/README.md` for the full record.

**Net effect**: the flattened, no-viewport, no-texture-handle DX7 object model is real and fully
confirmed. Phase S1 is unblocked.

---

## 3. Design decisions (recorded before implementation)

1. **Platform gate, same as `DX1`/`DX2`/`DX3`/`DX5`/`DX6`.** Same Windows-native-or-MinGW-cross-compile
   `FATAL_ERROR` gate.

2. **2D layer: port of `DX6`'s own, upgraded to `IDirectDraw7`/`IDirectDrawSurface7`.** `DDSURFACEDESC2`/
   `DDSCAPS2` are unchanged (already v4-shaped since `DX5`) — this is a type-rename port, not a
   structural change. `IDirectDrawSurface7::Unlock` keeps the same `Unlock(LPRECT)` shape v4 already
   had (no repeat of the `DX5-0` signature surprise).

3. **Top-level object creation uses `DirectDrawCreateEx`** (spike-confirmed, `DX7-0a2`), not the old
   `DirectDrawCreate`+`QueryInterface` chain every `DX2`..`DX6` backend used — the correct,
   recommended entry point for this DirectX era, and the one this plan's spike specifically
   validated.

4. **The whole viewport-object pattern is REMOVED.** No `CreateViewport`/`AddViewport`/
   `SetCurrentViewport`/`DeleteViewport` calls anywhere in this backend, and no `Dx7Viewport`-shaped
   member at all. `Create3DDevice` calls `device7->SetViewport(&D3DVIEWPORT7{...})` directly after
   device creation; every place `DX6` called `viewport3->Clear2(...)` now calls `device7->Clear(...)`
   directly. This simplifies device bring-up and every `Clear*` method (`Clear`/`ClearDepth`/
   `ClearStencil`/`ClearDepthAndStencil`/`ClearColorAndStencil`/`ClearColorDepthAndStencil`) — one
   fewer object to create, own, and tear down per device.

5. **`IDirect3D7::CreateDevice` call drops the trailing `nullptr` outer argument** `DX5`/`DX6`'s
   `IDirect3D3::CreateDevice` needed (spike-confirmed, `DX7-0c`) — same `IID_IDirect3DRGBDevice`
   choice as every prior backend in this family (see decision 10 for why not HAL/TnLHal).

6. **Texture binding is a direct `SetTexture(stage, surface7)` call, replacing the whole
   `Dx6ResolveTextureHandle`/`D3DRENDERSTATE_TEXTUREHANDLE`/`QueryInterface(IID_IDirect3DDevice2)`
   dance** `DX5`'s own design decision 6 needed and `DX6` inherited unchanged. Spike-confirmed
   (`DX7-0g`) this samples correctly with no texture-handle indirection at all — a genuine, positive
   simplification: no `IDirect3DTexture2` view is needed, no per-draw handle resolution helper
   exists in this backend at all. **Real finding, NOT anticipated by the `DX7-0` spike** (which
   tested texture *binding* but not texture *blending*): Wine's `IDirect3DDevice7::SetRenderState`
   outright rejects the legacy `D3DRENDERSTATE_TEXTUREMAPBLEND` render state every prior backend in
   this family used ("Render state 0x15 is invalid in d3d7"), discovered when the first full CTest
   pass threw at runtime. Fixed by using `SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE)`
   instead — the real DX7-native texture-stage mechanism, confirmed correct by the full `Dx7_Texture3D`
   pixel-verified sampling test (not just "doesn't throw").

7. **Stencil (`ApplyDepthStencilState`'s `D3DRENDERSTATE_STENCIL*` wiring, the combined
   depth+stencil Z-buffer surface format) is ported verbatim from `DX6`**, no changes — spike-confirmed
   (`DX7-0` Tests E/F) still real and correct through the flattened device API.

8. **CPU transform/clip pipeline, Phase O9 lighting, `D3DFVF_TLVERTEX` submission: verbatim port of
   `DX6`'s own** — no changes, since DX7's `DrawPrimitive`/`DrawIndexedPrimitive` keep the same FVF
   submission shape `DX5`/`DX6` already use.

9. **Hardware T&L (`IID_IDirect3DTnLHalDevice`, confirmed real via `EnumDevices7` in this Wine) is
   NOT adopted.** This backend family submits CPU-pre-transformed-and-lit `D3DTLVERTEX` vertices by
   design, matching XNA/FNA's own CPU-side `BasicEffect` math exactly and keeping behavior
   deterministic across environments/GPU drivers. Real hardware T&L requires submitting
   un-transformed, un-lit vertices (`D3DFVF_XYZ`+`NORMAL`, not `D3DFVF_XYZRHW`) and delegating to the
   device's own `SetTransform`/`SetLight`/`SetMaterial` fixed-function pipeline — architecturally the
   opposite of every backend in this family since `DX2-0`. Documented as an intentional boundary, not
   a missed capability: `IID_IDirect3DRGBDevice` (the same software device class every prior backend
   uses) is used here too, for consistency.

10. **Cube environment maps (`EnvironmentMapEffect`) stay accepted-and-ignored, with a specific,
    spike-informed reason recorded** (same class of reasoning as `DX6`'s multitexture deferral,
    decision 6 there): genuine cube-map sampling needs a 3-component texture coordinate
    (`D3DFVF_TEXCOORDSIZE3`) to index direction vectors and a `DDSCAPS2_CUBEMAP` surface — both
    requiring a second vertex layout and extending the whole CPU transform/clip pipeline to carry a
    3D texcoord channel, a disproportionate scope increase for this plan. `envMapEnabled=true`
    renders diffuse-texture-only (or untextured, matching the existing "degrade, don't throw"
    policy), not a throw.

11. **DXTn (S3TC/BC1-3) texture compression stays out of scope, not spiked** — same "no consumer, no
    scope" reasoning as `DX6` decision 7: CNA's content pipeline never feeds compressed texture data
    into any legacy DirectX backend's texture-upload path.

12. **Multitexture (`dualTexture`) stays accepted-and-ignored, same `DX6` reasoning** — `D3DFVF_TLVERTEX`
    still carries only one texture-coordinate pair; nothing about DX7 changes this constraint.

13. **32-bit surfaces only, `DirectSound`/`DirectInput`/`DirectPlay` out of scope, header
    containment, CMake integration shape** — identical to earlier plans, ported without change.

14. **CMake integration**: add `"DX7"` to `CNA_GRAPHICS_BACKEND`'s `STRINGS` property + a
    `CNA_BACKEND_DX7` option; a `cna_backend_graphics_dx7` static library target under
    `src/CNA/Internal/Backends/Dx7/`, same Windows-only `FATAL_ERROR` gate. Link set: `ddraw` +
    `dxguid` + `SDL3::SDL3` — identical to `DX1`/`DX2`/`DX3`/`DX5`/`DX6`.

15. **Testing: `scripts/run-wine-dx7.sh`**, modeled on `scripts/run-wine-dx6.sh` — same
    `~/.wine-cna-dx1` prefix.

16. **No execute-buffer code, no viewport object, no texture-handle code, proven by discipline.**
    The execute-buffer-discipline grep CTest (`DX7-1`) is adapted from `DX6`'s own but ALSO forbids
    `IDirectDraw4`/`IDirectDrawSurface4` (this backend is v7-only, not v4), `IDirect3D3`/
    `IDirect3DDevice3`/`IDirect3DViewport3` (this backend is v7-only), `D3DRENDERSTATE_TEXTUREHANDLE`
    (replaced by `SetTexture`), and `CreateViewport`/`AddViewport`/`SetCurrentViewport`/
    `DeleteViewport`/`Clear2` (the viewport object no longer exists at all) — a real, automated proof
    all four architectural changes are complete throughout, not just claimed. The existing `Dx6_Stencil`
    CTest ports to `Dx7_Stencil` unchanged in shape, proving stencil survives the API flattening.

---

## 4. Active execution order

1. **`DX7-0`** (existence-gate spike, §2) — done, unblocks everything else.
2. **Phase S1** (CMake integration + skeleton) — same shape as `DX6-1`..`DX6-6`.
3. **Phase S2** (2D layer: port from `Dx6GraphicsBackend`, upgraded to v7, decision 2).
4. **Phase S3** (3D device bring-up: `DirectDrawCreateEx`, `CreateDevice` signature change, viewport
   object REMOVED, decisions 3-5).
5. **Phase S4** (CPU transform/clip pipeline + Phase-O9 lighting: verbatim port, decision 8; texture
   binding simplified to direct `SetTexture`, decision 6).
6. **Phase S5** (`VertexBuffer`/`IndexBuffer` backends: verbatim port, upgraded to v7 surfaces).
7. **Phase S6** (state mapping: verbatim port including stencil, decision 7).
8. **Phase S7** (remaining `IGraphicsBackend` defaults: verbatim port).
9. **Phase S8** (tests + `docs/dx7-backend.md`, including the renamed `Dx7_Stencil` CTest).

For every task: build the affected target (`-DCNA_GRAPHICS_BACKEND=DX7`, MinGW cross-compile), run
the relevant CTest through `scripts/run-wine-dx7.sh`, and do not mark a task ✅ without both
actually passing.

---

## Phase S1 — CMake integration and skeleton

| # | Task | Status | Notes |
|---|---|---|---|
| `DX7-1` | Add `"DX7"` to `CNA_GRAPHICS_BACKEND`'s `STRINGS` property + `CNA_BACKEND_DX7` option; extend the Windows-only `FATAL_ERROR` gate; add the execute-buffer-discipline grep CTest (design decision 16) | ✅ | `cmake/BackendSelection.cmake` updated; `Dx7_ExecuteBufferDiscipline` passes (a real, multi-line-block-comment-aware `perl` stripper was needed here, unlike DX6's plain `sed`, since DX7's forbidden-symbol list overlaps with this backend's own explanatory doc comments). |
| `DX7-2` | `cna_backend_graphics_dx7` static library target; confirm minimal link set empirically | ✅ | `cmake/BackendLibraries.cmake`; same link set as `DX6` (`SDL3::SDL3 ddraw dxguid`) — `DirectDrawCreateEx`/`IDirectDraw7`/`IDirect3D7` all resolved with no new import library needed. |
| `DX7-3` | `include/CNA/Internal/Backends/Dx7/Dx7GraphicsBackend.hpp` + `src/CNA/Internal/Backends/Dx7/Dx7GraphicsBackend.cpp`: port of `Dx6GraphicsBackend`'s files | ✅ | Ported + hand-restructured to remove the viewport object, change `CreateDevice`'s signature, use `DirectDrawCreateEx`, and bind textures directly (see §3 decisions 3-6). |
| `DX7-4` | Factory dispatch for `DX7` in `CreateGraphicsBackend()` | ✅ | `include/CNA/GraphicsBackendType.hpp` enum/`#elif`/switch-case added. |
| `DX7-5` | `scripts/run-wine-dx7.sh` (design decision 15) | ✅ | Mirrors `run-wine-dx6.sh`, reuses `~/.wine-cna-dx1`. |
| `DX7-6` | Confirm `CnaTests`/the new MinGW test binaries link cleanly against the new backend target under cross-compilation; proactively add a `Dx7` entry to every `CNA_BACKEND_*` registry file (the `DX30-83`/`DX5-6`/`DX6-6` lesson) | ✅ | Added to `GraphicsBackendCompileDefinitionTests.cpp`, `GraphicsDeviceValidationTests.cpp`, `GraphicsDeviceCapabilityTests.cpp`, `cmake/UnitTests.cmake`, `CMakeLists.txt` up front — `CNA`/`CnaTests.exe` both linked and ran clean on the first attempt (zero backend-code compile errors at all). |

## Phase S2 — 2D layer (port from `Dx6GraphicsBackend`, upgraded to `IDirectDraw7`/`IDirectDrawSurface7`)

| # | Task | Status | Notes |
|---|---|---|---|
| `DX7-10`..`DX7-15` | Device/window bring-up (via `DirectDrawCreateEx`), texture/render-target backends, `SpriteBatch`, `SpriteFont`, logical transform, renamed 2D CTests | ✅ | Port of `DX6-10`..`DX6-15`, type-renamed to v7. `Dx7_Smoke`, `Dx7_TextureRenderTarget`, `Dx7_SpriteBatch`, `Dx7_Blend`, `Dx7_AddressMode`, `Dx7_SpriteFont`, `Dx7_LogicalTransform` all pass. |

## Phase S3 — Direct3D v7 device bring-up, flattened (no viewport object)

| # | Task | Status | Notes |
|---|---|---|---|
| `DX7-20` | `DirectDrawCreateEx(nullptr, &dd7, IID_IDirectDraw7, nullptr)` top-level object creation (decision 3) | ✅ | `Dx7_Smoke`/`Dx7_Device3DSmoke` confirm real device bring-up. |
| `DX7-21` | `IDirect3D7::CreateDevice(IID_IDirect3DRGBDevice, surface7, &device7)` — no outer param (decision 5) | ✅ | Compiled and ran clean first try. |
| `DX7-22` | Remove the viewport object entirely: `device7->SetViewport(&D3DVIEWPORT7{...})` replaces `CreateViewport`/`AddViewport`/`SetCurrentViewport` (decision 4) | ✅ | `Dx7_ExecuteBufferDiscipline` proves no viewport symbol survives; `Dx7_ColoredPrimitives`/`Dx7_Clipping` confirm rendering is still correct with no viewport object. |
| `DX7-23` | Z-buffer surface (combined depth+stencil, unchanged from `DX6-26`) | ✅ | `Dx7_Stencil` proves it end-to-end. |
| `DX7-24` | `Clear`/`ClearDepth`/`ClearStencil`/`ClearDepthAndStencil`/`ClearColorAndStencil`/`ClearColorDepthAndStencil` all route through `device7->Clear(...)` directly (decision 4) | ✅ | `Dx7_ZTest`/`Dx7_Stencil` both pass. |

## Phase S4 — CPU transform/clip pipeline + `DrawPrimitive` submission (verbatim port, texture binding simplified)

| # | Task | Status | Notes |
|---|---|---|---|
| `DX7-30`..`DX7-39` | CPU clip/transform math, `D3DTLVERTEX`/`D3DFVF_TLVERTEX` submission, Phase O9 CPU lighting | ✅ | Verbatim port of `DX6-30`..`DX6-40`. `Dx7_ColoredPrimitives`, `Dx7_IndexedPrimitives`, `Dx7_Texture3D`, `Dx7_Clipping`, `Dx7_Lighting` all pass. |
| `DX7-40` | Texture binding replaced with direct `SetTexture(stage, surface7)` (decision 6), removing `Dx6ResolveTextureHandle`/`D3DRENDERSTATE_TEXTUREHANDLE`/the `QueryInterface(IID_IDirect3DDevice2)` workaround entirely | ✅ | `Dx7_Texture3D` pixel-verifies correct sampling. Real finding: the legacy `D3DRENDERSTATE_TEXTUREMAPBLEND` render state is REJECTED by DX7 (`Render state 0x15 is invalid in d3d7`) — fixed with `SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE)`, see decision 6's own note. |

## Phase S5 — `VertexBuffer`/`IndexBuffer` backends (verbatim port, v7 surfaces)

| # | Task | Status | Notes |
|---|---|---|---|
| `DX7-50`, `DX7-51` | `Dx7VertexBufferBackend`/`Dx7IndexBufferBackend` | ✅ | Verbatim port of `DX6-50`/`DX6-51`. `Dx7_VertexIndexBuffer` passes. |

## Phase S6 — State mapping (verbatim port + stencil unchanged)

| # | Task | Status | Notes |
|---|---|---|---|
| `DX7-60`..`DX7-63` | `ApplyRasterizerState`/`ApplyBlendState`/`ApplySamplerState`, `SetDepthTestEnabled`/`SetBlendEnabled`/`SetDepthWriteEnabled` (verbatim port) | ✅ | Covered by the full 2D+3D CTest suite passing (blend/sampling/depth all exercised). |
| `DX7-64` | `ApplyDepthStencilState`'s stencil parameters (verbatim port of `DX6-64`'s `Dx6StencilOperationToD3D`-shaped helper, renamed `Dx7StencilOperationToD3D`) | ✅ | `WireFrame` reports `true` (inherited, `Dx7_WireframeAniso` passes); `AnisotropicFiltering` stays `false` (inherited). Real stencil write+test proven by `Dx7_Stencil` (4/4 checks pass) through `GraphicsDevice.DepthStencilState`, confirming stencil survives BOTH the viewport removal and the texture-binding change. |

## Phase S7 — Remaining `IGraphicsBackend` defaults (verbatim port)

| # | Task | Status | Notes |
|---|---|---|---|
| `DX7-70`..`DX7-76` | Occlusion query/volume-cube-textures/custom-effects/instancing/remaining clears/`DebugSimulateContextLoss` | ✅ | Verbatim port of `DX6-70`..`DX6-76`. `Dx7_RemainingDefaults` passes. |

## Phase S8 — Tests and documentation

| # | Task | Status | Notes |
|---|---|---|---|
| `DX7-80` | Full renamed 2D+3D CTest suite passing | ✅ | 19 ported from `DX6`, all green after fixing the `D3DRENDERSTATE_TEXTUREMAPBLEND` finding (decision 6). |
| `DX7-81` | Renamed `Dx7_Stencil` CTest: real stencil write-then-test through the full XNA public API, proving stencil survives the viewport/texture-handle removal | ✅ | `examples/dx7_stencil_test.cpp`; 4/4 checks pass (stamp left/right, test left/right). |
| `DX7-82` | `docs/dx7-backend.md`: mirror `docs/dx6-backend.md`'s structure | ✅ | Written after real verification, including the `D3DRENDERSTATE_TEXTUREMAPBLEND` finding. |
| `DX7-83` | Update `cmake/BackendSelection.cmake`'s `CNA_GRAPHICS_BACKEND` `STRINGS` docstring, `README.md`, and `plan_dxold.md`'s DX7 row | ✅ | All three updated. |
| `DX7-84` | Full `DX7`-labeled CTest suite regression + targeted cross-backend test re-run (mirroring `DX6-84`'s scope decision) | ✅ | 20/20 `DX7`-labeled CTests pass; `GraphicsBackendCompileDefinitionsTest`/`GraphicsDeviceValidationTest.SetRenderTargets_*`/`GraphicsDeviceCapabilityTest.*` all pass except the same 3 pre-existing `DX2-84` ungated-test-class failures every prior backend in this family also shows — zero new regressions. |

---

## Boundaries — explicitly out of scope for `DX7`

Identical to `plan_dx6.md`'s own Boundaries section, EXCEPT: the viewport object no longer exists
at all (removed, not merely unused); texture binding no longer uses the handle mechanism at all
(replaced, not merely simplified); hardware T&L is real in this environment but deliberately not
adopted (decision 9, an architectural boundary, not a missing capability); cube environment maps
are deferred with the specific `D3DFVF_TEXCOORDSIZE3`/`DDSCAPS2_CUBEMAP` reason recorded (decision
10); multitexture and DXTn stay out of scope for the same reasons `DX6` already recorded.

---

## See also

- `plan_dxold.md` — the roadmap this plan is row 7 of.
- `plan_dx6.md`, `docs/dx6-backend.md` — the backend this plan ports (2D layer type-renamed to v7;
  3D layer restructured to remove the viewport object and texture-handle mechanism; stencil
  unchanged).
- `dx7-spike/README.md` — the full `DX7-0` spike record.
- `docs/directx-legacy-backends-analysis.md` — the feasibility analysis; DX7's "hardware T&L, cube
  env maps — peak fixed-function" headline is per its own §3.1 table.
