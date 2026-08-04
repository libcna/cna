# DirectX 3 (real, DirectDraw v2 + Direct3D v2 DrawPrimitive) Graphics Backend — Implementation Plan

> **Naming: rename executed 2026-08-04 (owner instruction, dxold integration).** This backend
> is CNA's real, Route-B "DirectX 3" implementation per `plan_dxold.md`'s roadmap (row 3). It was
> originally built and shipped under the temporary CMake name `DX30`, because the `DX3` name was
> owned by the `../free-direct`-backed 2D backend at the time. That backend has since been renamed
> to `FREEDIRECT` (`plan_freedirect.md`, `docs/freedirect-backend.md`), and this backend now owns
> its final identity: `CNA_GRAPHICS_BACKEND=DX3`, `CNA_BACKEND_DX3`, `Dx3GraphicsBackend`,
> `src/CNA/Internal/Backends/Dx3/`, `plan_dx3.md`, `docs/dx3-backend.md`. The historical `DX30-*`
> task IDs below are kept verbatim as provenance and are not renumbered.
>
> **Status (2026-07-21): all phases complete and verified.** 19/19 `DX30`-labeled CTests pass,
> all green on the first run after the mechanical port (no transcription errors) — see the phase
> tables below and `docs/dx3-backend.md` for the full result.
>
> Owner's instruction (translated from Czech): *"Go do DX3. The rename of the current DX3 to
> free-direct will happen later — for now, name it DX30."*

---

## 0. TL;DR

- **`DX30` is architecturally `DX2` plus one mechanical upgrade**: the `IDirectDraw` object
  obtained from `DirectDrawCreate` is immediately upgraded via
  `dd->QueryInterface(IID_IDirectDraw2, &dd2)`, and every subsequent DirectDraw call is issued
  through the resulting `LPDIRECTDRAW2` instead of the v1 `LPDIRECTDRAW`. Per `plan_dxold.md`'s own
  roadmap table, "DX3 (real)" = *"DirectDraw v2 (`IDirectDraw2`, adds refresh-rate to
  `SetDisplayMode`) + execute-buffer Direct3D, matured."* The execute-buffer half of that
  description is **already known non-functional** in this environment (`dx2-spike/README.md`'s
  14-variant finding) — `DX2` already resolved the 3D question by using the DX3-SDK's own
  `IDirect3D2`/`IDirect3DDevice2::DrawPrimitive` immediate-mode API instead, which genuinely works.
  Since that API is *already* a DX3-SDK addition, **`DX30`'s 3D layer needs no new spike or new
  code at all** — it is `DX2`'s already-proven 3D layer, ported forward unchanged (including Phase
  O9's CPU lighting and `WireFrame` support).
- **New existence-gate spike (`DX30-0`, `dx3-spike/`)**: confirms `IDirectDraw2` is a real,
  fully-functional drop-in for `IDirectDraw` v1 in this Wine environment — `QueryInterface`
  succeeds, a genuinely v2-exclusive method (`GetAvailableVidMem`) returns real (non-stub) data,
  and `CreateSurface`/`Lock`/`Unlock` work identically through the v2 pointer. `SetDisplayMode`'s
  wider (5-argument, refresh-rate-adding) signature returns `E_NOTIMPL` in windowed `DDSCL_NORMAL`
  mode — expected, not a gap: `DX1-0`/`DX2-0` already established this backend family never calls
  `SetDisplayMode` at all (windowed mode never needs it), so the extra parameter is dead code here
  regardless, matching real DirectDraw's own documented "only in exclusive-fullscreen mode"
  behavior. See `dx3-spike/README.md` for the full record.
- **2D layer: mechanical port of `Dx2GraphicsBackend`**, upgraded to the `IDirectDraw2` object.
  No other 2D-layer change — `SpriteBatch`/`SpriteFont`/blend modes/texture/render-target code is
  otherwise byte-identical.
