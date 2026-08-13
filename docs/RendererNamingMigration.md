# CNA renderer naming migration (2026-08)

The owner-directed terminology + renderer-identity normalization performed on
`feature/renderer-naming-normalization` (base: post-modularization `develop`
`25db3ccbe`, endpoint `16f76cf1a`). It is a deliberate, breaking naming
normalization of the source and build surface — no renderer behavior, module
boundary, dependency edge, or SharpRuntime component mapping changed, and no
compatibility aliases were kept. The public renderer identity count is **41
before and exactly 41 after**.

**Status: complete and public.** Promoted to `develop` on 2026-08-10 by the
pre-renderer-expansion fast-forward, together with its accepted descendant
`feature/module-examples`. Everything below describes the live `develop` naming;
the old spellings survive only in historical records.

Campaign evidence (engine, per-pass rename maps, baseline/after inventories,
mechanical-replay reconciliation): `modularization/renderer-naming/`.

## 1. Graphics "backend" → "renderer" terminology

CNA's own graphics subsystem now uses **renderer** wherever "backend" used to
mean a CNA graphics renderer implementation:

| Old | New |
|---|---|
| `GraphicsBackendType` (`CNA/GraphicsBackendType.hpp`) | `GraphicsRendererType` (`CNA/GraphicsRendererType.hpp`) |
| `getCurrentGraphicsBackendType/Name()` | `getCurrentGraphicsRendererType/Name()` |
| `GraphicsDevice::GetBackend()` / `GetGraphicsBackendType/Name()` | `GetRenderer()` / `GetGraphicsRendererType/Name()` |
| `CNA::Internal::Backends` (+ `CNA/Internal/Backends/` headers) | `CNA::Internal::Renderers` (+ `CNA/Internal/Renderers/`) |
| `IGraphicsBackend`, `GraphicsBackendCreateArgs`, `CreateGraphicsBackend` | `IGraphicsRenderer`, `GraphicsRendererCreateArgs`, `CreateGraphicsRenderer` |
| resource contracts `ITextureBackend`, `ISpriteBatchBackend`, `IEffectBackend`, `IVertexBufferBackend`, … | `ITextureRenderer`, `ISpriteBatchRenderer`, `IEffectRenderer`, `IVertexBufferRenderer`, … |
| `<Family>GraphicsBackend` classes (`VulkanGraphicsBackend`, `SdlGraphicsBackend`, …) | `<Family>Renderer` (`VulkanRenderer`, `SdlRenderer`, …) |
| per-family `<Family><X>Backend` implementation classes | `<Family><X>Renderer` |
| `CNA_GRAPHICS_BACKEND` (CMake selector) | `CNA_GRAPHICS_RENDERER` |
| `CNA_BACKEND_<NAME>` (options + compile definitions, incl. internal `CNA_BACKEND_EASYGL`) | `CNA_RENDERER_<NAME>` (incl. `CNA_RENDERER_EASYGL`) |
| `cna_backend_graphics_<family>` targets, `BACKEND_TARGET`/`BACKEND_DIR` | `cna_renderer_<family>`, `RENDERER_TARGET`/`RENDERER_DIR` |
| `cmake/BackendSelection.cmake`, `cna_add_renderer_backend()`, `cna_register_backend_test()` | `cmake/RendererSelection.cmake`, `cna_add_renderer()`, `cna_register_renderer_test()` |
| renderer docs `docs/<family>-backend.md`, `docs/graphics-backend-feature-matrix.md` | `docs/<family>-renderer.md`, `docs/graphics-renderer-feature-matrix.md` |

Unchanged on purpose (not CNA graphics-renderer terminology):

- other subsystems' backend concepts: net (`ENetBackend`), audio prose
  ("audio backend"), and their docs (`docs/input-backend.md`,
  `docs/devices-native-backend-design.md`);
- third-party/upstream API vocabulary: Skia `GrBackendRenderTarget`/
  `GrBackendSurface`/`SkSurfaces::WrapBackendRenderTarget`, sokol
  `sg_query_backend`/`SG_BACKEND_*`, bgfx `bgfx::RendererType::*` (including its
  `OpenGLES` value and name string);
- historical evidence: `audit/`, `modularization/` campaign records, plan
  ledgers (`plan_*.md`, `NEXT*.md` task histories), spike directories, and the
  historical `BackendLibraries.cmake` references that describe the pre-Phase-3
  build.

## 2. DirectX renderer identity normalization

Public identities (selector / enum / module directory / main class):

