# D3D10 (real `ID3D10Device`, real HLSL shaders, DXVK-delivered) Renderer — Completeness Status

`D3D10` is architecturally very different from every renderer in the `DIRECTX1`..`DIRECTX8` family: Direct3D 10
(2006) removed the fixed-function pipeline **entirely** — there is no `SetVertexShader(rawFvfValue)`
trick like D3D8's, no `D3DFVF_*` bitmask, no fixed-function texture-stage states at all. Every draw
requires a real, compiled HLSL vertex+pixel shader pair (`vs_4_0`/`ps_4_0` — D3D10's own shader-model
ceiling). This renderer is architecturally much closer to this project's own `D3D9`/`D3D11` renderers
(both already compile real HLSL via `D3DCompile`) than to the `DIRECTX1`..`DIRECTX8` CPU-transform family.
Delivered via **Wine's own builtin `d3d10.dll`/`d3d10_1.dll`** (thin wrappers; DXVK 2.6.0 ships no
`d3d10.dll` at all) forwarding to **DXVK's real `d3d10core.dll`** (native override) + DXVK's
`dxgi.dll` (native override — D3D10, unlike D3D8, genuinely needs a real DXGI swap chain).

**This document covers a genuinely new v1 renderer, not a port.** Scope is deliberately bounded
(plans/plan_d3d10.md design decision 3, mirroring `DIRECTX1`'s own "baseline first, richness later" precedent
and `DIRECTX8`'s own "fixed-function only" scope-bounding) — see §3 below for exactly what's in and out.

---

## 1. Existence-gate spike (`DX10-0`) — see `dx10-spike/README.md` for the full record

| # | Spike | Result |
|---|---|---|
| `DX10-0a` | `D3D10CreateDeviceAndSwapChain` via Wine's `d3d10.dll` + DXVK's `d3d10core.dll`/`dxgi.dll` | ✅ Works |
| `DX10-0b` | `CreateRenderTargetView`/`CreateDepthStencilView`/`ClearRenderTargetView` | ✅ Works |
| `DX10-0c` | `D3DCompile(..., "vs_4_0"/"ps_4_0", ...)` | ✅ Works |
| `DX10-0d` | Real shader draw: `CreateVertexShader`/`CreatePixelShader`/`CreateInputLayout`/`CreateBuffer`+`Draw(6,0)` | ✅ Works |
| `DX10-0e` | `CreateTexture2D`+`CreateShaderResourceView`+`CreateSamplerState`, sampled in the pixel shader | ✅ Works — pixel-perfect readback |
| `DX10-0f` | `CreateTexture2D(D3D10_USAGE_STAGING)`+`CopyResource`+`Texture2D::Map` | ✅ Works — exact match |

Two real environment bugs found and fixed (`dx10-spike/README.md`): the shared `~/.wine-cna-d3d11`-
copied prefix inherited BROKEN, dangling `d3d10.dll`/`d3d10_1.dll` symlinks (DXVK 2.6.0 dropped
those, only ships `d3d10core.dll` now) — fixed by restoring Wine's own real builtin files; and
`IDXGISwapChain::Present()` crashes under Xvfb (a real DXVK `readMonitorEdidFromKey` bug, confirmed
independent of GPU and independent of EDID validity) — fixed by running against `DISPLAY=:0` (the
real desktop), matching an already-established precedent for this project's own D3D11/D3D12
Wine+DXVK testing.

## 2. Real architectural deltas vs. `DIRECTX1`..`DIRECTX8`

- **No fixed function at all.** Every 2D (`SpriteBatch`) and 3D (`DrawColoredPrimitives`) draw is a
  real HLSL shader pair (`vs_4_0`/`ps_4_0`), following this project's own `D3DCompile`-based
  precedent.
- **Real state OBJECTS, not per-call render states.** `ID3D10BlendState`/`ID3D10DepthStencilState`/
  `ID3D10RasterizerState`/`ID3D10SamplerState` are created fresh per `ApplyXState` call and bound
  via `OMSetBlendState`/`OMSetDepthStencilState`/`RSSetState`/`PSSetSamplers` — there is no
  equivalent of `DIRECTX1`..`DIRECTX8`'s `SetRenderState(D3DRS_xxx, value)` at all.
- **`D3D10_BLEND_DESC` shares ONE set of blend factors/op across all 8 render targets** — unlike
  `D3D11_BLEND_DESC`'s per-target `RenderTarget[8]` array. Only `BlendEnable`/
  `RenderTargetWriteMask` are per-target in D3D10 (`D3D10.1`'s `D3D10_BLEND_DESC1` added true
  per-target blend factors, not used here) — a real, load-bearing API shape difference found while
  writing `ApplyBlendState` (an initial per-target-array draft failed to compile).
- **Real MRT support** (`OMSetRenderTargets` takes an array) — a genuine, real difference from every
  `DIRECTX1`..`DIRECTX8` renderer (DirectDraw/early-Direct3D has exactly one active render target).
- **Real readback**: `CreateTexture2D(D3D10_USAGE_STAGING)` + `CopyResource` + `Map(D3D10_MAP_READ)`
  — yet another distinct mechanism from `DIRECTX1`..`DIRECTX7`'s `Blt`, `DIRECTX8`'s
  `CreateImageSurface`+`CopyRects`, or `D3D9`'s `GetRenderTargetData`.
- **No logical-resolution render target / letterboxing** — unlike `DIRECTX8`'s own addition (forced by
  D3D8 having no scaled-blit primitive at all), `D3D10` doesn't need one: the swap chain always
  matches the real window size, matching `DirectX11Renderer`'s own simpler, already-established
  convention (`SetVirtualResolution` is inert bookkeeping).
