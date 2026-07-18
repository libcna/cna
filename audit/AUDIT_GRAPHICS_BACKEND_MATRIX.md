# AUDIT_GRAPHICS_BACKEND_MATRIX.md — Cross-Backend Capability Matrix

**Status: SKELETON — to be populated during Pass 4, after per-backend per-file audits (Pass 2) provide evidence.**

## Confirmed backend list (verified against the repository, 2026-07-18)

14 real backends + 2 shared-infrastructure directories, matching the audit prompt's list exactly:

Ascii · Bgfx · Canvas · D3D11 · D3D12 · D3D9 · Dx3 · EasyGL · Headless · SdlGpu · SdlRenderer · Software · Vulkan ·
WebGPU — plus shared `D3DCommon` (D3D9/D3D11/D3D12 common infra) and `Common` (cross-backend `IGraphicsBackend`
contract, audited as shared code, not a standalone backend).

| Backend | src files | include files | External dependency | Structural note |
|---|---|---|---|---|
| Ascii | 3 | 3 | — | |
| Bgfx | 32 | 2 | `bgfx`/`bx` (upstream OSS) | |
| Canvas | 4 | 4 | — | Emscripten `canvas.getContext('2d')`, GPU-free 2D-only per `cmake/BackendSelection.cmake` |
| D3D11 | 10 | 10 | Windows SDK (d3d11/dxgi/d3dcompiler) | shares `D3DCommon` |
| D3D12 | 13 | 13 | Windows SDK (d3d12/dxgi) | shares `D3DCommon` |
| D3D9 | 46 (34 + 12 vendored-exempt) | 16 | Windows SDK (d3d9/d3dcompiler) | vendored FNA stock-effect HLSL (D-5); shares `D3DCommon` |
| Dx3 | 1 (single-file adapter, `Dx3GraphicsBackend.cpp`) | 1 | `free-direct` (sibling repo, D-6) | |
| EasyGL | 1 (single-file adapter, 4733-line `EasyGLGraphicsBackend.cpp`) | 1 (629-line `.hpp`) | `easy-gl` (sibling repo, D-6) | default backend on Linux/Emscripten per `cmake/BackendSelection.cmake` |
| Headless | 1 | 1 | — | |
| SdlGpu | 26 | 1 | SDL3 GPU API (vendored submodule) | |
| SdlRenderer | 1 | 1 | SDL3 2D renderer (vendored submodule) | |
| Software | 1 | 1 | — | |
| Vulkan | 38 | 2 | system Vulkan SDK | |
| WebGPU | 1 | 1 | `wgpu-native` (downloaded binary, `THIRD_PARTY_NOTICES.md`) | experimental per `CLAUDE.md` |
| D3DCommon (shared) | 41 (+5 vendored-exempt) | 5 | — | shared by D3D9/D3D11/D3D12 |
| Common (shared) | 0 | 2 | — | `IGraphicsBackend` contract |

Note the maturity split visible from file count alone: EasyGL/Dx3/Headless/SdlRenderer/Software/WebGPU are each a
**single monolithic adapter file** wrapping either an external engine library or a thin native API, while
Vulkan/D3D9/Bgfx/SdlGpu/D3D12/D3D11 are split across dozens of files. This is a maintainability data point in its
own right (see per-backend audit reports for whether the single-file backends' size is a real concern) and not
itself evidence of feature completeness or incompleteness — to be judged per-feature below, not assumed from
line/file count.

## Feature-by-feature matrix

**To be filled in during Pass 4** using the full feature list from the audit prompt §13 (init, device/adapter,
presentation, resize, fullscreen, clear, render targets/MRT, backbuffer +readback, texture create/upload/readback/
formats/compressed/mipmaps/cubemaps, depth/stencil, blend/rasterizer/sampler state, vertex/index buffers (static +
dynamic), vertex declarations, primitive/indexed drawing, instancing, shader create/compile/bind/constants, effect
plumbing, SpriteBatch paths, MSAA, sRGB/gamma, coordinate/clip-space/winding conventions, disposal, device
reset/loss, error reporting, sync/stalls, shutdown, platform availability, CI coverage, test coverage) — classified
per backend as `FULL` / `PARTIAL` / `STUB` / `UNSUPPORTED_INTENTIONALLY` / `MISSING_UNEXPECTEDLY` /
`NOT_APPLICABLE` / `NOT_VERIFIED`, each with an evidence citation (symbol/file/test).

_(populated after Pass 2 backend shard audits complete — see AUDIT_PROGRESS.md for current phase)_

## EasyGL cross-comparison

**To be filled in during Pass 4.** For every major EasyGL feature: is it required XNA behavior or a CNA-specific
extension; which other backends implement it; gaps; inconsistent semantics; backend-specific bugs; duplicated logic
that should be shared. EasyGL is an internal comparison baseline only, not the compatibility authority (XNA
4.0/FNA is) — per the audit prompt's explicit instruction.
