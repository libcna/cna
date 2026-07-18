# AUDIT_GRAPHICS_BACKEND_MATRIX.md — Cross-Backend Capability Matrix

**Status: PARTIALLY POPULATED (2026-07-19).** All 16 backend shards are now directly audited (Pass 2 backend
work complete). The cross-cutting defect matrix below is fully evidenced and populated. The full ~30-feature
grid (presentation modes, disposal, sRGB/gamma, CI coverage, etc.) still needs the `xna-graphics`/`tests-*`
shards (Task #4/#6) for several cells and remains SKELETON below.

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

**Full ~30-feature x 16-backend grid (init, presentation, MSAA, sRGB/gamma, disposal, CI coverage, etc.) is
still to be populated** — many of those features' evidence lives in shards not yet audited (`xna-graphics`
for disposal/effect semantics, `tests-*`/CI config for coverage). The sections below populate the
**cross-cutting defect matrix**, which IS fully evidenced now that all 16 backend shards are directly
audited (Pass 2 complete for backends) — this is the highest-value, freshest-evidence part of Pass 4 to do
immediately; the remaining feature cells should be filled in as `xna-graphics`/`tests-*` shards are audited
(Task #4/#6), since several features (disposal, effect-parameter plumbing) are only meaningfully verifiable
against that XNA-facing code, not the backend internals alone.

### Cross-cutting defect matrix (fully evidenced — Pass 2 backend audits complete)

Every row below cites the backend(s) confirmed to share the defect, confirmed NOT to share it (a genuine
positive), and N/A where the defect class cannot apply (e.g. no deferred-recording model to have a
render-target-scissor bug in). "?" = not yet directly checked (backend not yet reached this specific test,
even though its own shard is otherwise audited). See `AUDIT_CROSS_CUTTING_FINDINGS.md` for full narrative detail
on every row.

| Defect | Confirmed AFFECTED | Confirmed CLEAN (positive) | N/A / not applicable |
|---|---|---|---|
| Fog formula backwards (pre-Task-1111 mirrored `(FogEnd-z)/(FogEnd-FogStart)` vs. correct `(z+FogEnd)/(FogEnd-FogStart)`) | Bgfx, Vulkan (**original source**), D3D11+D3D12 (shared D3DCommon, all 15 fog shaders) | EasyGL (fixed pre-session) | D3D9 vendored stock effects (real `ComputeFogVectorEXT()`, correct FNA port); D3D9 CNA-custom shaders have a *different* defect (object-space-only, see below), not this one |
| SkinnedEffect world-space-normal-transform (complete omission variant) | EasyGL, WebGPU, Vulkan, SdlGpu, D3D11+D3D12, Bgfx — **ALL 14 backends with a SkinnedEffect implementation, exhaustively confirmed** | — | D3D9 vendored stock `SkinnedEffect.fx` (real Microsoft bytecode, bug structurally impossible) |
| SkinnedEffect normal-transform narrower variant (raw World, not inverse-transpose, applied post-skin) | EasyGL, SdlGpu, D3D9 (`SkinnedVertexColor3D`/`PbrSkinned3D`), D3D11+D3D12 PBR-skinned | — | — |
| EnvironmentMapEffect emissive/diffuse re-multiply (`(emissive+lightSum)*diffuse` instead of unscaled add) | Bgfx (**original**), WebGPU, Vulkan, SdlGpu | D3D9's own PBR shaders (`ambient+Lo+emissive`, correct); Vulkan's own PBR shaders (same correct pattern) | D3D11/D3D12 not yet directly re-checked for this specific bug in this pass |
| SkinnedEffect Ambient/Emissive dropped for skinned draws | Vulkan (both fields), D3D11+D3D12 (Emissive half only) | EasyGL, Bgfx, SdlGpu, **D3D9** (4 independent confirmations of the "Ambient pre-folded into Emissive" convention — revises the likely root cause toward backend-side misconsumption, not an upstream `SkinnedEffect::FillGpuDrawParams()` defect) | D3D9 vendored stock `SkinnedEffect.fx` (structurally impossible, real bytecode) |
| Missing Vulkan-NDC Y-flip (renders mirrored) | Vulkan only, 4 effect families (EnvironmentMapEffect, PbrEffect, SkinnedPbrEffect, InstancedEffect) | Vulkan's own other 14 shader families; D3D11/D3D12 correctly and deliberately never apply it (D3D clip space already matches XNA) | D3D9 (same D3D clip-space convention, no flip needed); sprite2d shaders project-wide (own pixel-to-NDC mapping) |
| `SpriteBatch.SetTransformMatrix()` no-op | Vulkan only | EasyGL, Bgfx, D3D9, D3D11, WebGPU, SdlGpu, SdlRenderer, Canvas, Dx3, Software, Headless, Ascii (via delegation) | — |
| `RegisterForWindow` dangling-pointer-on-construction-failure | EasyGL only | WebGPU, Canvas, SdlGpu (own different resource-leak risk instead), D3D11, D3D12, Bgfx, Vulkan, D3D9 (none of these 6 call `RegisterForWindow` at all) | Ascii, Software, Headless, SdlRenderer, Dx3 (not yet re-checked for this specific call in this pass) |
| Stencil + Scissor + DepthBias completeness | D3D12 (Stencil+Scissor: completely non-functional, PSO hardcodes `StencilEnable=FALSE`/never sets `ScissorEnable`); Vulkan (Scissor only: silently ignored whenever a render target is bound, no Viewport-style disclosure); SdlGpu (DepthBias only: not yet emulated, disclosed) | **D3D9 (all 3, most complete of any backend — direct native render states, no emulation)**; Bgfx (all 3, via stencil-state-rebuild + scissor-flag + vertex-shader-Z-offset emulation); SdlGpu (Stencil+Scissor functional) | D3D9's immediate rendering model structurally cannot have Vulkan's RT-bound-scissor-bug shape at all |
| Occlusion query multi-draw-per-Begin/End accumulation | D3D12 (each draw gets its own BeginQuery/EndQuery on the same heap slot, later draws overwrite earlier ones') | **D3D9 (structurally immune — immediate `Issue(BEGIN)`/`Issue(END)` around however many draws happen between them, natural GPU-level accumulation, no bookkeeping needed)** | — |
| `SetDataOptions::NoOverwrite` has no destination-offset parameter (streaming semantics architecturally impossible) | D3D11, EasyGL, **D3D9 (3rd confirmed instance)** | — | Architecture-level `IVertexBufferBackend`/`IIndexBufferBackend` interface gap — likely project-wide; not yet checked on remaining backends |
| Whole-cube mip regeneration touches all 6 faces even when only 1 changed | SdlGpu (`TextureCube`), D3D11 (`RenderTargetCube`) | **D3D12 (`RenderTargetCube`, regenerates only the active face)** | D3D9 (`RenderTargetCube` doesn't support mip regeneration at all — honestly disclosed scope gap, not a bug) |
| Dead-code generic `VertexElementFormat` -> native-format helper header (correct logic, zero production call sites) | Bgfx (`BgfxVertexFormatHelper.hpp`), Vulkan (`VulkanVertexFormatHelper.hpp`) | — | Architecture pattern, not itself an XNA-facing behavior bug — worth checking remaining backends' own equivalent helper (if any) |

### Notable per-backend architectural extremes (not defects, context for the matrix above)

- **Largest single files**: Vulkan's `VulkanGraphicsBackend.cpp` (8954 lines, despite Vulkan being a
  multi-file backend) is the single largest file in this entire audit — surpassing WebGPU's own genuinely
  single-file backend (8805 lines) and EasyGL's own single-file backend (4733 lines). D3D9 (50 files, no
  single file over ~1200 lines) sits at the opposite extreme of the same "monolithic vs. split" spectrum noted
  in the file-count table above.
- **Most XNA-authentic design goal, explicitly stated**: D3D9's own class-level doc comment states its goal as
  "pixel-for-pixel indistinguishability from the original XNA 4.0 runtime, not mere feature parity" — a
  stricter bar than D3D11/D3D12's own stated "parity, not authenticity," backed by concrete evidence
  (61/66 byte-identical vendored-shader matches against Microsoft's real shipped bytecode, per
  `compare_against_fxb.py`; real Reach/HiDef `D3DCAPS9`-backed profile enforcement; loud MRT-over-request
  errors rather than silent degradation).
- **Most complete PBR fragment shader**: Bgfx's, per that shard's own audit (real AlphaTest + real fog on top
  of the base glTF BRDF) — though D3D9's and Vulkan's own PBR shaders both independently get the
  emissive-combination formula right too (see matrix row above).

## EasyGL cross-comparison

**To be filled in during Pass 4.** For every major EasyGL feature: is it required XNA behavior or a CNA-specific
extension; which other backends implement it; gaps; inconsistent semantics; backend-specific bugs; duplicated logic
that should be shared. EasyGL is an internal comparison baseline only, not the compatibility authority (XNA
4.0/FNA is) — per the audit prompt's explicit instruction.