- **3D layer: verbatim port of `Dx2GraphicsBackend`'s 3D layer**, including Phase O9's CPU-side
  BasicEffect lighting (ambient + up to 3 directional lights, Blinn-Phong specular via real
  `D3DRENDERSTATE_SPECULARENABLE`) and confirmed-real `WireFrame` support. No re-verification of
  already-proven 3D math — same `IDirect3D2`/`IDirect3DDevice2`/`IDirect3DViewport2`/
  `IDirect3DTexture2` chain, obtained via `QueryInterface` off the (now v2) DirectDraw object
  exactly as `DX2` already does.
- **`GetAvailableVidMem` (a real, confirmed-working v2-exclusive capability) is NOT exposed**
  through `IGraphicsBackend` — that interface has no existing video-memory-query capability slot,
  and adding one is out of this plan's scope (no CNA consumer needs it yet). Documented as
  available-but-unused, not silently dropped.

---

## 1. What "real DirectX 3" concretely means for this backend

| Layer | Symbol(s) used | Introduced in | Never used here |
|---|---|---|---|
| DirectDraw (2D) | `IDirectDraw2`, `IDirectDrawSurface`, `DDSURFACEDESC` | DX3 SDK (object); `IDirectDrawSurface`/`DDSURFACEDESC` unchanged from v1 | `IDirectDraw` (v1, only used transiently to `QueryInterface` up to v2)/`IDirectDraw3`+, `IDirectDrawSurface2`+/`DDSURFACEDESC2` |
| Direct3D device/object | `IDirect3D2`, `IDirect3DDevice2` | DX3 SDK | `IDirect3D`/`IDirect3DDevice` (execute-buffer only, proven broken here — same finding `DX2-0` already made), `IDirect3D3`+/`IDirect3DDevice3`+ |
| Viewport | `IDirect3DViewport2` | DX3 SDK | `IDirect3DViewport` (v1)/`IDirect3DViewport3`+ |
| Texture | `IDirect3DTexture2` | DX3 SDK | `IDirect3DTexture` (v1)/`IDirect3DTexture3`+ (doesn't exist) |
| Vertex format | `D3DTLVERTEX` | DX2 SDK (`d3dtypes.h`) | `D3DVERTEX`/`D3DLVERTEX` |
| Draw call | `IDirect3DDevice2::DrawPrimitive`/`DrawIndexedPrimitive` | DX3 SDK | `IDirect3DDevice::Execute` (execute buffers, proven broken) |

Every row above is **identical to `plan_dx2.md`'s own table** except the first (`IDirectDraw2`
instead of `IDirectDraw` v1) — this is the entire technical delta between "real DX2" and "real
DX3" in this environment, confirmed empirically by `DX30-0` rather than assumed.

---

## 2. Existence-gate spike — `DX30-0` (run before any backend code)

| # | Spike | What it proves | Result |
|---|---|---|---|
| `DX30-0a` | `dd->QueryInterface(IID_IDirectDraw2, &dd2)` on a real `DirectDrawCreate()`'d object (`dx3_spike1_ddraw2.cpp`, Test A) | Whether `IDirectDraw2` is reachable at all | ✅ **Works** |
| `DX30-0b` | `dd2->GetAvailableVidMem(&caps, &total, &free)` (Test B) | Whether v2 is a real, implemented interface, not an `E_NOTIMPL` stub | ✅ **Works** — real non-zero data returned |
| `DX30-0c` | `dd2->CreateSurface(...)` + `Lock()`/`Unlock()` through the v2 pointer (Test C) | Whether the v2 object is a fully-functional drop-in for every v1 call `DX1`/`DX2` already rely on | ✅ **Works** — identical to v1 |
| `DX30-0d` | `dd2->SetDisplayMode(640, 480, 32, 60, 0)` in windowed `DDSCL_NORMAL` mode (Test D) | Whether the new refresh-rate parameter is usable | ❌ `E_NOTIMPL` — expected, matches `DX1-0`/`DX2-0`'s own finding that windowed mode never calls `SetDisplayMode` at all |

**Net effect**: `DX30`'s 2D layer needs exactly one code change vs `DX2` (upgrade the DirectDraw
object to v2 right after creation); its 3D layer needs zero changes. Phase O1 is unblocked. See
`dx3-spike/README.md` for the full record.

