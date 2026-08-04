# DirectX 6 (DirectDraw v4 + Direct3D v3, real stencil) Graphics Backend — Implementation Plan

> **Status (2026-07-21): `DX6-0` spike AND implementation phases R1-R8 all complete and verified.**
> 20/20 `DX6`-labeled CTests pass (19 ported + the new `Dx6_Stencil` real stencil write-then-test
> proof), first try after fixing the CMake registry wiring. Targeted cross-backend regression
> matches `DX3`/`DX5`'s own baseline exactly (same 3 pre-existing, already-documented `DX2-84`
> ungated-test-class failures, zero new ones). See §2 for the spike result and the phase tables
> below for per-task detail.

---

## 0. TL;DR

- **New backend: `CNA_GRAPHICS_BACKEND=DX6`.** No temporary-naming concern.
- **DX6 introduces NO new COM interface revision at all.** Unlike `DX2`→`DX3`→`DX5`'s
  progression (each a genuine new `IDirect3D`/`IDirect3DDevice`/`IDirectDraw` interface), DX6
  (1998) reuses `IDirect3D3`/`IDirect3DDevice3`/`IDirect3DViewport3`/`IDirectDraw4` verbatim —
  confirmed by inspecting the real MinGW headers: no `IDirect3D4`/`IDirect3DDevice4` exists. DX6's
  entire delta vs. `DX5` is new render states/capabilities on the *same* interface: stencil ops,
  multitexturing, and DXTn compression.
- **Real deliverable: stencil buffer operations**, resolving a boundary `DX2`/`DX3`/`DX5` have
  all explicitly documented as "no real stencil buffer exists at this DirectX era (DX6+)." A new
  existence-gate spike (`DX6-0`, `dx6-spike/`) confirmed real stencil WRITE (via
  `D3DRENDERSTATE_STENCILPASS=REPLACE`) and real stencil TEST (via `D3DRENDERSTATE_STENCILFUNC`)
  both work correctly against a combined depth+stencil Z-buffer surface
  (`DDPF_ZBUFFER|DDPF_STENCILBUFFER`, 32-bit total: 24 depth + 8 stencil, a D24S8-equivalent
  shape) — every predicted pixel matched exactly, no spike-authoring bugs this time.
- **Multitexture deliberately deferred, not implemented.** Investigated (real
  `SetTextureStageState`/`D3DTSS_*` render states exist and are real per the headers), but
  `D3DTLVERTEX`/`D3DFVF_TLVERTEX` (the vertex format this whole backend family's CPU
  transform/clip pipeline is built around) carries only ONE texture-coordinate pair
  (`D3DFVF_TEX1`). Genuine two-independent-UV multitexture (what `DualTextureEffect` actually
  needs) would require a second vertex layout (`D3DFVF_TEX2`) and extending the whole CPU
  transform/clip pipeline to carry a second UV channel — a disproportionate scope increase for
  this plan. Deferred with a clear, documented reason, matching this backend family's own
  established "accept and ignore, document why" discipline — not silently dropped, not
  half-implemented.
- **Everything else is a mechanical port of `DX5`'s own 2D+3D layers**, unchanged: same
  `IDirectDraw4`/`IDirectDrawSurface4`/`DDSURFACEDESC2`/`DDSCAPS2` 2D layer, same
  `IDirect3D3`/`IDirect3DDevice3`/`IDirect3DViewport3` 3D layer, same `D3DFVF_TLVERTEX`
  submission, same Phase O9 CPU lighting, same real `Clear2`-based depth clear.

---

## 1. What "DirectX 6" concretely means for this backend