| Old selector | New selector | Old enum | New enum | Old module dir | New module dir | Main class |
|---|---|---|---|---|---|---|
| `DX1` | `DIRECTX1` | `Dx1` | `DirectX1` | `renderers/dx1` | `renderers/directx1` | `DirectX1Renderer` |
| `DX2` | `DIRECTX2` | `Dx2` | `DirectX2` | `renderers/dx2` | `renderers/directx2` | `DirectX2Renderer` |
| `DX3` | `DIRECTX3` | `Dx3` | `DirectX3` | `renderers/dx3` | `renderers/directx3` | `DirectX3Renderer` |
| `DX5` | `DIRECTX5` | `Dx5` | `DirectX5` | `renderers/dx5` | `renderers/directx5` | `DirectX5Renderer` |
| `DX6` | `DIRECTX6` | `Dx6` | `DirectX6` | `renderers/dx6` | `renderers/directx6` | `DirectX6Renderer` |
| `DX7` | `DIRECTX7` | `Dx7` | `DirectX7` | `renderers/dx7` | `renderers/directx7` | `DirectX7Renderer` |
| `DX8` | `DIRECTX8` | `Dx8` | `DirectX8` | `renderers/dx8` | `renderers/directx8` | `DirectX8Renderer` |
| `D3D9` | `DIRECTX9` | `D3D9` | `DirectX9` | `renderers/d3d9` | `renderers/directx9` | `DirectX9Renderer` |
| `D3D10` | `DIRECTX10` | `D3D10` | `DirectX10` | `renderers/d3d10` | `renderers/directx10` | `DirectX10Renderer` |
| `D3D11` | `DIRECTX11` | `D3D11` | `DirectX11` | `renderers/d3d11` | `renderers/directx11` | `DirectX11Renderer` |
| `D3D12` | `DIRECTX12` | `D3D12` | `DirectX12` | `renderers/d3d12` | `renderers/directx12` | `DirectX12Renderer` |

There is intentionally **no DIRECTX4** (CNA never had that identity).
`DIRECT2D` and `FREEDIRECT` are unchanged. Macro/target/test surfaces follow:
`CNA_RENDERER_DIRECTX<N>`, `cna_renderer_directx<N>`, `cmake/Tests/DirectX<N>Tests.cmake`,
ctest names `DirectX<N>_*`, wine runners `scripts/run-wine-directx{1..8,10}.sh`,
discipline checks `scripts/check-directx*`, examples `examples/directx<N>_*.cpp`,
docs `docs/directx<N>-renderer.md`, namespaces
`CNA::Internal::Renderers::DirectX<N>`.

Native Microsoft vocabulary intentionally retained:

- `modules/renderers/common/d3d` (namespace `D3DCommon`, target
  `cna_renderer_d3dcommon`) — real shared Direct3D-native helper code, consumed
  by the directx11/directx12 families; DIRECTX9 and DIRECTX10 remain independent
  of it (unchanged dependency shape).
- Inside the directx9…directx12 families every class that wraps an actual
  Direct3D API object keeps its `D3D<N>` prefix (`D3D11Buffers`,
  `D3D11SamplerCache`, `D3D11InputLayoutCache`, `D3D12PipelineStateCache`,
  `D3D12ResourceStateTracker`, `D3D9EffectRenderer` + `cna_renderer_d3d9_effect`,
  `D3D11EffectRenderer`, `D3D<N><X>Renderer` resource implementations, …).
  Only the renderer identity class and the selection surface flipped to
  `DirectX<N>`. In DX1..DX8 families all `Dx<N>*` identifiers flipped, because
  `Dx<N>` never named a native API (their native APIs are DirectDraw/Direct3D
  COM revisions).
- `ID3D11Device`, `D3D12_*`/`D3D11_*` native macros, `d3d9/d3d11/d3d12/dxgi/
  d3dcompiler` link libraries, `d3d*.h`/`.dll` names, `D3DCompile`, DXVK/vkd3d
  tool scripts (`run-wine-dxvk.sh`, `run-wine-dxvk9.sh`, `run-wine-vkd3d.sh`),
  and the DiligentCore/sokol native-API axes (their own `D3D11`/`D3D12` values).
- Plan-ledger task IDs (`DX2-46`, `DX7-0`, `D9-23`, …) are historical
  identifiers and are preserved verbatim.

## 3. OPENGLES → OPENGLES3