---

## 3. Design decisions (recorded before implementation)

1. **Platform gate, same as `DX1`/`DX2`.** Same Windows-native-or-MinGW-cross-compile
   `FATAL_ERROR` gate.

2. **2D layer: port of `Dx2GraphicsBackend`, with the DirectDraw object upgraded to v2.**
   `Dx3GraphicsBackend`'s `Impl::dd` field becomes `LPDIRECTDRAW2` (was `LPDIRECTDRAW` in `DX2`);
   immediately after `DirectDrawCreate(nullptr, &ddV1, nullptr)`, `ddV1->QueryInterface(
   IID_IDirectDraw2, &dd)` upgrades it, then `ddV1->Release()` (the v1 pointer is no longer
   needed — `dd` is used for everything from that point on, exactly where `DX2`'s code used its
   own `dd`). Every other 2D method (`CreateSurface`, `SetCooperativeLevel`, shadow-backbuffer
   present, textures/render targets, `SpriteBatch` compositor, blend modes, `SpriteFont`,
   `TransformWindowToLogical`/`TransformLogicalToWindow`) is copied verbatim from `Dx2GraphicsBackend`
   — spike-confirmed (`DX30-0c`) that every v1 call used elsewhere in this codebase behaves
   identically through the v2 pointer, so no further per-call re-verification is needed.
   `SetDisplayMode` is still never called (matching `DX1`/`DX2`'s own decision) — the extra
   refresh-rate parameter stays unused, confirmed dead code for windowed mode (`DX30-0d`).

3. **3D layer: verbatim port of `Dx2GraphicsBackend`'s 3D layer, Phase O9 included.** Device/
   viewport/Z-buffer bring-up, `VertexBuffer`/`IndexBuffer` backends, the CPU transform/clip
   pipeline, `D3DTLVERTEX` submission via `DrawPrimitive`/`DrawIndexedPrimitive`, all per-draw state
   mapping, and Phase O9's CPU-side BasicEffect lighting (ambient + up to 3 directional lights,
   Blinn-Phong specular via real `D3DRENDERSTATE_SPECULARENABLE`) are copied unchanged from
   `Dx2GraphicsBackend.cpp` — only the enclosing namespace/class name changes (`Dx2`→`Dx3`). No
   re-derivation, no re-verification of already-proven math; only the CTests are re-run against the
   new backend binary to confirm the port itself introduced no transcription errors.

4. **`GetAvailableVidMem` is available-but-unused.** Confirmed real and functional
   (`DX30-0b`), but `IGraphicsBackend` has no existing video-memory-query capability slot and no
   current CNA consumer needs one — exposing it would be scope creep beyond what "DX3 real" needs.
   Documented here, not silently dropped, and not wired to any public API.

5. **Lighting/fog/multitexture/envMap/skinning/stencil/MRT/instancing/occlusion-query/volume-cube
   textures/custom-effects: identical boundary to `DX2` post-Phase-O9.** Fixed-function lighting
   is real for the two normal-bearing vertex strides (32/52); fog, multitexture, environment
   mapping, and skinning remain accepted-and-ignored; stencil/MRT/instancing/occlusion
   query/volume-cube textures/custom effects are genuinely unavailable at this DirectX era, same as
   `DX2`.

6. **32-bit surfaces only, `DirectSound`/`DirectInput`/`DirectPlay` out of scope, header
   containment, CMake integration shape** — identical to `plan_dx1.md`/`plan_dx2.md`'s equivalent
   decisions, ported without change.

7. **CMake integration**: add `"DX3"` to `CNA_GRAPHICS_BACKEND`'s `STRINGS` property + a
   `CNA_BACKEND_DX3` option (temporary name, see the notice at the top of this plan); a
   `cna_backend_graphics_dx3` static library target under `src/CNA/Internal/Backends/Dx3/`, same
   Windows-only `FATAL_ERROR` gate. Link set: `ddraw` + `dxguid` + `SDL3::SDL3` — identical to
   `DX1`/`DX2`.