| Layer | Symbol(s) used | Introduced in | Never used here |
|---|---|---|---|
| DirectDraw (2D) | `IDirectDraw4`, `IDirectDrawSurface4`, `DDSURFACEDESC2`, `DDSCAPS2` | DX5 SDK (**unchanged from `DX5`**) | `IDirectDraw7`+ |
| Direct3D device/object | `IDirect3D3`, `IDirect3DDevice3` | DX5 SDK (**unchanged from `DX5`** — DX6 adds no new interface) | `IDirect3D7`+ |
| Viewport | `IDirect3DViewport3` | DX5 SDK (**unchanged**) | — |
| Texture | `IDirect3DTexture2` | DX3 SDK (**unchanged**) | — |
| Stencil ops | `D3DRENDERSTATE_STENCILENABLE`/`STENCILFUNC`/`STENCILFAIL`/`STENCILZFAIL`/`STENCILPASS`/`STENCILREF`/`STENCILMASK`/`STENCILWRITEMASK` | **DX6 SDK — new, real, wired here** | Two-sided stencil (`D3DRS_CCW_STENCIL*`, a much later D3D9-era addition — doesn't exist at this DirectX era) |
| Stencil Z-buffer format | `DDPF_ZBUFFER\|DDPF_STENCILBUFFER`, `dwZBufferBitDepth=32`, `dwStencilBitDepth=8` | DX6 SDK — new, real, wired here | — |
| Multitexture | `SetTextureStageState`/`D3DTSS_COLOROP`/etc. | DX6 SDK — real per the headers, **deliberately deferred** (decision 6) | `D3DFVF_TEX2`-based vertex submission |
| Draw call | `IDirect3DDevice3::DrawPrimitive`/`DrawIndexedPrimitive` | DX5 SDK (**unchanged**) | — |

Confirmed present in this environment's MinGW-w64 headers before writing this plan:
`D3DRENDERSTATE_STENCILENABLE`(52) through `STENCILWRITEMASK`(59), `D3DSTENCILOP` (`KEEP`/`ZERO`/
`REPLACE`/`INCRSAT`/`DECRSAT`/`INVERT`/`INCR`/`DECR`), `DDPF_STENCILBUFFER`(`0x4000`),
`DDPIXELFORMAT`'s `dwStencilBitDepth` field (aliased into the same union slot as `dwRBitMask`),
`D3DTEXTURESTAGESTATETYPE`/`D3DTSS_COLOROP` etc. (confirmed present but not used, decision 6).

---

## 2. Existence-gate spike — `DX6-0` (run before any backend code)

| # | Spike | What it proves | Result |
|---|---|---|---|
| `DX6-0a` | `CreateSurface` with `DDPF_ZBUFFER\|DDPF_STENCILBUFFER`, `dwZBufferBitDepth=32`, `dwStencilBitDepth=8`, then `AddAttachedSurface`+`CreateDevice` off it | Whether a stencil-capable Z-buffer surface and device are real, not stubs | ✅ Works |
| `DX6-0b` | `Clear2` with an explicit stencil value (0), then a left-half-only quad with `STENCILFUNC=ALWAYS`, `STENCILPASS=REPLACE`, `STENCILREF=1` | Whether a real stencil WRITE happens | ✅ Works — left half shows the drawn color, right half stays the Clear2 background |
| `DX6-0c` | A full-screen quad with `STENCILFUNC=EQUAL`, `STENCILREF=1` drawn on top of `DX6-0b`'s result | Whether a real stencil TEST correctly gates the draw per-pixel | ✅ Works — left half (stencil==1) shows the new color, right half (stencil==0) correctly rejects the draw |

Every readback matched its predicted value exactly on the first run — no spike-authoring bugs this
time (unlike `dx5-spike`'s own `Clear2`-rect and diagonal-seam gotchas, both correctly avoided here
having already been learned). See `dx6-spike/README.md` for the full record.

**Net effect**: DX6's stencil capability is real, confirmed, and ready to wire into
`ApplyDepthStencilState`. Phase R1 is unblocked.

---

## 3. Design decisions (recorded before implementation)

1. **Platform gate, same as `DX1`/`DX2`/`DX3`/`DX5`.** Same Windows-native-or-MinGW-cross-compile
   `FATAL_ERROR` gate.

2. **2D layer: verbatim port of `DX5`'s own** (`IDirectDraw4`/`IDirectDrawSurface4`/
   `DDSURFACEDESC2`/`DDSCAPS2` throughout) — no changes, since DX6 introduces nothing new here.

