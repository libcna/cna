# DIRECTX8 (real `IDirect3D8`/`IDirect3DDevice8`, DXVK-delivered, fixed-function) Renderer — Completeness Status

`DIRECTX8` is architecturally very different from every renderer in this family so far (`DIRECTX1`..`DIRECTX7`, all
DirectDraw-based): DirectDraw and Direct3D merge into a single API in DirectX 8, so this renderer has
**no DirectDraw at all** — a single `IDirect3D8::CreateDevice` call creates both the device and its
own swap chain, the same device-bring-up shape as this project's own `DirectX9Renderer`. Delivered
via **DXVK 2.6.0's D8VK** (`Direct3DCreate8` resolved from DXVK's own `d3d8.dll`), not Wine's
built-in `ddraw.dll`/`d3d8.dll` — the same "Route B" pattern D3D9/D3D11/D3D12 already use in this
project, not the Wine-native pattern `DIRECTX1`..`DIRECTX7` use. Scope is **fixed-function 3D only**, matching
`DIRECTX1`..`DIRECTX7`'s own CPU-transform-and-submit shape — not real Shader Model 1.x programmable shaders
(a project-owner decision recorded in `plans/plan_dx8.md` and `dx8-spike/README.md`, since real XNA
effects need `ps_2_0`+ regardless of SM1.x support).

**This document covers what's specific to `DIRECTX8`.** Unlike `DIRECTX3`..`DIRECTX7` (each a mechanical port of
its predecessor with a documented delta), `DIRECTX8`'s 2D layer, device bring-up, and readback path are
all genuinely new designs, not ports — see `plans/plan_dx8.md` for the full design-decision record.

---

## 1. Existence-gate spike (`DX8-0`)