The established OpenGL ES 3.x EasyGL route is now selected as **OPENGLES3**:
`GraphicsRendererType::OpenGLES3`, `-DCNA_GRAPHICS_RENDERER=OPENGLES3` (still the
Linux default), `CNA_RENDERER_OPENGLES3` option, `CNA_GL_PROFILE_OPENGLES3`
profile define. `OPENGLES1` is unchanged; **OPENGLES2 did not exist yet at
migration time** — the future expansion was reserved to add it so the family
reads OPENGLES1/OPENGLES2/OPENGLES3. (Since realized: the Phase-2 expansion
added `OPENGLES2` on 2026-08-10 on exactly that reserved name — see
`plan_opengles2.md` / `docs/opengles2-renderer.md`.)
EasyGL remains the internal shared implementation of the GL profiles (four at
migration time, five since the `OPENGLES2` addition:
`CNA_RENDERER_EASYGL`, `cna_renderer_easygl`, `EasyGLRenderer`) and is still not
a public identity. bgfx's upstream `RendererType::OpenGLES` is untouched.

## 4. NOXNA → CNAEXT

Two related but distinct macros were renamed; they must stay distinct because
the marker is defined in every TU via `CNAHelper.hpp`:

| Old | New | Role |
|---|---|---|
| `NOXNA` | `CNAEXT` | always-defined extension **marker** on beyond-XNA declarations; expands to `[[deprecated]]` under `CNA_STRICT_XNA_API` (unchanged) |
| `CNA_NOXNA` | `CNA_CNAEXT` | CMake option + compile definition **gating** the `CNA::Graphics` engine layer (`#ifdef CNA_CNAEXT` TUs in graphics-ext) |
| `cna_noxna` / `CNA::NoXna` | `cna_cnaext` / `CNA::CnaExt` | extension umbrella (INTERFACE over `CNA::GraphicsExt` + `CNA::DevicesExt`, composition unchanged) |
| `probe_noxna` / `ModuleLinkClosure_NoXnaComposition` | `probe_cnaext` / `ModuleLinkClosure_CnaExtComposition` | module probes |
| `examples/noxna_settings_example.cpp` / `NOXNA_Settings_Compile_Run` | `examples/cnaext_settings_example.cpp` / `CNAEXT_Settings_Compile_Run` | example + ctest |
| `NOXNA.md` | `CNAEXT.md` | extension-layer design doc |
| `modules/input/src/NoXna/` | `modules/input/src/CnaExt/` | extension source area (module boundaries unchanged) |

Historical ledgers `input_noxna.md`, `input_noxna_progress.md` and
`noxna_devices.md` keep their names, and active comments citing them keep the
citations verbatim; their historical task IDs (N01, N-007, …) are unchanged.
Active CNA preprocessor uses of `NOXNA` are zero.

## 5. Canonical identity registry after normalization

`SDL_RENDERER OPENGLES3 OPENGL33 WEBGL1 WEBGL2 BGFX VULKAN WEBGPU MAGNUM
HEADLESS SOFTWARE STUB DIRECTX11 DIRECTX12 DIRECT2D CANVAS HTML_DOM SKIA ASCII
FREEDIRECT DIRECTX9 DIRECTX1 DIRECTX2 DIRECTX3 DIRECTX5 DIRECTX6 DIRECTX7
DIRECTX8 DIRECTX10 SDL_GPU OPENGLES1 OPENGL4 OPENGL1 OPENGL2 WICKED SOKOL
DILIGENT GLIDE GDI LLGL METAL OPENVG`

41 identities at normalization time; `OPENVG` (claude/renderer-openvg-c2wnet, ShivaVG-backed 2D
vector-graphics renderer) is the first Phase 2 addition on top of that baseline, bringing the
live count to 42.

Pinned by `scripts/check_renderer_identities.py` against both registries
(`GraphicsRendererType` enum and the `CNA_GRAPHICS_RENDERER` STRINGS list); the
old selectors (`DX1`..`DX8`, `D3D9`..`D3D12`, `OPENGLES`) are rejected as
unknown. See `docs/renderer-registry.md` for the full table.

## 6. Consumer migration summary

- configure with `-DCNA_GRAPHICS_RENDERER=<identity>` (new spellings above);
- test with `#if defined(CNA_RENDERER_<IDENTITY>)`; the GL-family internal
  identity is `CNA_RENDERER_EASYGL` + `CNA_GL_PROFILE_*`;
- link `CNA::CnaExt` (not `CNA::NoXna`), enable the engine layer with
  `-DCNA_CNAEXT=ON`, tag extensions with `CNAEXT`;
- include `CNA/GraphicsRendererType.hpp` and
  `CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp`;
- no aliases for the old names exist anywhere.