3. **3D device bring-up: verbatim port of `DX5`'s own** (`IDirect3D3`/`IDirect3DDevice3`/
   `IDirect3DViewport3`, `D3DFVF_TLVERTEX` submission, real `Clear2`-based depth clear, Phase O9 CPU
   lighting) — no changes, since DX6 reuses the exact same interfaces `DX5` already proved.

4. **Z-buffer surface upgrades to a combined depth+stencil format** (spike-confirmed, `DX6-0a`):
   `DDPF_ZBUFFER | DDPF_STENCILBUFFER`, `dwZBufferBitDepth = 32`, `dwStencilBitDepth = 8` (a
   D24S8-equivalent shape) — replacing `DX5`'s depth-only `dwZBufferBitDepth = 16` surface. Created
   once, attached to the shadow-backbuffer via `AddAttachedSurface`, same lifecycle as before.

5. **`ApplyDepthStencilState`'s stencil parameters are now real**, replacing `DX2`/`DX3`/`DX5`'s
   "accepted and ignored" boundary: `stencilEnable` → `D3DRENDERSTATE_STENCILENABLE`;
   `stencilFunc`/`referenceStencil`/`stencilMask` → `D3DRENDERSTATE_STENCILFUNC`/`STENCILREF`/
   `STENCILMASK`; `stencilFail`/`stencilDepthFail`/`stencilPass` → `D3DRENDERSTATE_STENCILFAIL`/
   `STENCILZFAIL`/`STENCILPASS` (mapped from CNA's `StencilOperation` enum via a new
   `Dx6StencilOperationToD3D` helper, matching the established `Dx6CompareFunctionToD3D`/etc.
   naming pattern); `stencilWriteMask` → `D3DRENDERSTATE_STENCILWRITEMASK`. `twoSidedStencilMode`/
   `ccwStencilFunc`/`ccwStencilPass`/`ccwStencilFail`/`ccwStencilDepthFail` remain
   accepted-and-ignored: two-sided stencil (separate front/back-face stencil ops) is a D3D9-era
   addition (`D3DRS_CCW_STENCIL*`) that doesn't exist at this DirectX era at all, confirmed by
   inspection — not a gap, a genuine unavailability.

6. **Multitexture (`dualTexture`) stays accepted-and-ignored, now with a specific, spike-informed
   reason recorded** (not just "not implemented yet"): real `SetTextureStageState`/`D3DTSS_*`
   render states exist and are usable per the headers, but `D3DTLVERTEX`/`D3DFVF_TLVERTEX` (the
   vertex format this whole backend family's CPU pipeline is built around) carries only one
   texture-coordinate pair. `DualTextureEffect` fundamentally needs two independent UV channels,
   which would require a second vertex layout (`D3DFVF_TEX2`) and extending the entire CPU
   transform/clip pipeline (`Dx6ClipVertex`, `Dx6BuildGenericClipVertex`, `Dx6ClipVertexToD3DTLVERTEX`,
   the whole stride-dispatch mechanism) to carry a second UV channel throughout — correctly scoped
   out as disproportionate for this plan, matching `docs/directx-legacy-backends-analysis.md`'s
   own "degrade, don't throw" policy: `dualTexture=true` renders diffuse-texture-only (texture0),
   not a throw.

7. **DXTn (S3TC/BC1-3) texture compression stays out of scope**, not spiked: CNA's own
   `Texture2D`/content pipeline doesn't currently feed compressed texture data into any legacy
   DirectX backend's `CreateTexture`/`UpdatePixels` path (all existing backends in this family
   receive already-decompressed RGBA8 pixel data), so there is no real caller-facing gap this
   plan needs to close — same "no consumer, no scope" reasoning already used for
   `GetAvailableVidMem` (`DX3` design decision 4) and real vertex buffers (`DX5` design decision 7).