| # | Spike | What it proves | Result |
|---|---|---|---|
| `DX8-0a` | `Direct3DCreate8` + `IDirect3D8::CreateDevice` via DXVK's installed `d3d8.dll` | Whether real device creation works through DXVK | ✅ Works — DXVK log confirms genuine engagement ("D3D9DeviceEx", "operating in D3D8 compatibility mode") |
| `DX8-0b` | `Clear(rect_count=0, rects=nullptr, TARGET\|ZBUFFER\|STENCIL, ...)` | Whether `count=0` clears the WHOLE target (D3D9 convention) or nothing (the `DIRECTX5`-`DIRECTX7` `Clear2` gotcha) | ✅ Clears everything — exact `(10,10,10)` readback |
| `DX8-0c` | `SetVertexShader(rawFvfValue)` + `DrawIndexedPrimitiveUP` — Gouraud quad, no vertex buffer | Whether the FVF-via-SetVertexShader idiom actually works | ✅ Works — real Gouraud interpolation confirmed |
| `DX8-0d` | `DrawPrimitiveUP`, far(blue)-then-near(red) overlapping triangles, `D3DRS_ZENABLE=TRUE` | Whether real Z-test occlusion works | ✅ Works — exact red readback (near wins) |
| `DX8-0e` | `CreateTexture` + `SetTexture` + `SetTextureStageState(D3DTSS_COLOROP, D3DTOP_MODULATE)` | Whether the modern stage-state texture mechanism works from the start (pre-empting `DX7-0`'s own legacy-render-state rejection) | ✅ Works — exact solid-red readback, first try |
| `DX8-0f` | Stencil write (left-half quad) then test (full-screen quad) | Whether real stencil write+test works | ✅ Works — exact green/black after write, red/black after test |
| `DX8-0g` | `Present()` with a real window | Whether the real DXVK-backed swap chain presents without error | ✅ Works — real Vulkan swapchain confirmed in the log |

One real bug found and fixed by the spike: `D3DPRESENT_PARAMETERS.FullScreen_PresentationInterval`
must be `D3DPRESENT_INTERVAL_DEFAULT` (not `D3DPRESENT_INTERVAL_IMMEDIATE`) in windowed mode, or
`CreateDevice` fails with `D3DERR_INVALIDCALL` — real DXVK/D3D8-compat validation, not a Wine bug.
See `dx8-spike/README.md` for the full record, including two further runtime bugs found only by the
full CTest suite (below).

## 2. Real architectural deltas vs. `DIRECTX1`..`DIRECTX7`

- **No DirectDraw.** No `IDirectDrawX`/`IDirectDrawSurfaceX` object anywhere in this renderer — a
  single `IDirect3D8::CreateDevice(adapter, type, hFocusWindow, behaviorFlags, &presentParams,
  &device)` call creates the device AND its own real swap chain. No manual "shadow backbuffer +
  identity `Blt` to primary" trick this whole family needed since `DX2-0`.
- **`D3DTLVERTEX`/`D3DFVF_TLVERTEX` are gone from the real headers.** D3D8 introduced the generic
  FVF/vertex-declaration model — callers define their own struct matching the FVF byte layout. The
  FVF *values* (`D3DFVF_XYZRHW`/`DIFFUSE`/`SPECULAR`/`TEX1`) are unchanged, so this renderer's
  `DirectX8TLVertex` struct reproduces the exact same 32-byte layout the old macro implied.
- **Render state names changed from `D3DRENDERSTATE_*` to `D3DRS_*`** (the underlying
  `D3DRENDERSTATETYPE` enum values are unchanged) — a naming-convention break, not a behavior one.
  Texture filter/address/anisotropy moved from per-device render states to per-stage
  `D3DTSS_*` texture-stage states (`D3DTSS_MAGFILTER`/`MINFILTER`/`ADDRESSU`/`ADDRESSV`/
  `MAXANISOTROPY`).
- **`DrawPrimitive`/`DrawIndexedPrimitive` require a bound vertex buffer** (`SetStreamSource`) — the
  CPU-memory-pointer submission `DIRECTX2`..`DIRECTX7` all used is `DrawPrimitiveUP`/`DrawIndexedPrimitiveUP`
  instead, which additionally require the FVF to be set via `SetVertexShader(fvfValue)` first —
  passing a raw FVF `DWORD` directly as if it were a real vertex-shader handle (D3D9 later split
  this into a separate `SetFVF()`; a real, confirmed-working D3D8-only idiom).
- **No `GetRenderTargetData`** (a D3D9-only addition) — readback uses `CreateImageSurface` (a
  lockable system-memory surface) + `CopyRects` from the real active surface instead.
- **No scaled-blit primitive at all.** No `StretchRect` (D3D9-only) and `CopyRects` is same-size-copy
  only — a real architectural gap `DX8-0` didn't anticipate, discovered mid-implementation while
  reusing `DIRECTX7`'s own letterboxing test. Solved with a **logical-resolution render target**: all
  rendering (`Clear`/3D draws/`SpriteBatch`) targets an internal, offscreen
  `D3DUSAGE_RENDERTARGET` texture sized to the requested virtual/logical resolution;
  `Present()` then switches to the real swap chain back buffer, draws the logical texture as a
  single letterbox-scaled full-screen quad through the same fixed-function pipeline, calls the real
  `Present()`, then restores the logical (or bound custom `RenderTarget2D`) target for the next
  frame. `GetViewportSize()`/`TransformWindowToLogical`/`TransformLogicalToWindow` all use this
  logical resolution, matching `DIRECTX1`..`DIRECTX7`'s own letterbox convention — NOT D3D9's own simpler
  "swap chain always matches the window" choice.
- **2D `SpriteBatch` is a redesign, not a port.** `DIRECTX1`..`DIRECTX7` all used DirectDraw's `Blt`/`BltFast`
  for a CPU-side pixel compositor. With no DirectDraw at all, `DirectX8SpriteBatchRenderer` renders real
  GPU-textured quads through the same fixed-function pipeline 3D geometry uses, with real
  `D3DRS_ALPHABLENDENABLE`/`SRCBLEND`/`DESTBLEND` state — a genuine capability improvement (real GPU
  blending, real rotation/scale) over `DIRECTX1`..`DIRECTX7`'s CPU-approximated-per-blend-mode-formula
  approach, matching the pattern every non-DirectDraw-based CNA renderer (D3D9/EasyGL/Vulkan) uses.
- **Real hardware blending, no preset-detection fallback.** `DIRECTX2`..`DIRECTX7`'s CPU-emulated
  `ApplyBlendState` detects which of the 4 XNA `BlendState` *presets* a caller's factors match and
  falls back to a hardcoded `AlphaBlend` formula for anything else (no real programmable blend
  hardware to fall back to). `DIRECTX8`'s `ApplyBlendState` sets `D3DRS_SRCBLEND`/`D3DRS_DESTBLEND`
  directly from the caller's own factors — ANY `BlendState`, preset or custom, produces its own
  genuine hardware blend result. D3D8 has no configurable blend *equation* though (no
  `D3DRS_BLENDOP` — a D3D9 addition), so a custom `BlendState` with `Opaque`'s exact factors but a
  different `BlendFunction` is genuinely indistinguishable from real `Opaque` on this hardware — a
  real, documented DirectX-8-era boundary, verified by `DirectX8_Blend`'s own Check F.
- **`AnisotropicFiltering` reports `true`** — unlike every prior renderer in this family (`DIRECTX2`..`DIRECTX7`,
  all confirmed-absent on their shared software rasterizer), `DIRECTX8` runs on a real GPU via DXVK, so
  this is a genuine, distinct capability here.
- **No native mip chain**, same as `DIRECTX2`..`DIRECTX7`: `Texture2D::SetData(level>0, ...)` throws honestly
  (`DirectX8TextureRenderer`/`DirectX8RenderTargetRenderer::UpdatePixelsLevel`) rather than silently discarding
  the upload.
- Everything else (CPU transform/clip pipeline — Sutherland-Hodgman near-plane clip, perspective
  divide, `BasicEffect` CPU lighting — `VertexBuffer`/`IndexBuffer` plain CPU-side storage) is
  conceptually the same math as `DIRECTX7`'s own `DirectX7ClipVertex`/`DirectX7ClipTriangleNearPlane`/
  `DirectX7ComputeVertexLighting`, re-expressed against the new `DirectX8TLVertex` struct and
  `DrawIndexedPrimitiveUP` submission shape — not a mechanical port, since the surrounding API shape
  differs too much.

## 3. Real bugs found only by the full CTest suite (beyond the `DX8-0` spike)

The `DX8-0` spike above only ever exercises raw D3D8 through a bare Win32 window with no SDL and a
single `Present()` per run. The full (SDL-based) CTest suite — every test calls `Present()` twice:
once explicitly in its own `Draw()`, once more automatically via the CNA framework's `EndDraw()` —
hit three further real bugs, none of them anticipated by the spike:

1. **A real `DirectX8Renderer::SetVirtualResolution` bug.** The logical render target (texture +
   surface + depth-stencil) is created once at construction using whatever `virtualWidth`/
   `virtualHeight` the renderer happened to be constructed with (the SDL window's own initial/default
   size, e.g. 800x480) — but `GraphicsDeviceManager`'s own `ApplyChanges()` calls
   `SetVirtualResolution` afterward with the test's REAL preferred size (e.g. 64x64), and the
   original implementation just updated bookkeeping fields without recreating the underlying D3D8
   resources. This left `ReadBackbuffer`'s `CopyRects` call copying from an 800x480 real surface
   into a 64x64 destination — a genuine size mismatch, `D3DERR_INVALIDCALL`. Fixed by having
   `SetVirtualResolution` actually recreate the logical texture/surface/depth-stencil at the new
   size (and rebind it as the active render target if it was the active one).
2. **Two environment-specific Wine/DXVK/AMD-RADV bugs, not code defects** — both reproduced in
   minimal, CNA-free spikes (`dx8_spike3_sdl_load_order.cpp`/`dx8_spike4_double_present.cpp`) and
   fully documented in `dx8-spike/README.md`'s "Two further runtime bugs" section:
   - SDL3's own internal, dynamic `LoadLibrary("dxgi.dll")` probe (for HDR/colorimetry detection,
     unrelated to whether the game uses Direct3D at all) crashes under this sandbox's Xvfb (no real
     monitor EDID) when `dxgi` is overridden to DXVK — corrupting Wine's monitor-enumeration state
     for the rest of the process. Fixed by giving `DIRECTX8` its own dedicated `~/.wine-cna-dx8` Wine
     prefix with `d3d8`/`d3d9`/`d3d10`/`d3d10_1`/`d3d10core`/`d3d11` native (D8VK genuinely needs
     `d3d9` native) but `dxgi` deliberately left as Wine's own builtin.
   - A real AMD RADV driver bug: the SECOND consecutive `Present()` call on this sandbox's GPU fails
     with `VK_ERROR_SURFACE_LOST_KHR` (fatal — Wine's X11 error handler aborts the process), 100%
     reproducible, unrelated to window resizing, timing, or message-pump gaps. Forcing DXVK onto the
     software (`llvmpipe`) Vulkan device instead avoids it entirely. `scripts/run-wine-directx8.sh` sets
     `DXVK_FILTER_DEVICE_NAME=llvmpipe` by default for this reason.
3. **A real D3D8 XYZRHW half-texel alignment bug.** D3D8's pre-transformed (`D3DFVF_XYZRHW`)
   vertices place texel/pixel centers at integer coordinates, not integer+0.5 like modern APIs (the
   same classic D3D8/9 convention `D3D9SpriteBatchRenderer` compensates for by baking a half-texel
   shift into its own orthographic projection matrix). Since `DirectX8SpriteBatchRenderer`'s quad is
   already in screen space with no such matrix, the same -0.5 shift is applied directly to the
   final screen-space quad corners instead (`DirectX8SpriteBatchRenderer::Draw` and `Present()`'s own
   full-screen quad). Found via `DirectX8_SpriteBatch`'s `SpriteEffects::FlipHorizontally` check (a
   1:1 texel:pixel mapping, maximally sensitive to any half-texel misalignment) and confirmed to
   also fix `DirectX8_AddressMode`'s `TextureAddressMode::Mirror` check, which failed for the same
   underlying reason.

