# Direct3D 10 (`ID3D10Device`, real HLSL shaders) Graphics Backend — Implementation Plan

> **Status (2026-07-21): DONE.** `DX10-0` existence-gate spike complete; implementation phases
> T1-T8 all complete, built, and verified: 10/10 `D3D10`-labeled CTests pass, no new cross-backend
> regressions (`GraphicsDeviceCapabilityTest` shows only 2 pre-existing-style "out of this v1's
> scope" gaps -- `SupportsOcclusionQuery`/`SupportsCustomEffects` -- NOT `SupportsMultipleRenderTargets`,
> which D3D10 correctly closes unlike every `DX1`..`DX8` backend). Three real bugs found only by
> the full CTest suite, beyond the `DX10-0` spike's own findings, are recorded in
> `docs/d3d10-backend.md` §2: `D3D10_BLEND_DESC` shares one set of blend factors across all 8
> targets (unlike `D3D11_BLEND_DESC`'s per-target array -- a real API-shape difference, not a
> mistake); a winding-order/back-face-culling bug where a 180°-rotated sprite was silently culled
> (fixed with a dedicated cull-none rasterizer state for `SpriteBatch`'s own draws); and a
> `SpriteBatch::Begin()` ordering bug where this backend's own `Begin()` was resetting
> `transformMatrix_` to identity AFTER the caller's `SetTransformMatrix()` call had already set it
> (not present in `DX8`'s own `Begin()`, which this was modeled on -- an original mistake, not a
> port defect). Also documented: `RenderTargetUsage::DiscardContents` (XNA's own default) auto-
> clears ALL bound targets on an MRT bind based on only the FIRST target's own usage -- real,
> documented XNA/FNA behavior, not a backend bug, found while writing the MRT CTest.

---

## 0. TL;DR

Direct3D 10 (2006) removed the fixed-function pipeline **entirely** — no `SetVertexShader
(rawFvfValue)` trick like D3D8's, no `D3DFVF_*`, no fixed-function texture-stage states. Every draw
needs a real, compiled HLSL vertex+pixel shader pair (`vs_4_0`/`ps_4_0` — D3D10's own shader-model
ceiling, no SM5.0). This backend is architecturally much closer to this project's own `D3D11`/`D3D9`
backends (both already compile real HLSL via `D3DCompile`) than to the `DX1`..`DX8` CPU-transform
family — `plan_dxold.md`'s row 10.

Delivered via Wine's own builtin `d3d10.dll`/`d3d10_1.dll` (thin wrappers; DXVK 2.6.0 ships no
`d3d10.dll` at all) forwarding to **DXVK's real `d3d10core.dll`** (native override) + DXVK's `dxgi.dll`
(native override — D3D10, unlike D3D8, genuinely needs a real DXGI swap chain).

## 1. What "Direct3D 10" concretely means for this backend

1. **No fixed function at all.** Every 2D (`SpriteBatch`) and 3D (`DrawColoredPrimitives`) draw is a
   real HLSL shader pair, following this project's own `D3DCompile`-based precedent
   (`D3D9EffectBackend.cpp`/`D3D11EffectBackend.cpp`), targeting `vs_4_0`/`ps_4_0`.
2. **Real hardware blending, depth/stencil, rasterizer — as real state OBJECTS**
   (`ID3D10BlendState`/`ID3D10DepthStencilState`/`ID3D10RasterizerState`), not per-call render-state
   setters like `DX1`..`DX8`'s `D3DRENDERSTATE_*`/`D3DRS_*` — matching `D3D11`'s own object-based
   state model, a real architectural difference from the whole `DX1`..`DX8` family.
3. **Real MRT support** (`ID3D10Device::OMSetRenderTargets` takes an array) — unlike every backend in
   the `DX1`..`DX8` family (DirectDraw/early-Direct3D has exactly one active render target).
4. **Real readback**: `CreateTexture2D(D3D10_USAGE_STAGING)` + `CopyResource` + `Map`/`Unmap` — yet
   another distinct mechanism from `DX1`..`DX7`'s `Blt`, `DX8`'s `CreateImageSurface`+`CopyRects`, or
   `D3D9`'s `GetRenderTargetData`.
5. **No logical-resolution render target / letterboxing** — unlike `DX8`'s own addition (forced by
   D3D8 having no scaled-blit primitive at all), `D3D10` doesn't need one: the swap chain always
   matches the real window size, matching `D3D11GraphicsBackend`'s own simpler, already-established
   convention (`SetVirtualResolution` is inert bookkeeping, `GetViewportSize` returns the real
   window's own pixel size via SDL).

## 2. Existence-gate spike (`DX10-0`) — see `dx10-spike/README.md` for the full record

| # | Spike | Result |
|---|---|---|
| `DX10-0a` | `D3D10CreateDeviceAndSwapChain` via Wine's `d3d10.dll` + DXVK's `d3d10core.dll`/`dxgi.dll` | ✅ Works |
| `DX10-0b` | `CreateRenderTargetView`/`CreateDepthStencilView`/`ClearRenderTargetView` | ✅ Works |
| `DX10-0c` | `D3DCompile(..., "vs_4_0"/"ps_4_0", ...)` | ✅ Works |
| `DX10-0d` | Real shader draw: `CreateVertexShader`/`CreatePixelShader`/`CreateInputLayout`/`CreateBuffer`+`Draw(6,0)` | ✅ Works |
| `DX10-0e` | `CreateTexture2D`+`CreateShaderResourceView`+`CreateSamplerState`, sampled in the pixel shader | ✅ Works — pixel-perfect readback |
| `DX10-0f` | `CreateTexture2D(D3D10_USAGE_STAGING)`+`CopyResource`+`Texture2D::Map` | ✅ Works — exact match |

Two real environment bugs found and fixed, both fully documented in `dx10-spike/README.md`:
- The `~/.wine-cna-d3d10` prefix (copied from the already-Vulkan-proven `~/.wine-cna-d3d11`)
  inherited BROKEN/dangling `d3d10.dll`/`d3d10_1.dll` symlinks (leftover from an older DXVK version
  that used to ship those directly — the current DXVK 2.6.0 package only ships `d3d10core.dll`).
  Fixed: removed those two overrides entirely, restored Wine's own real builtin files.
- `IDXGISwapChain::Present()` crashes (a real DXVK divide-by-zero bug in `readMonitorEdidFromKey`,
  confirmed independent of GPU and independent of whether a valid EDID is injected into the
  registry) under Xvfb `:99`. Fixed by running against `DISPLAY=:0` (the real desktop) instead —
  matches an already-established precedent for this project's own D3D11/D3D12 Wine+DXVK testing.

## 3. Design decisions (recorded before implementation)

1. **Wine prefix**: dedicated `~/.wine-cna-d3d10` (copied from `~/.wine-cna-d3d11`'s already-proven
   Vulkan state), `d3d10core`+`dxgi`+`d3d9`+`d3d11` overridden to native/DXVK, `d3d10`/`d3d10_1` left
   as Wine's own real builtin (verified not dangling symlinks).
2. **Run all tests against `DISPLAY=:0`**, not Xvfb `:99` — the only way found to avoid DXVK's own
   dxgi `Present()`-path divide-by-zero bug (§2). `scripts/run-wine-d3d10.sh` defaults to this.
3. **MVP scope, explicitly bounded** (mirroring `DX1`'s own "baseline first, lighting/richness as a
   later phase" precedent, and `DX8`'s own "fixed-function only" scope-bounding):
   - `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives` (**required** by `IGraphicsBackend`):
     real `vs_4_0`/`ps_4_0` shader pair — `world*view*projection` transform + vertex-color
     passthrough (matching `BasicEffect(VertexColorEnabled=true)`, no lighting).
   - `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx`: real unlit `BasicEffect` shader routes for
     `VertexPositionColor` and `VertexPositionTexture`, including diffuse tint and native draw/index
     offsets. Other stock-effect configurations are rejected explicitly rather than losing their
     effect state in the interface's colored fallback.
   - `CreateEffectBackend` (custom `ShaderEffect`): **not implemented in this v1**, same boundary
     `DX1`..`DX8` all share (throws via the base default).
   - `CreateOcclusionQuery`/`CreateTexture3D`/`CreateTextureCube`/`CreateRenderTargetCube`/
     `DrawInstancedPrimitivesEx`: **not implemented in this v1** — all have safe base-class
     defaults (return `nullptr`/throw), matching every backend in this family at its own MVP stage.
   - `SpriteBatch`: real GPU-quad rendering via `vs_4_0`/`ps_4_0` (position+color+texture,
     `DX10-0d`/`0e`'s own proven shader), matching `D3D9`/`D3D11`'s own real-shader 2D compositor
     pattern (not `DX1`..`DX8`'s CPU `Blt`-family approach, and not `DX8`'s own from-scratch
     GPU-quad-via-fixed-function approach either — this one genuinely uses shaders throughout).
4. **State objects, not per-call render states**: `ID3D10BlendState`/`ID3D10DepthStencilState`/
   `ID3D10RasterizerState`/`ID3D10SamplerState`, created (and cached, keyed by the XNA-side state
   description) once per distinct state combination, bound via `OMSetBlendState`/
   `OMSetDepthStencilState`/`RSSetState`/`PSSetSamplers` — matches `ID3D10Device`'s own object model
   (there is no equivalent of `D3D8`/`DX1`..`DX7`'s `SetRenderState(D3DRS_*, value)` at all).
5. **`SupportsCapability`**: `ThreeD`/`DepthStencilBuffer`/`WireFrame`/`AnisotropicFiltering`/
   **`MultipleRenderTargets`** all report `true` (D3D10 has real `OMSetRenderTargets(count>1,...)`
   support, a genuine, real difference from every `DX1`..`DX8` backend). `OcclusionQuery`/
   `CustomEffects` report `false` (out of this v1's scope, §3 above).
6. **No logical-resolution render target** (design decision in §1.5) — `Present()` is a direct
   `swapChain_->Present(syncInterval, flags)` call, matching `D3D11GraphicsBackend`'s own.
7. **Readback**: `CreateTexture2D(D3D10_USAGE_STAGING)` + `CopyResource` + `Map(D3D10_MAP_READ)` +
   `Unmap` (`DX10-0f`, spike-confirmed).

## 4. Active execution order

For every task: build the affected target (`-DCNA_GRAPHICS_BACKEND=D3D10`, MinGW cross-compile), run
the relevant CTest through `scripts/run-wine-d3d10.sh` (against `DISPLAY=:0`), and do not mark a task
✅ without both actually passing.

---

## Phase T1 — CMake integration and skeleton

| # | Task | Status | Notes |
|---|---|---|---|
| `D3D10-1` | Add `"D3D10"` to `CNA_GRAPHICS_BACKEND`'s `STRINGS`; `CNA_BACKEND_D3D10` option; Windows-only `FATAL_ERROR` gate | ✅ | |
| `D3D10-2` | `cna_backend_graphics_d3d10` target linking `d3d10`/`dxgi`/`d3dcompiler` (real mingw import libs — no DXVK `.dll.a` hack needed, unlike DX8) | ✅ | |
| `D3D10-3` | `include/CNA/Internal/Backends/D3D10/D3D10GraphicsBackend.hpp` + `.cpp` | ✅ | |
| `D3D10-4` | Factory dispatch for `D3D10` in `CreateGraphicsBackend()` | ✅ | |
| `D3D10-5` | `scripts/run-wine-d3d10.sh` (decision 2); one-time Wine-prefix setup (decision 1) | ✅ | |
| `D3D10-6` | `Dx8`-style registry additions: `GraphicsBackendType.hpp`, `GraphicsBackendCompileDefinitionTests.cpp`, `GraphicsDeviceValidationTests.cpp`/`GraphicsDeviceCapabilityTests.cpp` gate conditions | ✅ | |

## Phase T2 — Device/window bring-up

| # | Task | Status | Notes |
|---|---|---|---|
| `D3D10-10` | `D3D10CreateDeviceAndSwapChain` against a real `HWND`, `DXGI_SWAP_CHAIN_DESC` construction | ✅ | |
| `D3D10-11` | `Clear`/`Present`/`GetViewportSize`/`ReadBackbuffer` (staging texture + `Map`, decision 7) | ✅ | |
| `D3D10-12` | `SetVirtualResolution`/`SetPresentationMode` (inert bookkeeping, decision 6) | ✅ | |

## Phase T3 — 2D layer (real GPU-quad `SpriteBatch`, decision 3)

| # | Task | Status | Notes |
|---|---|---|---|
| `D3D10-20` | `D3D10TextureBackend`/`D3D10RenderTargetBackend`: real `ID3D10Texture2D`/views | ✅ | |
| `D3D10-21` | `D3D10SpriteBatchBackend`: real `vs_4_0`/`ps_4_0` shader, real blend state, rotation/scale | ✅ | |
| `D3D10-22` | `SpriteFont`/`DrawString` CTest (reuses the same GPU-quad path) | ✅ | |

## Phase T4 — 3D device state (Z-buffer/stencil/MRT, decisions 4/5)

| # | Task | Status | Notes |
|---|---|---|---|
| `D3D10-30` | `ClearColorAndDepth`/etc. via the real device-direct `Clear*` calls | ✅ | |
| `D3D10-31` | `SupportsDepthStencil()`/`SupportsCapability()` (decision 5 — `MultipleRenderTargets=true`) | ✅ | |
| `D3D10-32` | `SetRenderTargets(count>1)` real MRT support (genuinely new vs. `DX1`..`DX8`) | ✅ | |

## Phase T5 — Real shader-based 3D submission (decision 3)

| # | Task | Status | Notes |
|---|---|---|---|
| `D3D10-40` | `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives`: real `vs_4_0`/`ps_4_0` shader (transform + vertex-color passthrough) | ✅ | |
| `D3D10-41` | Near/far-plane clipping is real GPU hardware clipping (no CPU Sutherland-Hodgman needed, unlike `DX1`..`DX8`) | ✅ | Verified via `D3D10_ClippingBehindCamera` CTest. |
| `D3D10-42` | Initial `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx` fallback | superseded | `D3D10-85` replaces the state-dropping fallback with real unlit textured/colored routes. |

## Phase T6 — `VertexBuffer`/`IndexBuffer` backends

| # | Task | Status | Notes |
|---|---|---|---|
| `D3D10-50`, `D3D10-51` | `D3D10VertexBufferBackend`/`D3D10IndexBufferBackend`: real `ID3D10Buffer` | ✅ | |

## Phase T7 — State objects (decision 4)

| # | Task | Status | Notes |
|---|---|---|---|
| `D3D10-60`..`D3D10-63` | `ApplyBlendState`/`ApplyDepthStencilState`/`ApplyRasterizerState`/`ApplySamplerState`: real cached state objects | ✅ | |
| `D3D10-70`..`D3D10-76` | Occlusion query/3D-textures/cube-textures/custom-effects/instancing: left at `IGraphicsBackend`'s own safe defaults (decision 3) | ✅ | Documented v1 boundary, matching this family's own established pattern. |

## Phase T8 — Tests and documentation

| # | Task | Status | Notes |
|---|---|---|---|
| `D3D10-80` | Full 2D+3D CTest suite passing | ✅ | |
| `D3D10-81` | Reuse the shared, backend-agnostic EasyGL-authored blend/depth-stencil/rasterizer-state tests, mirroring `D3D11`'s own precedent | ✅ | |
| `D3D10-82` | `docs/d3d10-backend.md` | ✅ | |
| `D3D10-83` | Update `cmake/BackendSelection.cmake` docstring, `README.md`, `plan_dxold.md`'s DX10 row | ✅ | |
| `D3D10-84` | Full `D3D10`-labeled CTest suite regression + targeted cross-backend test re-run | ✅ | |
| `D3D10-85` | Restore the unlit textured `BasicEffect` `VertexPositionTexture` route; preserve draw/index offsets and reject unsupported effect combinations instead of silently dropping texture state | ✅ | Pixel oracle: `DirectX10_Textured3D`; cna-template cube verified in Wine. |

---

## Boundaries — explicitly out of scope for this v1

Real, hardware/API-level DirectX-10-era boundaries: none — D3D10 is a full modern API. Boundaries in
THIS v1 are scope decisions (§3.3), not hardware limits: lighting and the stock-effect variants
beyond the unlit textured/vertex-color `BasicEffect` subsets, custom `ShaderEffect`, occlusion query, 3D/cube
textures, instancing. All have safe base-class defaults and are documented, real future-work items,
not silently dropped.

## See also

- `dx10-spike/README.md` — the full `DX10-0` spike record, including the two environment-bug
  investigations.
- `plan_dxold.md` — the roadmap this backend is row 10 of.
- `docs/d3d10-backend.md` — completeness table (once written).