8. **Lighting/fog/envMap/skinning/MRT/instancing/occlusion-query/volume-cube-textures/
   custom-effects: identical boundary to `DX5`/`DX3`/`DX2` post-Phase-O9.** Only stencil moves
   from "accepted-and-ignored" to "real" in this phase.

9. **32-bit surfaces only, `DirectSound`/`DirectInput`/`DirectPlay` out of scope, header
   containment, CMake integration shape** — identical to earlier plans, ported without change.

10. **CMake integration**: add `"DX6"` to `CNA_GRAPHICS_BACKEND`'s `STRINGS` property + a
    `CNA_BACKEND_DX6` option; a `cna_backend_graphics_dx6` static library target under
    `src/CNA/Internal/Backends/Dx6/`, same Windows-only `FATAL_ERROR` gate. Link set: `ddraw` +
    `dxguid` + `SDL3::SDL3` — identical to `DX1`/`DX2`/`DX3`/`DX5`.

11. **Testing: `scripts/run-wine-dx6.sh`**, modeled on `scripts/run-wine-dx5.sh` — same
    `~/.wine-cna-dx1` prefix.

12. **No execute-buffer code, and genuinely wired stencil, proven by discipline + a new dedicated
    stencil CTest.** The execute-buffer-discipline grep CTest (`DX6-1`) is identical in shape to
    `DX5`'s own (v4-only DirectDraw, `IDirect3D3`/`IDirect3DDevice3` only, no `D3DVT_*` enum
    usage). A new `Dx6_Stencil` CTest proves the real write-then-test behavior end-to-end through
    the full XNA public API (`GraphicsDevice.DepthStencilState`), mirroring the spike's own Test
    B/C shape.

---

## 4. Active execution order

1. **`DX6-0`** (existence-gate spike, §2) — done, unblocks everything else.
2. **Phase R1** (CMake integration + skeleton) — same shape as `DX5-1`..`DX5-6`.
3. **Phase R2** (2D layer: verbatim port from `Dx5GraphicsBackend`, decision 2).
4. **Phase R3** (3D device bring-up: verbatim port, PLUS the stencil-capable Z-buffer surface,
   decision 4).
5. **Phase R4** (CPU transform/clip pipeline + Phase-O9 lighting: verbatim port, decision 3).
6. **Phase R5** (`VertexBuffer`/`IndexBuffer` backends: verbatim port).
7. **Phase R6** (state mapping: verbatim port, PLUS real stencil wiring in
   `ApplyDepthStencilState`, decision 5).
8. **Phase R7** (remaining `IGraphicsBackend` defaults: verbatim port).
9. **Phase R8** (tests + `docs/dx6-backend.md`, including the new `Dx6_Stencil` CTest).

For every task: build the affected target (`-DCNA_GRAPHICS_BACKEND=DX6`, MinGW cross-compile), run
the relevant CTest through `scripts/run-wine-dx6.sh`, and do not mark a task ✅ without both
actually passing.

---

## Phase R1 — CMake integration and skeleton