8. **Testing: `scripts/run-wine-dx3.sh`**, modeled on `scripts/run-wine-dx2.sh` — same
   `~/.wine-cna-dx1` prefix is reusable (confirmed by this plan's own spike, which ran against it).

9. **No execute-buffer code anywhere in this backend.** Same grep-based discipline CTest as
   `DX1-1`/`DX2-1` (`DX30-1`), asserting `src/CNA/Internal/Backends/Dx3/` never references
   `IDirect3DDevice::Execute`/`D3DEXECUTEBUFFERDESC`/`IDirect3DExecuteBuffer`/`D3DINSTRUCTION`/
   `D3DOP_`.

10. **Full CTest suite is a straight rename/port of `DX2`'s own 19 `DX2`-labeled CTests to
    `DX30`-labeled equivalents.** No separate "prove the `IDirectDraw2` upgrade happened" CTest is
    needed on top of that: `Dx3GraphicsBackend`'s constructor unconditionally throws if
    `QueryInterface(IID_IDirectDraw2)` fails (decision 2), so every one of these 19 tests already
    only ever runs against a genuine v2 object — their mere success already is the proof.
    `dx3_smoke_test.cpp`'s own header comment states this explicitly rather than adding a
    redundant dedicated test file just to re-demonstrate it.

---

## 4. Active execution order

1. **`DX30-0`** (existence-gate spike, §2) — done, unblocks everything else.
2. **Phase P1** (CMake integration + skeleton) — same shape as `DX2-1`..`DX2-6`.
3. **Phase P2** (2D layer: port from `Dx2GraphicsBackend`, upgraded to `IDirectDraw2`, decision 2).
4. **Phase P3** (3D device bring-up: verbatim port from `Dx2GraphicsBackend`, decision 3).
5. **Phase P4** (CPU transform/clip pipeline + Phase-O9 lighting: verbatim port).
6. **Phase P5** (`VertexBuffer`/`IndexBuffer` backends: verbatim port).
7. **Phase P6** (state mapping: verbatim port).
8. **Phase P7** (remaining `IGraphicsBackend` defaults: verbatim port).
9. **Phase P8** (tests + `docs/dx3-backend.md`).

For every task: build the affected target (`-DCNA_GRAPHICS_BACKEND=DX3`, MinGW cross-compile),
run the relevant CTest through `scripts/run-wine-dx3.sh`, and do not mark a task ✅ without both
actually passing.

---

## Phase P1 — CMake integration and skeleton

| # | Task | Status | Notes |
|---|---|---|---|
| `DX30-1` | Add `"DX3"` to `CNA_GRAPHICS_BACKEND`'s `STRINGS` property + `CNA_BACKEND_DX3` option; extend the Windows-only `FATAL_ERROR` gate; add the execute-buffer-discipline grep CTest (design decision 9) | ✅ | `Dx3_ExecuteBufferDiscipline` passing. |
| `DX30-2` | `cna_backend_graphics_dx3` static library target; confirm minimal link set empirically | ✅ | Same link set as `DX1`/`DX2` (`SDL3::SDL3 ddraw dxguid`). |
| `DX30-3` | `include/CNA/Internal/Backends/Dx3/Dx3GraphicsBackend.hpp` + `src/CNA/Internal/Backends/Dx3/Dx3GraphicsBackend.cpp`: mechanical port of `Dx2GraphicsBackend`'s files, `Dx2`→`Dx3` renamed throughout | ✅ | Every `IGraphicsBackend` method identical to `DX2`'s post-Phase-O9 state; only the DirectDraw object type/upgrade step (decision 2) differs. |
| `DX30-4` | Factory dispatch for `DX30` in `CreateGraphicsBackend()` | ✅ | |
| `DX30-5` | `scripts/run-wine-dx3.sh` (design decision 8) | ✅ | Reuses `~/.wine-cna-dx1` prefix. |
| `DX30-6` | Confirm `CnaTests`/the new MinGW test binaries link cleanly against the new backend target under cross-compilation | ✅ | `cmake-build-dx3` configures/builds clean (MinGW cross, Release, ccache). |

## Phase P2 — 2D layer (port from `Dx2GraphicsBackend`, upgraded to `IDirectDraw2`)

| # | Task | Status | Notes |
|---|---|---|---|
| `DX30-10` | Device/window bring-up: `DirectDrawCreate` → `QueryInterface(IID_IDirectDraw2)` upgrade (decision 2) → `SetCooperativeLevel`/primary surface/shadow-backbuffer/`Clear`/`Present` | ✅ | |
| `DX30-11` | Texture/render-target backends | ✅ | Port unchanged from `DX2`. |
| `DX30-12` | `SpriteBatch` CPU compositor, all rotation/scale/flip/blend/sampling paths | ✅ | Port unchanged from `DX2`. |
| `DX30-13` | `SpriteFont` | ✅ | Port unchanged from `DX2`. |
| `DX30-14` | `TransformWindowToLogical`/`TransformLogicalToWindow`/letterbox present math | ✅ | Port unchanged from `DX2`. |
| `DX30-15` | Renamed 2D CTests passing, pixel-verified | ✅ | Every test's own success is the proof `IDirectDraw2` was genuinely used (decision 10) — no separate CTest needed. |

## Phase P3 — Direct3D v2 device bring-up (verbatim port)

| # | Task | Status | Notes |
|---|---|---|---|
| `DX30-20`..`DX30-26` | `IDirect3D2`/`IDirect3DDevice2`/viewport/Z-buffer bring-up, `ClearColorAndDepth`/etc. wiring, `Dx3_Device3DSmoke` CTest | ✅ | Verbatim port of `DX2-20`..`DX2-26`; obtained via `QueryInterface(IID_IDirect3D2)` off the now-v2 `dd` object instead of the v1 one — spike-confirmed (`DX30-0c`) this works identically. |

## Phase P4 — CPU transform/clip pipeline + `DrawPrimitive` submission (verbatim port, incl. Phase O9)

| # | Task | Status | Notes |
|---|---|---|---|
| `DX30-30`..`DX30-39` | CPU clip/transform math, `D3DTLVERTEX` packing, `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives`/`DrawPrimitivesEx`/`DrawIndexedPrimitivesEx`, depth-test/texture/clipping CTests | ✅ | Verbatim port of `DX2-30`..`DX2-39`. |
| `DX30-40` | CPU-side BasicEffect lighting (Phase O9's `Dx2ComputeVertexLighting`, specular via `D3DRENDERSTATE_SPECULARENABLE`) | ✅ | Verbatim port of `DX2-91`..`DX2-96`'s implementation. `Dx3_Lighting` CTest (4/4, same checks as `Dx2_Lighting`). |

## Phase P5 — `VertexBuffer`/`IndexBuffer` backends (verbatim port)

| # | Task | Status | Notes |
|---|---|---|---|
| `DX30-50`, `DX30-51` | `Dx3VertexBufferBackend`/`Dx3IndexBufferBackend`, `SetData`/`GetData` round-trip test | ✅ | Verbatim port of `DX2-40`..`DX2-42`. |

## Phase P6 — State mapping (verbatim port)

| # | Task | Status | Notes |
|---|---|---|---|
| `DX30-60`..`DX30-64` | `ApplyDepthStencilState`/`ApplyRasterizerState`/`ApplyBlendState`/`ApplySamplerState`, `SetDepthTestEnabled`/`SetBlendEnabled`/`SetDepthWriteEnabled` | ✅ | Verbatim port of `DX2-50`..`DX2-54`. `WireFrame` reports `true` (inherited from `DX2`'s Phase O9 finding — same software RGB device, same confirmed-real `D3DFILL_WIREFRAME` distinctness); `AnisotropicFiltering` stays `false` (same confirmed-absent finding). |

## Phase P7 — Remaining `IGraphicsBackend` defaults (verbatim port)

| # | Task | Status | Notes |
|---|---|---|---|
| `DX30-70`..`DX30-76` | Occlusion query/volume-cube-textures/custom-effects/instancing/remaining clears/`DebugSimulateContextLoss` | ✅ | Verbatim port of `DX2-60`..`DX2-66`. |

## Phase P8 — Tests and documentation

| # | Task | Status | Notes |
|---|---|---|---|
| `DX30-80` | Full renamed 2D+3D CTest suite passing | ✅ | **19/19 `DX30`-labeled CTests**, all ported from `DX2`'s own 19, all green on the first run (no transcription errors found in the mechanical port). |
| `DX30-81` | `docs/dx3-backend.md`: mirror `docs/dx2-backend.md`'s structure | ✅ | |
| `DX30-82` | Update `CMakeLists.txt`'s `CNA_GRAPHICS_BACKEND` STRINGS docstring, `README.md`, and `plan_dxold.md`'s DX3(real) row | ✅ | |
| `DX30-83` | Full `DX30`-labeled CTest suite regression + targeted cross-backend test re-run (mirroring `DX2-98`'s scope decision — a full multi-hour `CnaTests` regression is out of proportion for a mechanical port; the cross-backend test classes any new backend addition could plausibly affect are re-run directly) | ✅ | **19/19 `DX30`-labeled CTests pass** via `ctest -L DX30 -j2`. Targeted `CnaTests` filter run (`GraphicsBackendCompileDefinitionsTest.*:GraphicsDeviceValidationTest.SetRenderTargets_*:GraphicsDeviceCapabilityTest.*`) confirms `ExactlyOneGraphicsBackendIsSelected` passes (the new `GraphicsBackendType.hpp`/`GraphicsBackendCompileDefinitionTests.cpp` entries), `SetRenderTargets_FourTargets_DoesNotThrow`/`FiveTargets_Throws` pass (the new `GraphicsDeviceValidationTests.cpp` entry), `DoesNotSupportWireFrame` passes (the new `GraphicsDeviceCapabilityTests.cpp` entry) — and the same 3 pre-existing, already-documented ungated-test-class failures `DX2-84` found (`SupportsMultipleRenderTargets`/`SupportsOcclusionQuery`/`SupportsCustomEffects`) are unchanged, confirming zero new regressions. Also found and fixed one real, proactively-caught gap along the way: `include/CNA/GraphicsBackendType.hpp`'s own `CNA_BACKEND_*` → `GraphicsBackendType` mapping table had no `Dx3` entry at all (a full build under `-DCNA_GRAPHICS_BACKEND=DX30` failed to compile `#error`-gated code until fixed) — the same class of "new backend needs a matching entry in every `CNA_BACKEND_*` registry" gap `DX2-84` found for other files, caught here by an actual from-scratch build rather than discovered later. |

---

## Boundaries — explicitly out of scope for `DX30`

Identical to `plan_dx2.md`'s own Boundaries section (post-Phase-O9): execute-buffer Direct3D
(proven non-functional); fog (lighting itself is real for normal-bearing strides); multitexture/
env-mapping/skinning (accepted-and-ignored); stencil operations (no real stencil buffer until
DX6); `IDirectDraw3`+/`IDirectDrawSurface2`+ features; `DirectSound`/`DirectInput`/`DirectPlay`;
MRT/instancing/occlusion query/volume-cube textures/custom programmable effects; real Windows/
macOS hardware verification (MinGW cross-compile + Wine on Linux only, same caveat every Route-B
CNA backend carries). Additionally: `GetAvailableVidMem` is real and confirmed working but not
exposed through any public API (decision 4) — a deliberate scope boundary, not an oversight.

---

## See also

- `plan_dxold.md` — the roadmap this plan is row 3 of; see its own "DX3 naming transition" section
  for why this backend is temporarily named `DX30`.
- `plan_dx2.md`, `docs/dx2-backend.md` — the backend this plan ports verbatim (2D layer + 3D layer
  + Phase O9 lighting/`WireFrame`), including the existence-gate-spike discipline `DX30-0` follows.
- `dx3-spike/README.md` — the full `DX30-0` spike record.
- `plan_freedirect.md`, `docs/freedirect-backend.md` — the existing, shipping `../free-direct`-backed `DX3`
  backend, whose name this backend will inherit once the still-pending rename task runs.