- **`SpriteBatch` is real GPU-quad rendering through a real `vs_4_0`/`ps_4_0` shader** (position +
  color + texture, an orthographic screen-to-clip transform) — no half-texel correction needed
  (unlike `DIRECTX8`/`D3D9`'s own pre-D3D10 pixel-center convention): D3D10 places texel/pixel centers at
  pixel CENTERS like every modern API, matching `D3D11`'s own convention exactly.
- **Winding order flips at 180° rotation — a real bug found via CTest.** D3D10's default rasterizer
  state culls back-facing triangles (`D3D10_CULL_BACK`); a sprite rotated by ~180° flips its
  screen-space winding order and is silently culled unless the 2D layer explicitly disables culling
  for its own quads. Fixed: `D3D10SpriteBatchRenderer` binds a dedicated cull-none rasterizer state
  for the duration of its own `DrawIndexed` call, saving/restoring whatever state 3D draws set.
- **`SetTransformMatrix` real bug found via CTest**: the public `SpriteBatch::Begin(...)` call order
  is `renderer_->SetTransformMatrix(transformMatrix_)` THEN `renderer_->Begin()` — an initial draft
  of `D3D10SpriteBatchRenderer::Begin()` unconditionally reset `transformMatrix_` back to identity,
  silently discarding whatever the caller's own `Begin(..., transformMatrix)` overload had just set
  moments earlier. Fixed by removing that reset (matching `DirectX8SpriteBatchRenderer::Begin()`'s own
  precedent, which never had this bug).
- **`RenderTargetUsage::DiscardContents` (XNA's own default) auto-clears on every MRT bind — a real,
  documented XNA/FNA behavior, not a renderer bug**, found while writing the MRT CTest:
  `GraphicsDevice::SetRenderTargets` checks only the FIRST bound target's own usage, and if it's the
  default `DiscardContents`, clears ALL currently-bound targets (not just the first) on that bind.
  A render target that must survive being bound alongside others needs
  `RenderTargetUsage::PreserveContents` explicitly.

## 3. MVP scope — explicitly bounded (plans/plan_d3d10.md design decision 3)

- `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives` (**required** by `IGraphicsRenderer`): real
  `vs_4_0`/`ps_4_0` shader — `world*view*projection` transform + vertex-color passthrough (matches
  `BasicEffect(VertexColorEnabled=true)`, no lighting).
- `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx`: real unlit `BasicEffect` shader routes for
  `VertexPositionColor` and `VertexPositionTexture`. They apply diffuse tint, texture sampling,
  `vertexStart`, `startIndex`, `baseVertex`, and the captured stream offset. Unsupported effect
  variants throw instead of being silently rendered as unrelated flat vertex color.
- `CreateEffectRenderer` (custom `ShaderEffect`), occlusion query, 3D/cube textures, instancing: not
  implemented in this v1 — all have safe `IGraphicsRenderer` base-class defaults (return
  `nullptr`/throw), matching every renderer in this family at its own MVP stage.
- `SpriteBatch`: real GPU-quad rendering (see §2).

## 4. CTest results

**11/11 `D3D10`-labeled CTests pass**: `DirectX10_LegacyInterfaceDiscipline`, `DirectX10_Smoke`,
`DirectX10_Device3DSmoke`, `DirectX10_Textured3D`, `DirectX10_SpriteBatch`, `DirectX10_GraphicsCapability`,
`DirectX10_MultipleRenderTargets`, plus the shared, renderer-agnostic EasyGL-authored
`DirectX10_BlendState_Opaque`/`DirectX10_BlendState_AlphaBlend`/`DirectX10_DepthStencilState_StencilEnable`/
`DirectX10_RasterizerState_CullMode` (the same sources `D3D11`/`Vulkan` already reuse verbatim, testing
the vertex-color path and the dedicated texture×diffuse pixel oracle).

`SupportsCapability`: `ThreeD`/`DepthStencilBuffer`/`WireFrame`/`AnisotropicFiltering`/
`MultipleRenderTargets` all report `true` (`MultipleRenderTargets=true` is a genuine, real
difference from every `DIRECTX1`..`DIRECTX8` renderer). `OcclusionQuery`/`CustomEffects` report `false` (out
of this v1's scope, not a hardware limitation).

Cross-renderer regression: `GraphicsRendererCompileDefinitionsTest.ExactlyOneGraphicsRendererIsSelected`,
`GraphicsDeviceValidationTest.SetRenderTargets_*`, and `GraphicsDeviceCapabilityTest.*` — see
`plans/plan_d3d10.md`'s own task table for the run record (same pre-existing `DX2-84` ungated-test-class
gap every prior legacy-scope renderer in this family shows expected, not a new regression).

The legacy-interface-discipline check (`scripts/check-directx10-legacy-interface-discipline.sh`) forbids
`D3DFVF_*`/`D3DTLVERTEX`/`D3DVT_*`/`D3DRENDERSTATE_*`/`D3DRS_*`/`SetVertexShader(` anywhere in this
renderer's source — a real, automated proof this renderer genuinely has no fixed-function-era
leftovers, not just claimed.

## 5. Boundaries — explicitly out of scope

No hardware/API-level DirectX-10-era boundaries — D3D10 is a full modern API. Every boundary in this
v1 is a scope decision (§3), not a hardware limit, and all have safe base-class defaults.

## See also

- `plans/plan_d3d10.md` — this renderer's own implementation plan and design-decision record.
- `dx10-spike/README.md` — the full `DX10-0` spike record plus the environment-bug investigations.
- `docs/directx8-renderer.md` — the last renderer in the `DIRECTX1`..`DIRECTX8` legacy family; `D3D10` diverges from
  it architecturally rather than porting it.
- `plans/plan_dxold.md` — the roadmap this renderer is row 10 of.