| # | Task | Status | Notes |
|---|---|---|---|
| `DX6-1` | Add `"DX6"` to `CNA_GRAPHICS_BACKEND`'s `STRINGS` property + `CNA_BACKEND_DX6` option; extend the Windows-only `FATAL_ERROR` gate; add the execute-buffer-discipline grep CTest (design decision 12) | ✅ | `cmake/BackendSelection.cmake` updated; `Dx6_ExecuteBufferDiscipline` passes. |
| `DX6-2` | `cna_backend_graphics_dx6` static library target; confirm minimal link set empirically | ✅ | `cmake/BackendLibraries.cmake`; same link set as `DX5` (`SDL3::SDL3 ddraw dxguid`), confirmed by a clean link. |
| `DX6-3` | `include/CNA/Internal/Backends/Dx6/Dx6GraphicsBackend.hpp` + `src/CNA/Internal/Backends/Dx6/Dx6GraphicsBackend.cpp`: port of `Dx5GraphicsBackend`'s files | ✅ | Ported + hand-edited for real stencil (see §3 decisions 4/5). |
| `DX6-4` | Factory dispatch for `DX6` in `CreateGraphicsBackend()` | ✅ | `include/CNA/GraphicsBackendType.hpp` enum/`#elif`/switch-case added. |
| `DX6-5` | `scripts/run-wine-dx6.sh` (design decision 11) | ✅ | Mirrors `run-wine-dx5.sh`, reuses `~/.wine-cna-dx1`. |
| `DX6-6` | Confirm `CnaTests`/the new MinGW test binaries link cleanly against the new backend target under cross-compilation; proactively add a `Dx6` entry to every `CNA_BACKEND_*` registry file (the `DX30-83`/`DX5-6` lesson) | ✅ | Added to `GraphicsBackendCompileDefinitionTests.cpp`, `GraphicsDeviceValidationTests.cpp`, `GraphicsDeviceCapabilityTests.cpp`, `cmake/UnitTests.cmake`, `CMakeLists.txt` up front this time — `CnaTests.exe` linked and ran clean on the first attempt. |

## Phase R2 — 2D layer (verbatim port from `Dx5GraphicsBackend`)

| # | Task | Status | Notes |
|---|---|---|---|
| `DX6-10`..`DX6-15` | Device/window bring-up, texture/render-target backends, `SpriteBatch`, `SpriteFont`, logical transform, renamed 2D CTests | ✅ | Verbatim port of `DX5-10`..`DX5-15`. `Dx6_Smoke`, `Dx6_TextureRenderTarget`, `Dx6_SpriteBatch`, `Dx6_Blend`, `Dx6_AddressMode`, `Dx6_SpriteFont`, `Dx6_LogicalTransform` all pass. |

## Phase R3 — Direct3D v3 device bring-up + stencil-capable Z-buffer

| # | Task | Status | Notes |
|---|---|---|---|
| `DX6-20`..`DX6-25` | `IDirect3D3`/`IDirect3DDevice3`/viewport bring-up (verbatim port of `DX5-20`..`DX5-25`) | ✅ | `Dx6_Device3DSmoke`, `Dx6_GraphicsCapability` pass. |
| `DX6-26` | Z-buffer surface upgraded to combined depth+stencil (decision 4): `DDPF_ZBUFFER\|DDPF_STENCILBUFFER`, `dwZBufferBitDepth=32`, `dwStencilBitDepth=8` | ✅ | Implemented exactly as specified; `Dx6_Stencil` proves it end-to-end. |
| `DX6-27` | Real `Clear2`-based `ClearDepth` (verbatim port of `DX5-26`) | ✅ | `Dx6_ZTest` passes (order-independent depth occlusion, both draw orders). |

## Phase R4 — CPU transform/clip pipeline + `DrawPrimitive` submission (verbatim port)

| # | Task | Status | Notes |
|---|---|---|---|
| `DX6-30`..`DX6-40` | CPU clip/transform math, `D3DTLVERTEX`/`D3DFVF_TLVERTEX` submission, Phase O9 CPU lighting | ✅ | Verbatim port of `DX5-30`..`DX5-40`. `Dx6_ColoredPrimitives`, `Dx6_IndexedPrimitives`, `Dx6_Texture3D`, `Dx6_Clipping`, `Dx6_Lighting` all pass. |

## Phase R5 — `VertexBuffer`/`IndexBuffer` backends (verbatim port)

| # | Task | Status | Notes |
|---|---|---|---|
| `DX6-50`, `DX6-51` | `Dx6VertexBufferBackend`/`Dx6IndexBufferBackend` | ✅ | Verbatim port of `DX5-50`/`DX5-51`. `Dx6_VertexIndexBuffer` passes. |

## Phase R6 — State mapping (verbatim port + real stencil)