## 4. CTest results

**20/20 `DIRECTX8`-labeled CTests pass**: `DirectX8_LegacyInterfaceDiscipline`, `DirectX8_Smoke`,
`DirectX8_TextureRenderTarget`, `DirectX8_SpriteBatch`, `DirectX8_Blend`, `DirectX8_AddressMode`, `DirectX8_SpriteFont`,
`DirectX8_GraphicsCapability`, `DirectX8_LogicalTransform`, `DirectX8_Device3DSmoke`, `DirectX8_VertexIndexBuffer`,
`DirectX8_ColoredPrimitives`, `DirectX8_IndexedPrimitives`, `DirectX8_ZTest`, `DirectX8_Texture3D`, `DirectX8_Clipping`,
`DirectX8_RemainingDefaults`, `DirectX8_Lighting`, `DirectX8_WireframeAniso`, `DirectX8_Stencil`.

`SupportsCapability(GraphicsCapability::WireFrame)` reports `true`; unlike every prior renderer in
this family, `AnisotropicFiltering` also reports `true` (real GPU via DXVK — §2 above).
`SupportsCapability(GraphicsCapability::DepthStencilBuffer)` reports `true`.

Cross-renderer regression: `GraphicsRendererCompileDefinitionsTest.ExactlyOneGraphicsRendererIsSelected`,
`GraphicsDeviceValidationTest.SetRenderTargets_*`, and `GraphicsDeviceCapabilityTest.*` all pass
except the same 3 pre-existing, already-documented ungated-test-class failures `DX2-84` found
(`SupportsMultipleRenderTargets`/`SupportsOcclusionQuery`/`SupportsCustomEffects` — that test file's
own header comment documents it as only ever building against a fully 3D-capable renderer) — zero new
regressions.

