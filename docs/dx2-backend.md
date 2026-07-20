# DX2 (real DirectDraw v1 + Direct3D v2 `DrawPrimitive`) Backend — Completeness Status

`DX2` is CNA's first legacy-DirectX backend with a **real, working 3D pipeline** — unlike `DX1`
(2D-only by construction, since DirectX 1 shipped no Direct3D at all) and unlike `DX3` (which
deliberately throws on every 3D call even though its `../free-direct` sibling's underlying
DirectDraw generation technically has an execute-buffer Direct3D counterpart it doesn't use). Its
2D layer is a verbatim port of `DX1`'s own real `IDirectDraw`/`IDirectDrawSurface` **v1**-only
code (`plan_dxold.md`'s roadmap: "DirectDraw v1 unchanged" between DX1 and DX2). Its 3D layer is
built on `IDirect3D2`/`IDirect3DDevice2`/`IDirect3DViewport2`/`IDirect3DTexture2`'s
`DrawPrimitive`/`DrawIndexedPrimitive` immediate-mode API — **not** the literal DirectX-2-SDK
`IDirect3D`/`IDirect3DDevice` execute-buffer surface, which was spiked exhaustively (14 variants)
and found non-functional in this environment's Wine (see `dx2-spike/README.md`). Cross-compiled
via MinGW-w64 and run under Wine, the same Route B delivery mechanism `D3D9`/`D3D11`/`D3D12`/`DX1`
already use.

This document is the completeness status after `plan_dx2.md`'s full Phase O1–O8 implementation.
Every row cites the task(s) that verified it — see `plan_dx2.md`'s own task tables for full design
rationale and code detail, and `dx2-spike/README.md` for the existence-gate spike record.

**Status legend** (matches `docs/dx1-backend.md`'s own convention)

- ✅ — fully supported, matches FNA/XNA behavior exactly (or as closely as this DirectX era's real
  hardware/API surface reasonably allows).
- 🟨 — code exists but does not fully meet its own stated goal; a real, documented, permanent
  limitation rather than a hidden gap.
- ❌-throws-by-design — intentionally unsupported; throws a clear, specific exception rather than
  silently no-op'ing or producing wrong output.
- ⚪-degrades-to-nullptr — intentionally unsupported, but via `IGraphicsBackend`'s own
  `return nullptr` default rather than a throw.
- ⚠-accepted-and-ignored — the parameter is accepted without throwing, but has no effect (either a
  real capability gap at this DirectX era, or a lossy real-D3D-render-state mapping).

---

## 0. Existence-gate spike (`DX2-0`) — the execute-buffer detour

Full record: `dx2-spike/README.md`. Summary:

- The literal DirectX-2-SDK `IDirect3D`/`IDirect3DDevice` execute-buffer API
  (`IDirect3DDevice::Execute`, `D3DOP_TRIANGLE` instruction streams) was tried first, exhaustively
  (14 variants: vertex formats, render states, render-target types, device GUIDs, readback
  paths). **Every one produced black**, despite every API call succeeding and Wine's own trace
  confirming a mechanically correct pipeline (real FBO, correct vertex stride, correct instruction
  parsing, real draw call issued).
- Following a suggestion to isolate the failure to a specific call rather than conclude "old D3D
  is broken in Wine" wholesale: `IDirect3DDevice2::DrawPrimitive`/`DrawIndexedPrimitive` (added one
  interface revision later, in the DirectX 3 SDK) **works correctly** — real Gouraud interpolation,
  genuine Z-test occlusion, correct texture sampling, all reproducible across runs.
- Two further spikes de-risked Phase O3/O4 before any backend code was written:
  `IDirect3DViewport(2)::Clear()` has no depth/color *value* parameter (only
  `IDirect3DViewport3::Clear2`, DX5+, does) — confirmed a manually-`Lock()`-written Z-buffer value
  is genuinely respected by the real depth test instead. A surface with both
  `DDSCAPS_OFFSCREENPLAIN` and `DDSCAPS_TEXTURE` supports plain 2D `Lock`/`Blt` **and** real
  `IDirect3DTexture2` 3D sampling on the *same* surface instance.
- **Owner-confirmed decision**: build DX2's 3D layer on `IDirect3DDevice2`'s `DrawPrimitive`
  rather than staying strictly within the literal DirectX-2-SDK execute-buffer surface — a
  deliberate scope choice to deliver genuine working 3D over exact-SDK-version purity.

## 1. Device / window bring-up — 2D layer (Phase O1/O2, ported from `DX1`)

| Feature | Status | Notes |
|---|---|---|
| `CNA_GRAPHICS_BACKEND=DX2` CMake selection, Windows-only gate, MinGW cross-compile | ✅ | Same `FATAL_ERROR` gate `D3D9`/`D3D11`/`D3D12`/`DX1` already share. |
| `DirectDrawCreate` → `SetCooperativeLevel(DDSCL_NORMAL)` → primary `CreateSurface` | ✅ | Verbatim port of `DX1`'s real device/window bring-up (real Win32 `HWND`, no `SetDisplayMode` call). |
| `Clear()` / `Present()` | ✅ | Same shadow-backbuffer + per-frame letterbox `Present()` architecture as `DX1`, with one real change: the shadow-backbuffer surface is now flagged `DDSCAPS_OFFSCREENPLAIN \| DDSCAPS_3DDEVICE` (design decision 4) so a real Direct3D v2 device can be created against it. |
| Pixel-exact readback (`Dx2_Smoke` CTest) | ✅ | Same 4 checks as `Dx1_Smoke`. |
| `SetPresentationMode()` | 🟨 | Same honest scope as `DX1` — `Stretch`/`Overscan`/`NativeBackBuffer` not yet distinguished. |
| `TransformWindowToLogical`/`TransformLogicalToWindow` | ✅ | Ported verbatim from `DX1-68`; `Dx2_LogicalTransform` CTest. |

## 2. Texture2D / RenderTarget2D (Phase O2/O4, ported from `DX1` + a real 3D extension)

| Feature | Status | Notes |
|---|---|---|
| `Dx2TextureBackend`/`Dx2RenderTargetBackend` construction | ✅ | Ported from `DX1`. `CreateOffscreenSurface` now requests `DDSCAPS_OFFSCREENPLAIN \| DDSCAPS_TEXTURE` (not `DX1`'s plain `OFFSCREENPLAIN`) — spike-confirmed (`dx2_spike9_dualcap_texture.cpp`) that both caps together support plain 2D `Lock`/`Blt` **and** real 3D `IDirect3DTexture2` sampling on the same surface. |
| `SetData`/`UpdatePixels` round-trip | ✅ | Ported from `DX1`. |
| Mip levels (`level>0` `SetData`) | ❌-throws-by-design | Ported from `DX1` — no native mip chain on `IDirectDrawSurface`. |
| `SetRenderTarget2D` / bind-redirect | ✅ | Ported from `DX1`. |
| `SetRenderTargets` with 2+ bindings (MRT) | ❌-throws-by-design | Ported from `DX1-27`; confirmed by `Dx2_GraphicsCapability`. |
| Real 3D texture sampling (`GpuDrawParams::texture0`) | ✅ | `Dx2ResolveTextureHandle` fetches a fresh `IDirect3DTexture2`+`D3DTEXTUREHANDLE` per draw (not cached — a handle is only valid for the specific device instance it was obtained against, and `device2` is torn down/recreated on backbuffer resize), bound via `D3DRENDERSTATE_TEXTUREHANDLE`+`TEXTUREMAPBLEND=MODULATE`. `Dx2_Texture3D` CTest: a 2x2 checker texture's 4 quadrants read back exactly correct. |

## 3. SpriteBatch CPU compositor + blend math (Phase O2, ported verbatim from `DX1`)

Identical to `docs/dx1-backend.md` §3/§4 — `CompositeQuad`, all 4 blend-mode formulas,
`Wrap`/`Mirror`/`Clamp` sampling, `Point`/`Linear` filtering. `Dx2_SpriteBatch` (10/10),
`Dx2_Blend` (5/5), `Dx2_AddressMode` (5/5) CTests all pass.

## 4. `SpriteFont` (Phase O2, ported verbatim from `DX1`)

Needs zero new backend code beyond the `SpriteBatch` draw path, the same finding `DX1-50`/`51`
made. `Dx2_SpriteFont` CTest (5 checks) passes.

## 5. Real 3D device bring-up (Phase O3)

| Feature | Status | Notes |
|---|---|---|
| `IDirect3D2` acquisition | ✅ | `dd->QueryInterface(IID_IDirect3D2, ...)`, created once at construction (independent of backbuffer size), reused across resizes. |
| Z-buffer + `IDirect3DDevice2` + `IDirect3DViewport2` creation | ✅ | A 16-bit `DDSCAPS_ZBUFFER` surface, `AddAttachedSurface`'d onto the shadow-backbuffer; `IID_IDirect3DRGBDevice` created via `CreateDevice` (the historically-correct, no-hardware-required software device — spike-confirmed to route the same as `IID_IDirect3DHALDevice` in this environment); a full-viewport `IDirect3DViewport2`. All torn down and recreated inside `CreateBackBuffer()` whenever the backbuffer itself is resized. |
| `ClearColorAndDepth`/`ClearDepth`/`ClearDepthAndStencil`/`ClearColorAndStencil`/`ClearColorDepthAndStencil` | ✅ | Real, via direct `Lock()`+fill on the Z-buffer surface (`FillZBuffer16`) — **not** `viewport->Clear()`, which has no depth/color *value* parameter in this API generation (only `IDirect3DViewport3::Clear2`, DX5+, does). Stencil parameters are accepted-and-ignored. |
| `ClearStencil` (stencil-only) | ⚠-accepted-and-ignored | No real stencil buffer exists at this DirectX era (DX6+); complete no-op. |
| `SupportsDepthStencil()` | ✅ (`true`) | Unlike `DX1`'s permanent `false`. |
| `Dx2_Device3DSmoke` CTest | ✅ | 4/4 checks: device bring-up, both depth-aware `Clear` overloads, state-toggle methods no longer throwing (Phase O6). |

## 6. Real 3D rendering — CPU transform/clip + `DrawIndexedPrimitive` (Phase O4/O5)

| Feature | Status | Notes |
|---|---|---|
| `Dx2VertexBufferBackend`/`Dx2IndexBufferBackend` (16- and 32-bit) | ✅ | Plain CPU-side `std::vector<uint8_t>` storage, matching the `Software` backend's own identical approach — the CPU transform pipeline reads directly from these buffers each draw. `CreateIndexBuffer32` explicitly overridden with real 32-bit storage (not the shared default's delegate-to-16-bit fallback). `Dx2_VertexIndexBuffer` CTest (5/5). |
| CPU transform + near-plane clip | ✅ | `Dx2ClipVertex`/`Dx2ClipTriangleNearPlane`/`Dx2BuildPositionColorClipVertex`/`Dx2BuildGenericClipVertex`, ported from `SoftwareGraphicsBackend.cpp`'s own math, simplified (no world-space position/normal — lighting/envMap/skinning out of scope, see §7). `Dx2_Clipping` CTest (2/2): a straddling triangle clips to a visible partial fragment, a fully-behind-camera triangle renders nothing. |
| `D3DTLVERTEX` packing | ✅ | `Dx2ClipVertexToD3DTLVERTEX` — perspective-divides the **position only**; color/uv are **not** premultiplied by `invW` (unlike `Software`'s own `RasterVertex`, which does — real Direct3D already performs perspective-correct interpolation internally via `rhw`, so premultiplying again would double-apply it). |
| `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives` | ✅ | `VertexPositionColor`, `TriangleList` only, submitted via the shared `SubmitDx2Primitives` helper — uses `DrawIndexedPrimitive` internally even for non-indexed calls, since near-plane clipping can turn one triangle into a quad needing 2 triangles sharing vertices. `Dx2_ColoredPrimitives` (2/2), `Dx2_IndexedPrimitives` (2/2, both 16- and 32-bit index widths). |
| `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx` | ✅ | Stride-dispatched vertex layouts (16/20/24/32/52 bytes, matching `Software`'s own set); real texture0 modulation. Honors `GpuDrawParams::vertexStart`/`startIndex`/`baseVertex` (a real gap found in the `Software` reference backend — it doesn't honor these at all, `EasyGL` does — DX2 matches `EasyGL`). |
| Depth-test occlusion | ✅ | `Dx2_ZTest` CTest (2/2): both draw orders, proving real, order-independent depth-test occlusion, not "last write wins". |
| Real texture sampling in a 3D draw | ✅ | `Dx2_Texture3D` CTest (see §2). |

## 7. Per-draw 3D state mapping (Phase O6)

| Feature | Status | Notes |
|---|---|---|
| `ApplyDepthStencilState` | ✅ (depth) / ⚠ (stencil) | `D3DRENDERSTATE_ZENABLE`/`ZFUNC`/`ZWRITEENABLE` real; every stencil parameter accepted-and-ignored (no real stencil buffer/ops until DX6, confirmed absent from `d3dtypes.h` by inspection). |
| `ApplyRasterizerState` | ✅ (cull/fill) / ⚠ (scissor/bias) | `D3DRENDERSTATE_CULLMODE`/`FILLMODE` real; `scissorTestEnable`/`depthBias`/`slopeScaleDepthBias` accepted-and-ignored — no such render state exists in `d3dtypes.h` at this era. |
| `ApplyBlendState` | ✅ (color factors) / ⚠ (alpha factors/op) | `D3DRENDERSTATE_ALPHABLENDENABLE`/`SRCBLEND`/`DESTBLEND` real; **D3D v1/v2 has no separate alpha blend-factor/op pair at all** — `alphaSrcBlend`/`alphaDstBlend`/`colorBlendFunc`/`alphaBlendFunc` accepted-and-ignored, a real lossy mapping, not an oversight. |
| `ApplySamplerState` | ✅ (slot 0 only) / ⚠ (addressV, other slots) | `D3DRENDERSTATE_TEXTUREMAG`/`TEXTUREMIN`/`TEXTUREADDRESS`/`ANISOTROPY` real for slot 0 — D3D v1/v2 has exactly one texture stage, no per-slot sampler state concept, and `TEXTUREADDRESS` is a single combined U+V mode (`addressV` accepted-and-ignored, other slots ignored entirely). |
| `SetDepthTestEnabled`/`SetDepthWriteEnabled` | ✅ | Direct `D3DRENDERSTATE_ZENABLE`/`ZWRITEENABLE` toggles. |
| `SetBlendEnabled` | ⚠-accepted-and-ignored (deliberate no-op) | Matches `D3D9`'s/`D3D11`'s/`D3D12`'s own identical reasoning — a bare "enable blending" has no defined factors in XNA; real config always arrives via `ApplyBlendState`, which already unconditionally enables blending. |
| `SupportsCapability(GraphicsCapability::ThreeD)` | ✅ (`true`) | The full bundle that flag's own doc comment defines (vertex/index buffers, 3D draw calls, depth/stencil clears AND state) is genuinely complete as of this phase. |
| `SupportsCapability(GraphicsCapability::DepthStencilBuffer)` | ✅ (`true`) | A real, if depth-only, buffer exists. |
| `SupportsCapability(GraphicsCapability::WireFrame)`/`AnisotropicFiltering` | conservatively `false` | The corresponding render states (`D3DFILL_WIREFRAME`/`D3DRENDERSTATE_ANISOTROPY`) are real and accepted, but neither was spike-verified to produce genuinely distinct rendering output on this environment's software RGB device — reporting `true` would be an unverified claim, not a "doesn't exist" one. |
| `SupportsCapability(GraphicsCapability::MultiSampleAntiAliasing \| MultipleRenderTargets \| OcclusionQuery \| CustomEffects)` | ✅ (`false`) | Genuinely unavailable at this DirectX era. |

`Dx2_GraphicsCapability` CTest: 9/9 checks, including the `SetRenderTargets(count=2)` throw
(the one remaining real "calling it anyway still throws" case, tied to `MultipleRenderTargets`).

A real, non-regression test finding surfaced while wiring `ApplySamplerState`: `Dx2_Texture3D`
temporarily failed because real bilinear+wrap filtering (XNA's true default `SamplerState`) now
genuinely blends samples near UV edges — the previous no-op default had silently hidden this. Not
a backend bug; the test now explicitly requests `SamplerState.PointClamp`, matching what it
actually intends to verify ("does sampling read the right texel", not wrap/bilinear edge behavior).

## 8. Remaining `IGraphicsBackend` defaults, genuinely unavailable at this era (Phase O7)

| Feature | Status | Notes |
|---|---|---|
| `CreateOcclusionQuery` | ⚪-degrades-to-nullptr | Occlusion queries are DX9-only. Never overridden — the shared `IGraphicsBackend` default applies directly, same as `DX1`/`DX3`. |
| `CreateTexture3D`/`CreateTextureCube`/`CreateRenderTargetCube` | ⚪-degrades-to-nullptr | Volume/cube textures are DX7/DX8+. Never overridden. |
| `CreateEffectBackend` | ⚪-degrades-to-nullptr | No programmable shader stage exists at this DirectX era. Never overridden. |
| `DrawInstancedPrimitivesEx` | ❌-throws-by-design | No instancing concept exists. Never overridden — the shared default throws. |
| `DebugSimulateContextLoss`/`DebugRestoreContext` | ✅ (no-op) | Inherited `IGraphicsBackend` default. |

`Dx2_RemainingDefaults` CTest (5/5) proves all of the above empirically rather than leaving it
asserted only in comments.

## 9. What actually works today

A CNA game built with `CNA_GRAPHICS_BACKEND=DX2` and MinGW-cross-compiled: creates a real window,
initializes a real `IDirectDraw` (v1) + `IDirect3DDevice2` device against it, does everything
`DX1` does for 2D (`SpriteBatch`, `SpriteFont`, all 4 blend modes, texture sampling/addressing),
**and additionally draws real 3D geometry** — `VertexBuffer`/`IndexBuffer` (16- and 32-bit),
`DrawPrimitives`/`DrawIndexedPrimitives` through `BasicEffect` (vertex-color and one-texture
paths), genuine order-independent depth-test occlusion, near-plane clipping, and real per-draw
rasterizer/depth/blend/sampler state. Lighting, fog, multitexture, environment mapping, and
skinning are read from `GpuDrawParams` but not evaluated — the vertex's own diffuse color and
(single) texture sample are used as-is, matching the `Software` backend's own identical,
already-documented v1 scope boundary. This is measurably more than
`docs/directx-legacy-backends-analysis.md`'s original analysis-level "~15%" estimate for DX2/3
(which assumed the execute-buffer path) — see that document's own updated §3.1 note.

## 10. Full `CnaTests` regression (`DX2-84`)

A full `CnaTests` run (all 5415 tests, not just the 17 dedicated `DX2` CTests) through Wine:
**19 failed (1 confirmed a concurrency flake, independently re-verified as a clean pass in
isolation — 18 genuine), 1 `Not Run`.**

**Two real, pre-existing, cross-backend CMake/test-infrastructure gaps were found and fixed
along the way** — this from-scratch MinGW + full `CnaTests` configuration had simply never been
exercised via `ctest`'s own per-test discovery before (the same class of "never run this way
before" gap `DX1-88` itself repeatedly found):

1. `cmake/UnitTests.cmake` never wired a `CROSSCOMPILING_EMULATOR` for `DX1`'s or `DX2`'s own
   `CnaTests` binary (`D3D9`/`D3D11`/`D3D12` already had one) — without it, `ctest`'s
   `gtest_discover_tests(PRE_TEST)` step can't even enumerate tests under Wine. Fixed for both
   backends together.
2. `gtest_discover_tests` never set a `WORKING_DIRECTORY`, defaulting to the build directory
   instead of the repo root — but roughly 140 test fixtures (`SongTest`/`MediaLibraryTestFixture`/
   `PlaylistParserTest`/etc.) load real files via a path relative to the repo root, so every one
   of them threw `FileNotFoundException` when ctest ran them from a non-repo-root cwd. Fixed by
   pinning `WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"` — confirmed, by manually reproducing the
   exact failure from the build directory, that this is a **genuine, universal gap affecting every
   backend run this way, not a DX2 regression**.

Also found and fixed two backend-list gaps of the exact same shape `DX1-1`/`DX3-27` already
needed: `GraphicsBackendCompileDefinitionsTest.ExactlyOneGraphicsBackendIsSelected` and
`GraphicsDeviceValidationTest.SetRenderTargets_FourTargets_DoesNotThrow` both lacked a
`CNA_BACKEND_DX2` case.

**Methodology finding, worth keeping for future full-suite regressions of this backend family**:
running `ctest -j4` against a shared `WINEPREFIX` causes spurious failures/timeouts scattered
across unrelated test categories — 2 `DX2` tests, 2 `GamerServices` tests, 1 audio test, and 1
content-loading test all independently reproduced as clean, fast passes when run alone. `-j2`
with a `--timeout` safety net is the practical balance between a fully-serial run's ~3-hour
runtime and `-j4`'s contention; budget for a `ctest --rerun-failed` pass afterward rather than
assuming every parallel failure is real.

**Final 19 failures, categorized against `DX1-88`'s own precedent** (`docs/dx1-backend.md` §7a):

| Category | Count | Notes |
|---|---|---|
| `MediaLibraryTestFixture`/`MediaLibrarySavePictureTest` | 7 | Pre-existing `CNA_FFMPEG_AVAILABLE=OFF` gap, matching `DX1-88`'s own ~6. |
| `GraphicsDeviceCapabilityTest.SupportsMultipleRenderTargets`/`SupportsOcclusionQuery`/`SupportsCustomEffects` | 3 | This test has **no backend gate at all** — the same pre-existing design `DX1-88` documented. Unlike `DX1`, `SupportsThreeD`/`SupportsDepthStencilBuffer` now correctly **pass** for `DX2`, since real 3D genuinely exists. |
| `CnjTexture3DTest`/`CnjStockEffectTest`/`CnjEffectTest`/`XnbContainerFuzzTest.MutatedRealTexture2DFixture`/`Texture3DTextureCubeContentTypeReaderTest`×2 | 6 | Content genuinely requiring `Texture3D`/custom-effect support DX2 doesn't have by design — the content-pipeline code doesn't null-check `CreateTexture3D`'s `nullptr` return cleanly. A pre-existing content-pipeline robustness gap that would affect **any** nullptr-returning backend, not unique to DX2; out of this task's scope to fix. |
| `AudioTagParserTest.ReadsNonAsciiVorbisCommentTitleCorrectly` | 1 | The **exact same test** `docs/dx1-backend.md` §7a already names as a pre-existing Windows/Wine non-ASCII-encoding quirk. |
| `StrictXnaApiSurfaceCheck_Compile_Run` (`Not Run`) | 1 | A separate executable target never wired into the `CnaTests` build step — pre-existing, same category `DX1-88` also hit. |

Zero DX2-caused failures remain unaccounted for.

## 11. Known permanent limitations

- **No fixed-function lighting/fog** (`lightingEnabled`, `ambientColor`, `light{0,1,2}*`,
  `fogEnabled`/etc.) — matches the `Software` backend's own identical, pre-existing v1 scope
  boundary; the vertex's own diffuse color is used as-is.
- **No multitexture, environment mapping, or skinning** (`dualTexture`, `envMapping`, `skinned`)
  — accepted-and-ignored (renders diffuse-texture-only), not thrown.
- **No real stencil operations** — no stencil buffer/ops exist until DX6.
- **No separate alpha blend-factor/op pair** — D3D v1/v2 has none; only the color factors map to
  real state.
- **Exactly one texture stage** — `D3DRENDERSTATE_TEXTUREADDRESS` is a single combined U+V mode;
  `ApplySamplerState` only honors slot 0.
- **`IDirectDraw2+` features of any kind** — permanently out of scope for the `DX2` name
  specifically (2D layer stays v1-only), same boundary `DX1-1` already enforces.
- **No literal execute-buffer Direct3D** — proven non-functional in this environment; the 3D layer
  is deliberately built on the next interface revision (`IDirect3DDevice2`) instead, an
  owner-confirmed scope decision, not an oversight.
- **MRT, instancing, occlusion query, volume/cube textures, custom programmable effects** — none
  exist at this DirectX era; Phase O7's throws/defaults are permanent.
- **Real Windows/macOS hardware verification** — this backend is proven via MinGW cross-compile +
  Wine on Linux in this dev environment, same caveat every Route-B CNA backend already carries.

## See also

- `plan_dx2.md` — the full implementation plan (design decisions, phase task tables, the
  execute-buffer-detour status note).
- `plan_dxold.md` — the roadmap this backend is row 2 of (DX1/2/3/5/6/7/8/10).
- `docs/dx1-backend.md` — the 2D architecture this backend ports verbatim wherever the 3D layer
  doesn't change anything.
- `dx2-spike/README.md` — the full `DX2-0` existence-gate spike record, including all 14
  ruled-out execute-buffer variants and the `IDirect3DDevice2` breakthrough.
- `docs/directx-legacy-backends-analysis.md` — the feasibility analysis that authorized this whole
  backend family; §3.1's DX2/3 capability estimate is superseded by this backend's actual,
  empirically-verified result.
- `scripts/run-wine-dx2.sh`, `scripts/check-dx2-execute-buffer-discipline.sh` — this backend's own
  Wine wrapper and execute-buffer-discipline check.