| # | Task | Status | Notes |
|---|---|---|---|
| `DX6-60`..`DX6-63` | `ApplyRasterizerState`/`ApplyBlendState`/`ApplySamplerState`, `SetDepthTestEnabled`/`SetBlendEnabled`/`SetDepthWriteEnabled` (verbatim port) | ✅ | Covered by the full 2D+3D CTest suite passing (blend/sampling/depth all exercised). |
| `DX6-64` | `ApplyDepthStencilState`'s stencil parameters wired to real `D3DRENDERSTATE_STENCIL*` render states (decision 5), new `Dx6StencilOperationToD3D` helper | ✅ | `WireFrame` reports `true` (inherited, `Dx6_WireframeAniso` passes); `AnisotropicFiltering` stays `false` (inherited, empirically confirmed absent). Real stencil write+test proven by `Dx6_Stencil` (4/4 checks pass) through `GraphicsDevice.DepthStencilState`, not just the raw spike. |

## Phase R7 — Remaining `IGraphicsBackend` defaults (verbatim port)

| # | Task | Status | Notes |
|---|---|---|---|
| `DX6-70`..`DX6-76` | Occlusion query/volume-cube-textures/custom-effects/instancing/remaining clears/`DebugSimulateContextLoss` | ✅ | Verbatim port of `DX5-70`..`DX5-76`. `Dx6_RemainingDefaults` passes. |

## Phase R8 — Tests and documentation

| # | Task | Status | Notes |
|---|---|---|---|
| `DX6-80` | Full renamed 2D+3D CTest suite passing | ✅ | 19 ported from `DX5`, all green first try after CMake wiring fixes. |
| `DX6-81` | New `Dx6_Stencil` CTest: real stencil write-then-test through the full XNA public API (`GraphicsDevice.DepthStencilState`), mirroring the spike's Test B/C shape | ✅ | `examples/dx6_stencil_test.cpp`; 4/4 checks pass (stamp left/right, test left/right). |
| `DX6-82` | `docs/dx6-backend.md`: mirror `docs/dx5-backend.md`'s structure | ✅ | Written after real verification, §3/§4 below. |
| `DX6-83` | Update `CMakeLists.txt`'s `CNA_GRAPHICS_BACKEND` STRINGS docstring, `README.md`, and `plan_dxold.md`'s DX6 row | ✅ | `cmake/BackendSelection.cmake`'s STRINGS/docstring (the file actually holding it), `README.md`, `plan_dxold.md` all updated. |
| `DX6-84` | Full `DX6`-labeled CTest suite regression + targeted cross-backend test re-run (mirroring `DX5-83`'s scope decision) | ✅ | 20/20 `DX6`-labeled CTests pass; `GraphicsBackendCompileDefinitionsTest`/`GraphicsDeviceValidationTest.SetRenderTargets_*`/`GraphicsDeviceCapabilityTest.*` all pass except the same 3 pre-existing `DX2-84` ungated-test-class failures every prior backend in this family also shows — zero new regressions. |

---

## Boundaries — explicitly out of scope for `DX6`

Identical to `plan_dx5.md`'s own Boundaries section, EXCEPT stencil operations, which are now
real (decision 5) instead of accepted-and-ignored. Additionally: multitexture (`dualTexture`)
stays accepted-and-ignored with the specific `D3DFVF_TEX2` reason recorded (decision 6); DXTn
compression stays out of scope (decision 7, no real caller-facing gap); two-sided stencil doesn't
exist at this DirectX era (decision 5).

---

## See also

- `plan_dxold.md` — the roadmap this plan is row 6 of.
- `plan_dx5.md`, `docs/dx5-backend.md` — the backend this plan ports (2D+3D layers unchanged; only
  stencil is new).
- `dx6-spike/README.md` — the full `DX6-0` spike record.
- `docs/directx-legacy-backends-analysis.md` — the feasibility analysis; DX6's "multitexture/DXTn/
  stencil" delta is per its own §3.1 table.