The legacy-interface-discipline check (`scripts/check-directx8-legacy-interface-discipline.sh`) forbids
`IDirectDrawX`/`DDSURFACEDESC`/`DDSCAPS`/`D3DTLVERTEX`/`D3DVT_*`/`D3DRENDERSTATE_*` anywhere in this
renderer's source — a real, automated proof this renderer genuinely has no DirectDraw and uses only
modern `D3DRS_*` render-state naming, not just claimed.

## 5. Boundaries — explicitly out of scope

Same DirectX-8-era boundaries as documented in `plans/plan_dx8.md`: no Shader Model 1.x programmable
shaders/`CreateEffectRenderer` (project-owner scope decision), no multitexture, no DXTn compressed
formats, no cube environment maps, no MRT (`SetRenderTargets(count>1)` throws), no occlusion query,
no native mip chain (`SetData(level>0)` throws).

## See also

- `plans/plan_dx8.md` — this renderer's own implementation plan and design-decision record.
- `dx8-spike/README.md` — the full `DX8-0` spike record plus the two further runtime-bug
  investigations (§3 above).
- `docs/directx7-renderer.md` — the last DirectDraw-based renderer in this family; `DIRECTX8` diverges from it
  architecturally rather than porting it.
- `plans/plan_dxold.md` — the roadmap this renderer is a row of.
