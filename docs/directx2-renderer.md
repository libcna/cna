# DIRECTX2 (real DirectDraw v1 + Direct3D v2 `DrawPrimitive`) Renderer — Completeness Status

`DIRECTX2` is CNA's first legacy-DirectX renderer with a **real, working 3D pipeline** — unlike `DIRECTX1`
(2D-only by construction, since DirectX 1 shipped no Direct3D at all) and unlike `DIRECTX3` (which
deliberately throws on every 3D call even though its `../free-direct` sibling's underlying
DirectDraw generation technically has an execute-buffer Direct3D counterpart it doesn't use). Its
2D layer is a verbatim port of `DIRECTX1`'s own real `IDirectDraw`/`IDirectDrawSurface` **v1**-only
code (`plans/plan_dxold.md`'s roadmap: "DirectDraw v1 unchanged" between DIRECTX1 and DIRECTX2). Its 3D layer is
built on `IDirect3D2`/`IDirect3DDevice2`/`IDirect3DViewport2`/`IDirect3DTexture2`'s
`DrawPrimitive`/`DrawIndexedPrimitive` immediate-mode API — **not** the literal DirectX-2-SDK
`IDirect3D`/`IDirect3DDevice` execute-buffer surface, which was spiked exhaustively (14 variants)
and found non-functional in this environment's Wine (see `dx2-spike/README.md`). Cross-compiled
via MinGW-w64 and run under Wine, the same Route B delivery mechanism `D3D9`/`D3D11`/`D3D12`/`DIRECTX1`
already use.

This document is the completeness status after `plans/plan_dx2.md`'s full Phase O1–O9 implementation
(Phase O9 added real CPU-side lighting and a `WireFrame`/`AnisotropicFiltering` re-verification —
§6a and §7). Every row cites the task(s) that verified it — see `plans/plan_dx2.md`'s own task tables for
full design rationale and code detail, and `dx2-spike/README.md` for the existence-gate spike
record.

**Status legend** (matches `docs/directx1-renderer.md`'s own convention)

- ✅ — fully supported, matches FNA/XNA behavior exactly (or as closely as this DirectX era's real
  hardware/API surface reasonably allows).
- 🟨 — code exists but does not fully meet its own stated goal; a real, documented, permanent
  limitation rather than a hidden gap.
- ❌-throws-by-design — intentionally unsupported; throws a clear, specific exception rather than
  silently no-op'ing or producing wrong output.
- ⚪-degrades-to-nullptr — intentionally unsupported, but via `IGraphicsRenderer`'s own
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
- Two further spikes de-risked Phase O3/O4 before any renderer code was written:
  `IDirect3DViewport(2)::Clear()` has no depth/color *value* parameter (only
  `IDirect3DViewport3::Clear2`, DIRECTX5+, does) — confirmed a manually-`Lock()`-written Z-buffer value
  is genuinely respected by the real depth test instead. A surface with both
  `DDSCAPS_OFFSCREENPLAIN` and `DDSCAPS_TEXTURE` supports plain 2D `Lock`/`Blt` **and** real
  `IDirect3DTexture2` 3D sampling on the *same* surface instance.
- **Owner-confirmed decision**: build DIRECTX2's 3D layer on `IDirect3DDevice2`'s `DrawPrimitive`
  rather than staying strictly within the literal DirectX-2-SDK execute-buffer surface — a
  deliberate scope choice to deliver genuine working 3D over exact-SDK-version purity.

## 1. Device / window bring-up — 2D layer (Phase O1/O2, ported from `DIRECTX1`)

| Feature | Status | Notes |
|---|---|---|
| `CNA_GRAPHICS_RENDERER=DIRECTX2` CMake selection, Windows-only gate, MinGW cross-compile | ✅ | Same `FATAL_ERROR` gate `D3D9`/`D3D11`/`D3D12`/`DIRECTX1` already share. |
| `DirectDrawCreate` → `SetCooperativeLevel(DDSCL_NORMAL)` → primary `CreateSurface` | ✅ | Verbatim port of `DIRECTX1`'s real device/window bring-up (real Win32 `HWND`, no `SetDisplayMode` call). |
| `Clear()` / `Present()` | ✅ | Same shadow-backbuffer + per-frame letterbox `Present()` architecture as `DIRECTX1`, with one real change: the shadow-backbuffer surface is now flagged `DDSCAPS_OFFSCREENPLAIN \| DDSCAPS_3DDEVICE` (design decision 4) so a real Direct3D v2 device can be created against it. |
| Pixel-exact readback (`DirectX2_Smoke` CTest) | ✅ | Same 4 checks as `DirectX1_Smoke`. |
| `SetPresentationMode()` | 🟨 | Same honest scope as `DIRECTX1` — `Stretch`/`Overscan`/`NativeBackBuffer` not yet distinguished. |
| `TransformWindowToLogical`/`TransformLogicalToWindow` | ✅ | Ported verbatim from `DX1-68`; `DirectX2_LogicalTransform` CTest. |

## 2. Texture2D / RenderTarget2D (Phase O2/O4, ported from `DIRECTX1` + a real 3D extension)

| Feature | Status | Notes |
|---|---|---|
| `DirectX2TextureRenderer`/`DirectX2RenderTargetRenderer` construction | ✅ | Ported from `DIRECTX1`. `CreateOffscreenSurface` now requests `DDSCAPS_OFFSCREENPLAIN \| DDSCAPS_TEXTURE` (not `DIRECTX1`'s plain `OFFSCREENPLAIN`) — spike-confirmed (`dx2_spike9_dualcap_texture.cpp`) that both caps together support plain 2D `Lock`/`Blt` **and** real 3D `IDirect3DTexture2` sampling on the same surface. |
| `SetData`/`UpdatePixels` round-trip | ✅ | Ported from `DIRECTX1`. |
| Mip levels (`level>0` `SetData`) | ❌-throws-by-design | Ported from `DIRECTX1` — no native mip chain on `IDirectDrawSurface`. |
| `SetRenderTarget2D` / bind-redirect | ✅ | Ported from `DIRECTX1`. |
| `SetRenderTargets` with 2+ bindings (MRT) | ❌-throws-by-design | Ported from `DX1-27`; confirmed by `DirectX2_GraphicsCapability`. |
| Real 3D texture sampling (`GpuDrawParams::texture0`) | ✅ | `DirectX2ResolveTextureHandle` fetches a fresh `IDirect3DTexture2`+`D3DTEXTUREHANDLE` per draw (not cached — a handle is only valid for the specific device instance it was obtained against, and `device2` is torn down/recreated on backbuffer resize), bound via `D3DRENDERSTATE_TEXTUREHANDLE`+`TEXTUREMAPBLEND=MODULATE`. `DirectX2_Texture3D` CTest: a 2x2 checker texture's 4 quadrants read back exactly correct. |

## 3. SpriteBatch CPU compositor + blend math (Phase O2, ported verbatim from `DIRECTX1`)

Identical to `docs/directx1-renderer.md` §3/§4 — `CompositeQuad`, all 4 blend-mode formulas,
`Wrap`/`Mirror`/`Clamp` sampling, `Point`/`Linear` filtering. `DirectX2_SpriteBatch` (10/10),
`DirectX2_Blend` (5/5), `DirectX2_AddressMode` (5/5) CTests all pass.

## 4. `SpriteFont` (Phase O2, ported verbatim from `DIRECTX1`)

Needs zero new renderer code beyond the `SpriteBatch` draw path, the same finding `DX1-50`/`51`
made. `DirectX2_SpriteFont` CTest (5 checks) passes.

## 5. Real 3D device bring-up (Phase O3)

| Feature | Status | Notes |
|---|---|---|
| `IDirect3D2` acquisition | ✅ | `dd->QueryInterface(IID_IDirect3D2, ...)`, created once at construction (independent of backbuffer size), reused across resizes. |
| Z-buffer + `IDirect3DDevice2` + `IDirect3DViewport2` creation | ✅ | A 16-bit `DDSCAPS_ZBUFFER` surface, `AddAttachedSurface`'d onto the shadow-backbuffer; `IID_IDirect3DRGBDevice` created via `CreateDevice` (the historically-correct, no-hardware-required software device — spike-confirmed to route the same as `IID_IDirect3DHALDevice` in this environment); a full-viewport `IDirect3DViewport2`. All torn down and recreated inside `CreateBackBuffer()` whenever the backbuffer itself is resized. |
| `ClearColorAndDepth`/`ClearDepth`/`ClearDepthAndStencil`/`ClearColorAndStencil`/`ClearColorDepthAndStencil` | ✅ | Real, via direct `Lock()`+fill on the Z-buffer surface (`FillZBuffer16`) — **not** `viewport->Clear()`, which has no depth/color *value* parameter in this API generation (only `IDirect3DViewport3::Clear2`, DIRECTX5+, does). Stencil parameters are accepted-and-ignored. |
| `ClearStencil` (stencil-only) | ⚠-accepted-and-ignored | No real stencil buffer exists at this DirectX era (DIRECTX6+); complete no-op. |
| `SupportsDepthStencil()` | ✅ (`true`) | Unlike `DIRECTX1`'s permanent `false`. |
| `DirectX2_Device3DSmoke` CTest | ✅ | 4/4 checks: device bring-up, both depth-aware `Clear` overloads, state-toggle methods no longer throwing (Phase O6). |

## 6. Real 3D rendering — CPU transform/clip + `DrawIndexedPrimitive` (Phase O4/O5)

| Feature | Status | Notes |
|---|---|---|
| `DirectX2VertexBufferRenderer`/`DirectX2IndexBufferRenderer` (16- and 32-bit) | ✅ | Plain CPU-side `std::vector<uint8_t>` storage, matching the `Software` renderer's own identical approach — the CPU transform pipeline reads directly from these buffers each draw. `CreateIndexBuffer32` explicitly overridden with real 32-bit storage (not the shared default's delegate-to-16-bit fallback). `DirectX2_VertexIndexBuffer` CTest (5/5). |
| CPU transform + near-plane clip | ✅ | `DirectX2ClipVertex`/`DirectX2ClipTriangleNearPlane`/`DirectX2BuildPositionColorClipVertex`/`DirectX2BuildGenericClipVertex`, ported from `SoftwareRenderer.cpp`'s own math, simplified (envMap/skinning out of scope, see §7; lighting is real for stride 32/52 as of Phase O9, see §6a). `DirectX2_Clipping` CTest (2/2): a straddling triangle clips to a visible partial fragment, a fully-behind-camera triangle renders nothing. |
| `D3DTLVERTEX` packing | ✅ | `DirectX2ClipVertexToD3DTLVERTEX` — perspective-divides the **position only**; color/uv are **not** premultiplied by `invW` (unlike `Software`'s own `RasterVertex`, which does — real Direct3D already performs perspective-correct interpolation internally via `rhw`, so premultiplying again would double-apply it). |
| `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives` | ✅ | `VertexPositionColor`, `TriangleList` only, submitted via the shared `SubmitDx2Primitives` helper — uses `DrawIndexedPrimitive` internally even for non-indexed calls, since near-plane clipping can turn one triangle into a quad needing 2 triangles sharing vertices. `DirectX2_ColoredPrimitives` (2/2), `DirectX2_IndexedPrimitives` (2/2, both 16- and 32-bit index widths). |
| `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx` | ✅ | Stride-dispatched vertex layouts (16/20/24/32/52 bytes, matching `Software`'s own set); real texture0 modulation. Honors `GpuDrawParams::vertexStart`/`startIndex`/`baseVertex` (a real gap found in the `Software` reference renderer — it doesn't honor these at all, `EasyGL` does — DIRECTX2 matches `EasyGL`). |
| Depth-test occlusion | ✅ | `DirectX2_ZTest` CTest (2/2): both draw orders, proving real, order-independent depth-test occlusion, not "last write wins". |
| Real texture sampling in a 3D draw | ✅ | `DirectX2_Texture3D` CTest (see §2). |

## 6a. CPU-side BasicEffect lighting (Phase O9)

| Feature | Status | Notes |
|---|---|---|
| Ambient + up to 3 directional lights (Lambertian diffuse) | ✅ | `DirectX2ComputeVertexLighting()`, evaluated per-vertex (CNA's default across every renderer — real XNA's `PreferPerPixelLighting` defaults to `false`) for `stride==32`/`52` (`VertexPositionNormalTexture`/`Skinned`) when `GpuDrawParams::lightingEnabled`. Ported from `EasyGLRenderer.cpp`'s `EnsureLit3DVertexLitProgram()` GLSL and `BasicEffect::FillGpuDrawParams()`'s field semantics, not re-derived. Normal transformed by `transpose(inverse(World₃ₓ₃))` (cofactor/determinant shortcut, matching `EasyGL`'s own Task-398 fix) — correct under non-uniform World scale. |
| Blinn-Phong specular highlight | ✅ | Computed alongside diffuse, but composited by real `D3DRENDERSTATE_SPECULARENABLE` + `D3DTLVERTEX::specular` hardware **after** the texture-modulate stage — spike-confirmed real (`dx2_spike10_specular_wireframe_aniso.cpp` Test C), not folded into the diffuse channel on the CPU. One honest divergence from XNA's exact shader formula: real fixed-function hardware's specular-add has no per-pixel alpha weighting (`color.rgb += specular*color.a`), so DIRECTX2's highlight isn't alpha-weighted — invisible for the overwhelmingly common opaque case, a subtle brightness difference only for a semi-transparent, specular-lit surface. |
| Fog, multitexture, env-mapping, skinning | ⚪-accepted-and-ignored | Unchanged from Phase O1-O8 — Phase O9 only addressed lighting. Skinned vertices (`stride==52`) are lit using their raw, unskinned local-space position/normal. |
| `DirectX2_Lighting` CTest | ✅ (4/4) | Full-intensity Lambertian pixel-verified against hand-computed values; a light facing away from the surface verified to clamp to 0 (not negative/wrapped); a real specular highlight verified to appear and disappear with `SpecularColor`. |

## 7. Per-draw 3D state mapping (Phase O6)

| Feature | Status | Notes |
|---|---|---|
| `ApplyDepthStencilState` | ✅ (depth) / ⚠ (stencil) | `D3DRENDERSTATE_ZENABLE`/`ZFUNC`/`ZWRITEENABLE` real; every stencil parameter accepted-and-ignored (no real stencil buffer/ops until DIRECTX6, confirmed absent from `d3dtypes.h` by inspection). |
| `ApplyRasterizerState` | ✅ (cull/fill) / ⚠ (scissor/bias) | `D3DRENDERSTATE_CULLMODE`/`FILLMODE` real; `scissorTestEnable`/`depthBias`/`slopeScaleDepthBias` accepted-and-ignored — no such render state exists in `d3dtypes.h` at this era. |
| `ApplyBlendState` | ✅ (color factors) / ⚠ (alpha factors/op) | `D3DRENDERSTATE_ALPHABLENDENABLE`/`SRCBLEND`/`DESTBLEND` real; **D3D v1/v2 has no separate alpha blend-factor/op pair at all** — `alphaSrcBlend`/`alphaDstBlend`/`colorBlendFunc`/`alphaBlendFunc` accepted-and-ignored, a real lossy mapping, not an oversight. |
| `ApplySamplerState` | ✅ (slot 0 only) / ⚠ (addressV, other slots) | `D3DRENDERSTATE_TEXTUREMAG`/`TEXTUREMIN`/`TEXTUREADDRESS`/`ANISOTROPY` real for slot 0 — D3D v1/v2 has exactly one texture stage, no per-slot sampler state concept, and `TEXTUREADDRESS` is a single combined U+V mode (`addressV` accepted-and-ignored, other slots ignored entirely). |
| `SetDepthTestEnabled`/`SetDepthWriteEnabled` | ✅ | Direct `D3DRENDERSTATE_ZENABLE`/`ZWRITEENABLE` toggles. |
| `SetBlendEnabled` | ⚠-accepted-and-ignored (deliberate no-op) | Matches `D3D9`'s/`D3D11`'s/`D3D12`'s own identical reasoning — a bare "enable blending" has no defined factors in XNA; real config always arrives via `ApplyBlendState`, which already unconditionally enables blending. |
| `SupportsCapability(GraphicsCapability::ThreeD)` | ✅ (`true`) | The full bundle that flag's own doc comment defines (vertex/index buffers, 3D draw calls, depth/stencil clears AND state) is genuinely complete as of this phase. |
| `SupportsCapability(GraphicsCapability::DepthStencilBuffer)` | ✅ (`true`) | A real, if depth-only, buffer exists. |
| `SupportsCapability(GraphicsCapability::WireFrame)` | ✅ (`true`, Phase O9) | `dx2_spike10_specular_wireframe_aniso.cpp` Test D empirically confirmed `D3DFILL_WIREFRAME` genuinely renders edge-only output on this environment's software RGB device (a point inside a filled triangle reads back the cleared background color in `WIREFRAME` mode, the triangle's own color in `SOLID` mode) — real, verified distinctness, not assumed from the render state merely existing. `DirectX2_WireframeAniso` CTest (4/4). |
| `SupportsCapability(GraphicsCapability::AnisotropicFiltering)` | `false` (Phase O9: now evidence-backed) | The same spike's Test E rendered a heavily-minified checkerboard under `D3DTFN_POINT`/`LINEAR`/`ANISOTROPIC` (2 levels) and got byte-identical readback at every sampled point across all four — this software RGB device does not implement anisotropic (or even point-vs-linear) minification filtering distinctly at all. `D3DRENDERSTATE_ANISOTROPY` is still set by `ApplySamplerState` (a real, accepted render state); it simply has no observable effect here. |
| `SupportsCapability(GraphicsCapability::MultiSampleAntiAliasing \| MultipleRenderTargets \| OcclusionQuery \| CustomEffects)` | ✅ (`false`) | Genuinely unavailable at this DirectX era. |

`DirectX2_GraphicsCapability` CTest: 9/9 checks, including the `SetRenderTargets(count=2)` throw
(the one remaining real "calling it anyway still throws" case, tied to `MultipleRenderTargets`).

A real, non-regression test finding surfaced while wiring `ApplySamplerState`: `DirectX2_Texture3D`
temporarily failed because real bilinear+wrap filtering (XNA's true default `SamplerState`) now
genuinely blends samples near UV edges — the previous no-op default had silently hidden this. Not
a renderer bug; the test now explicitly requests `SamplerState.PointClamp`, matching what it
actually intends to verify ("does sampling read the right texel", not wrap/bilinear edge behavior).

## 8. Remaining `IGraphicsRenderer` defaults, genuinely unavailable at this era (Phase O7)

| Feature | Status | Notes |
|---|---|---|
| `CreateOcclusionQuery` | ⚪-degrades-to-nullptr | Occlusion queries are DX9-only. Never overridden — the shared `IGraphicsRenderer` default applies directly, same as `DIRECTX1`/`DIRECTX3`. |
| `CreateTexture3D`/`CreateTextureCube`/`CreateRenderTargetCube` | ⚪-degrades-to-nullptr | Volume/cube textures are DIRECTX7/DIRECTX8+. Never overridden. |
| `CreateEffectRenderer` | ⚪-degrades-to-nullptr | No programmable shader stage exists at this DirectX era. Never overridden. |
| `DrawInstancedPrimitivesEx` | ❌-throws-by-design | No instancing concept exists. Never overridden — the shared default throws. |
| `DebugSimulateContextLoss`/`DebugRestoreContext` | ✅ (no-op) | Inherited `IGraphicsRenderer` default. |

`DirectX2_RemainingDefaults` CTest (5/5) proves all of the above empirically rather than leaving it
asserted only in comments.

## 9. What actually works today

A CNA game built with `CNA_GRAPHICS_RENDERER=DIRECTX2` and MinGW-cross-compiled: creates a real window,
initializes a real `IDirectDraw` (v1) + `IDirect3DDevice2` device against it, does everything
`DIRECTX1` does for 2D (`SpriteBatch`, `SpriteFont`, all 4 blend modes, texture sampling/addressing),
**and additionally draws real 3D geometry** — `VertexBuffer`/`IndexBuffer` (16- and 32-bit),
`DrawPrimitives`/`DrawIndexedPrimitives` through `BasicEffect` (vertex-color and one-texture
paths), genuine order-independent depth-test occlusion, near-plane clipping, real per-draw
rasterizer/depth/blend/sampler state, and (Phase O9) real CPU-computed ambient + up to 3
directional-light Lambertian/Blinn-Phong lighting for the two normal-bearing vertex layouts, with
the specular highlight composited by real fixed-function hardware. `WireFrame` fill mode is real
and spike-confirmed distinct. Fog, multitexture, environment mapping, and skinning are still read
from `GpuDrawParams` but not evaluated — matching the `Software` renderer's own identical,
already-documented v1 scope boundary for those specific features. This is measurably more than
`docs/directx-legacy-renderers-analysis.md`'s original analysis-level "~15%" estimate for DIRECTX2/3
(which assumed the execute-buffer path) — see that document's own updated §3.1 note.

## 10. Full `CnaTests` regression (`DX2-84`)

A full `CnaTests` run (all 5415 tests, not just the 17 dedicated `DIRECTX2` CTests) through Wine:
**19 failed (1 confirmed a concurrency flake, independently re-verified as a clean pass in
isolation — 18 genuine), 1 `Not Run`.**

**Two real, pre-existing, cross-renderer CMake/test-infrastructure gaps were found and fixed
along the way** — this from-scratch MinGW + full `CnaTests` configuration had simply never been
exercised via `ctest`'s own per-test discovery before (the same class of "never run this way
before" gap `DX1-88` itself repeatedly found):

1. `cmake/UnitTests.cmake` never wired a `CROSSCOMPILING_EMULATOR` for `DIRECTX1`'s or `DIRECTX2`'s own
   `CnaTests` binary (`D3D9`/`D3D11`/`D3D12` already had one) — without it, `ctest`'s
   `gtest_discover_tests(PRE_TEST)` step can't even enumerate tests under Wine. Fixed for both
   renderers together.
2. `gtest_discover_tests` never set a `WORKING_DIRECTORY`, defaulting to the build directory
   instead of the repo root — but roughly 140 test fixtures (`SongTest`/`MediaLibraryTestFixture`/
   `PlaylistParserTest`/etc.) load real files via a path relative to the repo root, so every one
   of them threw `FileNotFoundException` when ctest ran them from a non-repo-root cwd. Fixed by
   pinning `WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"` — confirmed, by manually reproducing the
   exact failure from the build directory, that this is a **genuine, universal gap affecting every
   renderer run this way, not a DIRECTX2 regression**.

Also found and fixed two renderer-list gaps of the exact same shape `DX1-1`/`DX3-27` already
needed: `GraphicsRendererCompileDefinitionsTest.ExactlyOneGraphicsRendererIsSelected` and
`GraphicsDeviceValidationTest.SetRenderTargets_FourTargets_DoesNotThrow` both lacked a
`CNA_RENDERER_DIRECTX2` case.

**Methodology finding, worth keeping for future full-suite regressions of this renderer family**:
running `ctest -j4` against a shared `WINEPREFIX` causes spurious failures/timeouts scattered
across unrelated test categories — 2 `DIRECTX2` tests, 2 `GamerServices` tests, 1 audio test, and 1
content-loading test all independently reproduced as clean, fast passes when run alone. `-j2`
with a `--timeout` safety net is the practical balance between a fully-serial run's ~3-hour
runtime and `-j4`'s contention; budget for a `ctest --rerun-failed` pass afterward rather than
assuming every parallel failure is real.

**Final 19 failures, categorized against `DX1-88`'s own precedent** (`docs/directx1-renderer.md` §7a):

| Category | Count | Notes |
|---|---|---|
| `MediaLibraryTestFixture`/`MediaLibrarySavePictureTest` | 7 | Pre-existing `CNA_FFMPEG_AVAILABLE=OFF` gap, matching `DX1-88`'s own ~6. |
| `GraphicsDeviceCapabilityTest.SupportsMultipleRenderTargets`/`SupportsOcclusionQuery`/`SupportsCustomEffects` | 3 | This test has **no renderer gate at all** — the same pre-existing design `DX1-88` documented. Unlike `DIRECTX1`, `SupportsThreeD`/`SupportsDepthStencilBuffer` now correctly **pass** for `DIRECTX2`, since real 3D genuinely exists. |
| `CnjTexture3DTest`/`CnjStockEffectTest`/`CnjEffectTest`/`XnbContainerFuzzTest.MutatedRealTexture2DFixture`/`Texture3DTextureCubeContentTypeReaderTest`×2 | 6 | Content genuinely requiring `Texture3D`/custom-effect support DIRECTX2 doesn't have by design — the content-pipeline code doesn't null-check `CreateTexture3D`'s `nullptr` return cleanly. A pre-existing content-pipeline robustness gap that would affect **any** nullptr-returning renderer, not unique to DIRECTX2; out of this task's scope to fix. |
| `AudioTagParserTest.ReadsNonAsciiVorbisCommentTitleCorrectly` | 1 | The **exact same test** `docs/directx1-renderer.md` §7a already names as a pre-existing Windows/Wine non-ASCII-encoding quirk. |
| `StrictXnaApiSurfaceCheck_Compile_Run` (`Not Run`) | 1 | A separate executable target never wired into the `CnaTests` build step — pre-existing, same category `DX1-88` also hit. |

Zero DX2-caused failures remain unaccounted for.

**Phase O9 addendum**: this full-suite regression was not re-run in its entirety for Phase O9's
additive lighting/`WireFrame` change (a multi-hour run, out of proportion to a narrowly-scoped
change). Instead, all 19 `DIRECTX2`-labeled CTests were re-run (17 pre-existing + 2 new, all passing),
plus a targeted re-run of the two cross-renderer `CnaTests` classes this change could plausibly
affect: `GraphicsDeviceCapabilityTest.DoesNotSupportWireFrame` (fixed to branch on
`CNA_RENDERER_DIRECTX2`, now passes) and its 3 already-documented ungated sibling failures above
(`SupportsMultipleRenderTargets`/`SupportsOcclusionQuery`/`SupportsCustomEffects`, unchanged).

## 11. Known permanent limitations

- **No fixed-function fog** (`fogEnabled`/`fogColor`/`fogStart`/`fogEnd`) — matches the `Software`
  renderer's own identical, pre-existing v1 scope boundary. Ambient + up to 3 directional-light
  Lambertian/Blinn-Phong **lighting** is real as of Phase O9 (§6a) for the two normal-bearing
  vertex layouts (`VertexPositionNormalTexture`/`Skinned`, stride 32/52) — strides without a normal
  have no lighting concept and are unaffected.
- **No multitexture, environment mapping, or skinning** (`dualTexture`, `envMapping`, `skinned`)
  — accepted-and-ignored (renders diffuse-texture-only), not thrown. Phase O9's lighting for
  `stride==52` uses the vertex's raw, unskinned local-space position/normal.
- **No real stencil operations** — no stencil buffer/ops exist until DIRECTX6.
- **No separate alpha blend-factor/op pair** — D3D v1/v2 has none; only the color factors map to
  real state.
- **Exactly one texture stage** — `D3DRENDERSTATE_TEXTUREADDRESS` is a single combined U+V mode;
  `ApplySamplerState` only honors slot 0.
- **`IDirectDraw2+` features of any kind** — permanently out of scope for the `DIRECTX2` name
  specifically (2D layer stays v1-only), same boundary `DX1-1` already enforces.
- **No literal execute-buffer Direct3D** — proven non-functional in this environment; the 3D layer
  is deliberately built on the next interface revision (`IDirect3DDevice2`) instead, an
  owner-confirmed scope decision, not an oversight.
- **MRT, instancing, occlusion query, volume/cube textures, custom programmable effects** — none
  exist at this DirectX era; Phase O7's throws/defaults are permanent.
- **Real Windows/macOS hardware verification** — this renderer is proven via MinGW cross-compile +
  Wine on Linux in this dev environment, same caveat every Route-B CNA renderer already carries.

## See also

- `plans/plan_dx2.md` — the full implementation plan (design decisions, phase task tables, the
  execute-buffer-detour status note).
- `plans/plan_dxold.md` — the roadmap this renderer is row 2 of (DIRECTX1/2/3/5/6/7/8/10).
- `docs/directx1-renderer.md` — the 2D architecture this renderer ports verbatim wherever the 3D layer
  doesn't change anything.
- `dx2-spike/README.md` — the full `DX2-0` existence-gate spike record, including all 14
  ruled-out execute-buffer variants and the `IDirect3DDevice2` breakthrough.
- `docs/directx-legacy-renderers-analysis.md` — the feasibility analysis that authorized this whole
  renderer family; §3.1's DIRECTX2/3 capability estimate is superseded by this renderer's actual,
  empirically-verified result.
- `scripts/run-wine-directx2.sh`, `scripts/check-directx2-execute-buffer-discipline.sh` — this renderer's own
  Wine wrapper and execute-buffer-discipline check.
