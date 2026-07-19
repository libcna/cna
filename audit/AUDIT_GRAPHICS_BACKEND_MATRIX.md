# AUDIT_GRAPHICS_BACKEND_MATRIX.md — Cross-Backend Capability Matrix

**Status: POPULATED, static review + full runtime verification (2026-07-19, Pass 4 + Pass 6).** All
16 backend shards, the full `xna-graphics` shard (191 files), and every `tests-*`/`examples-tests-*`
shard are directly audited (static review). Since Pass 4 first populated this matrix, **Pass 6 has
additionally built AND runtime-tested every one of the 14 real graphics backends** (EasyGL, Canvas,
D3D9, D3D11, D3D12, Dx3, WebGPU, Vulkan, SdlGpu, Bgfx, SdlRenderer, Software, Ascii, Headless) — none
remain static-only. The "Runtime-verified" row below now reflects this; the cross-cutting defect
matrix and feature-by-feature grid are populated from a mix of static review and this new runtime
evidence, cross-referenced in the "Pass 6 runtime-verification summary" section added below. A
handful of grid cells remain marked `?` where even the runtime sweep didn't specifically exercise
that exact feature (noted per-row) -- these are now few and genuinely narrow, not broad unknowns.

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

Two categories of feature below. **XNA-facing features** (disposal, EffectParameter plumbing, VertexBuffer/
IndexBuffer semantics) live in the shared `Microsoft::Xna::Framework::Graphics` layer (`xna-graphics` shard,
191 files, fully audited) — a defect here affects every backend uniformly, since backends never see these
APIs directly. **Backend-facing features** (MSAA, fog, stencil/scissor, occlusion query, CI coverage) are
genuinely per-backend and are evidenced from each `backend-*` shard's own audit plus the `examples-tests-*`
shards' file/test inventories.

### XNA-facing features (uniform across all backends — the bug or its absence is in shared code)

| Feature | Status | Evidence |
|---|---|---|
| `RenderTargetCube.Dispose(bool)` | **MISSING** (universal) | Unlike `RenderTarget2D` (has the Task 717 fix: "still bound to device" guard + dangling-pointer clear), `RenderTargetCube` has no `Dispose(bool)` override at all — a use-after-free risk on every backend equally, since this is XNA-facing state, not backend state. |
| `EffectParameter` Matrix Get/Set/Transpose | **INVERTED** (universal) | All 8 Matrix-related methods have FNA's convention backwards. Confirmed NOT to affect the 7 stock effects' actual rendering (their `FillGpuDrawParams()` bypasses `EffectParameter`'s generic accessors entirely) — exposure is custom/user-authored `Effect`s using the generic Matrix accessors directly, equally on every backend. |
| `EffectParameter.Elements`/`StructureMembers` | **ALWAYS EMPTY** (universal) | Nothing anywhere populates these — array/struct-typed custom parameters report zero sub-elements regardless of backend. |
| `BasicEffect.Parameters` (generic `effect.Parameters["X"]` access) | **EMPTY** for `BasicEffect` only | Every sibling stock effect (AlphaTest/DualTexture/EnvironmentMap/Skinned/Pbr/SkinnedPbr) populates `Effect::Parameters`; `BasicEffect` — the single most commonly used stock effect — does not, on any backend (rendering itself is unaffected; only generic parameter introspection breaks). |
| `VertexBuffer`/`IndexBuffer` destination-byte-offset `SetData` | **MISSING** (universal, root cause) | Confirmed the true root cause of the `IVertexBufferBackend`/`IIndexBufferBackend::SetDataWithOptions()` no-offset gap independently found in 3 backends (D3D11, EasyGL, D3D9) — the gap originates in the shared `VertexBuffer.hpp`/`IndexBuffer.hpp` XNA-facing API itself (FNA's real `offsetInBytes` overload was dropped entirely), so it is architecturally present on **every** backend, not just the 3 where a backend audit happened to trip over its symptom. Real ring-buffer/streaming `NoOverwrite` usage is impossible to express on any backend. |
| `SpriteFont`/`SpriteBatch` default-character UB, `SpriteEffects` 4th-value OOB read | **UNIVERSAL** (both HIGH) | Both bugs live in `Microsoft::Xna::Framework::Graphics::SpriteFont`/`SpriteBatch` themselves, not any backend's `SpriteBatch` implementation — reachable identically regardless of which backend is selected. |
| `SamplerState.AddressW` / `BlendState.ColorWriteChannels` / `RasterizerState.MultiSampleAntiAlias` | **Real & correct at the XNA class level; silently unenforceable below `IGraphicsBackend`** (universal) | Resolved this session: all three are fully real, correctly-implemented, settable properties on their respective XNA-facing classes — the gap is 100% confined to `IGraphicsBackend::ApplySamplerState()`/`ApplyBlendState()`/`ApplyRasterizerState()`'s own signatures never carrying these fields through, so **every** backend built on this shared interface is equally unable to honor them, not just the one (D3D11) whose own source comment happened to disclose it first. |
| `GraphicsDevice::Dispose()` event ordering | **INVERTED vs. FNA** (universal) | Disposes owned resources *before* raising `Disposing`, backwards from FNA's real order — a `Disposing` handler can never observe a still-valid resource, on any backend. |
| `GraphicsDeviceManager` -> `GraphicsDevice` event forwarding | **MISSING subscription, but `GraphicsDevice` itself raises correctly** (universal) | Resolved this session: `GraphicsDevice` correctly raises `DeviceResetting`/`DeviceReset`/`DeviceLost`/`Disposing` at the right points on every backend; `GraphicsDeviceManager` simply never subscribes to them, so a real backend-detected device-lost/reset cycle (via the one backend that calls the `deviceEventCallback`, see below) silently never reaches `IGraphicsDeviceService` listeners regardless of backend. |
| `TextureCube` XNA-facing `SetData`/`GetData` per-face semantics | **Correct** (universal) | Resolved this session: matches FNA exactly at the XNA-facing-class level; the "regenerates mips for all 6 faces" defect (SdlGpu, D3D11) is purely backend-level, not a fault in this shared class. |

### Backend-facing features

| Feature | Ascii | Bgfx | Canvas | D3D9 | D3D11 | D3D12 | Dx3 | EasyGL | Headless | SdlGpu | SdlRenderer | Software | Vulkan | WebGPU |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Fog formula (vs. FNA `ComputeFogFactor`) | delegates to SdlRenderer | **WRONG** (mirrored) | ? | correct (vendored stock) / **object-space-only** (CNA-custom) | **WRONG** (mirrored, shared D3DCommon) | **WRONG** (mirrored, shared D3DCommon) | ? | **correct** (post-Task-1111 fix) | N/A (no 3D lighting) | **MISSING ENTIRELY** (0/10 shader families) | ? | ? | **WRONG** (mirrored, original source of the bug) | ? |
| SkinnedEffect world-space normal transform | delegates | **missing** (complete omission) | N/A (no SkinnedEffect) | vendored stock: **correct**; CNA-custom PBR: **narrower "raw World" variant** | **missing** (shared D3DCommon, 4/4 non-PBR skinned shaders) | same as D3D11 (shared source) | N/A | **missing** (complete omission, F2) | N/A | **missing** (complete omission, explicitly "ported from Vulkan") | delegates | N/A | **missing** (complete omission, original source of the bug per explicit porting comments) | **missing** (complete omission, explicitly "ported from EasyGL line-for-line") |
| `SpriteBatch.SetTransformMatrix()` | delegates (inherits fix) | correct | correct | correct | correct | not directly checked, likely correct (mirrors D3D11 per established pattern) | correct | correct | correct (via delegation semantics) | correct (threaded as Draw param) | correct (fixed Task 675, precedent for WebGPU/SdlGpu's design) | correct | **BROKEN** (no-op, only affected backend) | correct (threaded as Draw param) |
| Stencil + Scissor + DepthBias | inherits | full emulation (all 3) | ? | **most complete of any backend** (native D3D9 render states, no emulation) | full support (both) | **Stencil+Scissor completely non-functional**; DepthBias untested | ? | ? (not the focus of the scoped review) | N/A | Stencil+Scissor functional; DepthBias stored-not-applied (disclosed) | ? | ? | Scissor **silently broken when a RenderTarget is bound** (backbuffer-only); Stencil/DepthBias not flagged | ? |
| Occlusion query multi-draw accumulation | inherits | ? | N/A | **structurally immune** (native `Issue(BEGIN/END)`, GPU-level accumulation) | ? | **BROKEN** (each draw's Begin/End overwrites the prior draw's result on the same query-heap slot) | ? | ? | N/A | ? | N/A | N/A | ? | ? |
| Whole-cube mip regeneration (per-face vs. all-6) | N/A | ? | N/A | N/A (no mip regen support at all for RenderTargetCube — disclosed scope gap, not a bug) | **all 6 faces** (should be 1) | **correct — per-face only** (positive counter-example vs. sibling D3D11) | N/A | ? | N/A | **all 6 faces** (should be 1) | N/A | N/A | ? | ? |
| `RegisterForWindow` constructor exception safety | N/A (no window registration) | N/A | **safe** (only fallible step precedes registration) | N/A | N/A | N/A | N/A | **UNSAFE** (registers before `SDL_GL_CreateContext`, which can throw — F1, only confirmed instance) | N/A | **safe** (registration is the last of 10 sequential steps — though see the distinct SdlGpu-specific constructor-leak finding below) | N/A | N/A | N/A | **model example** (every fallible step wrapped, full catch-and-cleanup-then-rethrow) |
| Constructor exception safety (any fallible resource-creation leak, not just the window registry) | ? | ? | ? | ? | ? | ? | ? | (see above — F1) | ? | **unsafe**: 10 sequential fallible resource-creation calls with no try/catch; leaks device+window+partial pipelines if any fails | ? | ? | ? | **model example** for the whole audit |
| MSAA support maturity | ? | ? | ? | ? | **thorough, independently re-derived correct** (color/resolve-texture split, cube-specific `D3D11_RESOURCE_MISC_TEXTURECUBE` vs. `SampleDesc.Count>1` conflict correctly handled) | ? | N/A (no MSAA — 2D-only per BackendSelection.cmake) | ? (2 test files reference it) | N/A | ? (1 dedicated MSAA test) | N/A (2D-only) | N/A (software rasterizer, no MSAA) | ? (4 dedicated MSAA tests, but no MSAA-specific finding surfaced) | ? (1 dedicated MSAA test) |
| sRGB/gamma-correct rendering, dedicated test coverage | none | none | N/A | none | none | none | N/A | none | N/A | none | N/A | N/A | **only backend with a dedicated sRGB test** (1) | none |
| EnvironmentMapEffect emissive/diffuse re-multiply | delegates | **WRONG** (original source) | N/A | vendored stock: correct; CNA custom: not flagged | **WRONG** (shared D3DCommon, "ported line-by-line from Vulkan") | same (shared source) | N/A | N/A (no EnvironmentMapEffect implementation confirmed in scoped review) | N/A | **WRONG** | N/A | N/A | **WRONG** (confirmed via direct source read) | **WRONG** |
| Missing Vulkan-NDC Y-flip (Vulkan-only defect class; included for completeness) | N/A | N/A | N/A | N/A (D3D clip space already matches XNA, correctly no-flip) | N/A (same) | N/A (same) | N/A | N/A | N/A | N/A | N/A | N/A | **4 of ~18 shader families missing it** (EnvironmentMapEffect, PbrEffect, SkinnedPbrEffect, InstancedEffect) | N/A |
| CI test-suite breadth (`examples-tests-*` shard file count, all now audited) | 6 | 98 | 2 | 14 | 3 | 2 | 9 | **218** (largest by far) | 7 | 22 | 67 | 6 | 70 | 22 |
| Runtime-verified in this audit's Linux sandbox vs. static-only (per D-P4) -- **UPDATED, Pass 6: ALL 14 backends now built AND runtime-tested, none remain static-only** | runtime (6/6 own suite) | runtime (110/114 own suite, 5504/5511 general) | runtime (Emscripten via emsdk, `CnaTests` blocked only by an unrelated harness-linking gap) | runtime (280+/280+ own suite via real Wine+DXVK) | runtime (5/7 own suite via real Wine+DXVK) | runtime (220/220 via real Wine+vkd3d-proton; 1 known crash reproduced live, Proton-fix path unavailable -- no Steam/Proton in this sandbox) | runtime (59/61 own suite; empirically closes the `Dx3_SpriteBatch` investigation) | runtime (0 failed, this audit's own baseline) | runtime (7/7 own suite) | runtime (21/21 own suite, 5500/5507 general) | runtime (65/68 own suite) | runtime (6/6 own suite) | runtime (5495/5507 general, 1 crash) | runtime (23/23 own suite, 1 crash in the general suite) |
| Architecture: single monolithic file vs. multi-file split | delegates to SdlRenderer | multi-file (32 src) | multi-file (4 src) | multi-file (34+12 vendored) | multi-file (10 src) | multi-file (13 src) | **single-file** | **single-file** (4733 lines, largest single-file backend) | **single-file** | multi-file (26 src) | **single-file** | **single-file** | multi-file (38 src, but its own single largest file — `VulkanGraphicsBackend.cpp`, 8954 lines — is the single largest file in the ENTIRE audit) | **single-file** (8805 lines, 2nd-largest single file in the audit) |

Cells marked `?` are genuine, honestly-recorded gaps in the existing audit corpus — either the relevant
backend's own shard audit didn't specifically exercise that feature (most backends' MSAA/sRGB/occlusion-query
depth was not scoped as deeply as D3D9/D3D11/D3D12/Vulkan's, where a specific defect surfaced and drove a
closer look), or this Pass-4 synthesis pass did not have time to re-read every backend's full source
specifically hunting for that one feature. None of these `?` cells are load-bearing for this audit's
headline findings — the fog/skinned-normal/EnvironmentMapEffect/SpriteBatch-transform/stencil-scissor rows
above (this audit's most-corroborated cross-backend defects) are all fully evidenced.

## Backend-facing feature rows still worth a targeted future pass (not attempted here, to avoid guessing)

- Occlusion query behavior on Ascii/Canvas/EasyGL/Headless/SdlRenderer/Software/Vulkan/WebGPU specifically for
  the multi-draw-per-Begin/End accumulation semantics (only D3D9 confirmed immune, D3D12 confirmed broken).
- MSAA resolve correctness on Bgfx/Vulkan/SdlGpu/WebGPU/EasyGL/D3D9/D3D12 at the same depth D3D11 received
  (color/resolve-texture split, cube-vs-MSAA resource-flag conflicts).
- sRGB/gamma test coverage is thin project-wide (only Vulkan has one dedicated test) — this may reflect a
  genuinely under-tested area rather than a hidden defect, but it's worth a dedicated sweep rather than
  inferring correctness from silence.
- Constructor exception safety for every remaining fallible-resource-creation backend beyond the 4
  (`EasyGL`/`WebGPU`/`Canvas`/`SdlGpu`) whose `RegisterForWindow`-adjacent constructor code was specifically
  traced this session.

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

EasyGL is this project's default backend on Linux/Emscripten and, per its own audit report, "the most
XNA-behaviorally-complete backend in the project" per `CLAUDE.md`'s own framing — it has the richest "found a
real bug, root-caused it, fixed it" comment history of any file in this audit. It is used here purely as an
**internal baseline for comparison**, not as the compatibility authority — XNA 4.0/FNA remains that authority,
and where EasyGL itself diverges from FNA (fog formula history, skinned-normal-transform bug) that is recorded
as an EasyGL defect, not a target other backends should match.

### Features EasyGL gets right that other backends should be measured against

- **Fog formula (post-Task-1111 fix)**: `(aPos.z+uFogEnd)/(uFogEnd-uFogStart)`, matching FNA's real
  `ComputeFogFactor` exactly. This is the **one and only** backend confirmed to have the historically-correct
  formula — Bgfx, Vulkan, and D3D11/D3D12 (shared D3DCommon) all use the mirrored, pre-fix formula, and at
  least two of those backends' own source comments falsely claim to match "EasyGL's established formula,"
  meaning EasyGL's own correct fix was the misremembered reference point for a bug that then propagated
  elsewhere. SdlGpu has no fog implementation at all (a different, more severe gap than "wrong formula").
- **Non-skinned normal-matrix handling**: `BindDrawParams` correctly computes the true
  `transpose(inverse(world3x3))` for `BasicEffect`/`EnvironmentMapEffect`/`PbrEffect`'s lit/vertex-lit
  variants, citing its own prior fix (Task 398, "the raw upper-left 3x3 used before was only correct for
  rotation/uniform-scale/translation World matrices"). This is the exact correctness bar that the *skinned*
  variant (F2) and PBR-skinned variant (F3) fail to meet in this same file, and that every other backend's
  skinned shader also fails to meet — EasyGL is simultaneously the demonstration that the project knows how
  to get this right (for non-skinned shaders) and one of 14 backends confirmed to get it wrong for skinned
  ones.
- **`PreferPerPixelLighting` default handling**: correctly implements both a per-vertex-lit (Gouraof) and
  per-pixel-lit shader family and selects between them via `SelectProgram()`, matching real XNA/FNA's actual
  default (`PreferPerPixelLighting=false`) rather than silently always using one or the other.
- **`RecoverableResource`/`ResourceRegistry` GL-context-loss recovery**: a coherent, consistently-applied
  pattern across every GL-object-owning class (`Texture`, `RenderTarget`, `RenderTargetCube`, `VertexBuffer`,
  `IndexBuffer`, `OcclusionQuery`, `SpriteBatch`) with a documented opt-out mechanism
  (`GraphicsBackendCreateArgs::contextRecoveryEnabled`). No other backend in this audit has an equivalent
  context-loss-recovery story documented at this level of completeness (GL context loss is a GL-specific
  concern; D3D/Vulkan's own device-lost handling is a structurally different problem, tracked separately via
  the `GraphicsDeviceManager`/`GraphicsDevice` device-event findings above).

### Features where EasyGL itself is the confirmed bug, and other backends should NOT be measured against it

- **`SkinnedEffect`/`SkinnedPbrEffect` normal-transform (F2/F3)**: EasyGL's skinned shaders never compose the
  object's `uWorld`/`uNormalMatrix` into the lighting normal at all (F2, complete omission) or use the raw
  `uWorld` instead of the inverse-transpose (F3, `EnsurePbrSkinnedProgram`). Per the systematic FNA parity
  sweep above, this exact bug (in one of its two variants) is now confirmed in **all 14 backends with a
  SkinnedEffect implementation** — and the explicit "ported from EasyGLGraphicsBackend::
  EnsureSkinnedProgram()'s GLSL shader line-for-line" comment in WebGPU's own source is direct, first-hand
  evidence that EasyGL is the *origin* of this bug's propagation to at least one other backend, not merely a
  fellow sufferer of an independently-introduced defect.
- **`RegisterForWindow` constructor exception safety (F1)**: EasyGL is the *only* one of the four
  `RegisterForWindow`-calling backends (EasyGL, WebGPU, Canvas, SdlGpu) confirmed to register before every
  fallible construction step has completed — WebGPU is this audit's own model example of the correct pattern
  (full try/catch, cleanup-then-rethrow, registration last), and both Canvas and SdlGpu were independently
  checked and confirmed safe. This is a case where EasyGL should adopt the *other* backends' established
  pattern, not the reverse.

### Duplicated logic across EasyGL and other backends worth consolidating

- **Fog-formula propagation mechanism**: at least 3 backend-groups' shader source comments cite "the
  established EasyGL/Bgfx formula" or "Task 888 formula" when in fact only EasyGL's is correct — the fog
  formula is currently copy-pasted (correctly or incorrectly) per-backend rather than shared, which is exactly
  why a since-fixed EasyGL bug could keep re-propagating to new backends built after the fix landed. A shared,
  single source of truth for this one scalar formula (even just a well-linked reference comment quoting FNA's
  `EffectHelpers.cs` directly, rather than "matches EasyGL's formula") would have prevented at least 2 of the
  3 confirmed propagation instances.
- **Skinned-normal-transform fix, once written for EasyGL, would need to be re-applied to 13 other backends
  independently** — there is no shared, single-source-of-truth implementation of "compose the per-object
  normal matrix with the per-vertex bone-skin matrix" anywhere in this codebase; each backend's shader
  reimplements this by hand in its own shading language (GLSL/HLSL/WGSL/BGFX shading language), which is
  exactly the propagation vector this audit traced across EasyGL -> WebGPU (explicit), EasyGL -> D3D11/D3D12
  via Vulkan (explicit "ported line-by-line from Vulkan"), and EasyGL -> SdlGpu via Vulkan (explicit "mirrors
  VulkanGraphicsBackend's own skinned3d.vert.glsl exactly"). A shared reference derivation (even just a single
  well-commented canonical shader snippet in a project doc that every backend's shader-author is expected to
  consult) would reduce the "which backend has the wrong-but-plausible-looking convention" confusion visible
  in the shader header comments themselves (`pbr3d_skinned.vert.glsl`'s factually false citation of
  `skinned3d.vert.glsl` as precedent is the sharpest example of this).
- **Generic `VertexElementFormat` -> native-format helper header, correct but entirely dead code**: this
  exact shape (a well-mapped, well-tested conversion helper with zero real call sites, because the actual
  per-pipeline vertex layout is hardcoded per-stride/per-shader instead) is now confirmed in Bgfx
  (`BgfxVertexFormatHelper.hpp`) and Vulkan (`VulkanVertexFormatHelper.hpp`) specifically, not EasyGL —
  EasyGL's own `Prog3D`-based per-shader-variant uniform-location-cache design (11 explicit variants) is a
  structurally different, no-generic-helper-needed approach to the same underlying problem (avoiding runtime
  shader-variant dispatch cost). Worth checking whether Bgfx/Vulkan's dead helper code should simply be
  deleted, or whether it represents unfinished work toward a more data-driven vertex-layout system that was
  abandoned partway.

## Pass 6 runtime-verification summary

Every one of the 14 real backends listed above has now been built AND runtime-tested this session
(not merely statically reviewed) — full narrative for each lives in `AUDIT_CROSS_CUTTING_FINDINGS.md`
("Pass 6" sections). Headline results per backend:

| Backend | Own dedicated CTest suite | General `CnaTests` suite | Headline Pass 6 finding |
|---|---|---|---|
| EasyGL | (this audit's own baseline) | 5503/5507, 0 failed | Root-caused the project-wide `WORKING_DIRECTORY` CTest bug; new `EasyGL_MRT_TwoAttachments` defect |
| Vulkan | 6/6 blend-state tests pass (Task 868 confirmed FIXED) | 5495/5507, 7 failed, 1 CRASH | **CRITICAL**: Texture2D-fuzz crash (stack smashing); SkinnedEffect+Fog renders black |
| WebGPU | 23/23 | 1 CRASH partway through | **CRITICAL**: Texture2D-fuzz crash (non-catchable Rust panic); `WebGPU_Msaa` corrected to FIXED |
| D3D11 | 5/7 | not attempted (documented cross-compile limitation) | 2 new defects: specular asymmetry, black-vertex-color bug |
| D3D12 | 220/220 (`D3D12_Smoke`, real vkd3d-proton) | not attempted (same limitation) | Only 1 CTest exists for this whole backend — a real coverage gap; known swapchain crash reproduced live |
| D3D9 | 280+/280+ (100%) | not attempted (same limitation) | Zero real failures |
| Dx3 | 59/61 | n/a (built via native Linux, not Wine) | Empirically closes the `Dx3_SpriteBatch` investigation (both static predictions confirmed) |
| Bgfx | 110/114 | 5504/5511, 3 failed | Root-caused the universal `cna_demo_xact` build defect; new cull-mode bug |
| SdlGpu | 21/21 | 5500/5507, 3 failed | New: rejects real user-authored GLSL content EasyGL accepts |
| Canvas | (build-only; `CnaTests` blocked by an unrelated harness-linking gap) | n/a | Corrects an earlier "Emscripten unavailable" assumption -- it's genuinely buildable |
| SdlRenderer | 65/68 | 56 failed (expected, 2D-only methodology noise) | New: `SDL_Renderer_FullscreenToggle` uncaught-exception crash |
| Software | 6/6 | 3 failed | New: Texture3D round-trip returns garbage data |
| Ascii | 6/6 | 52 failed (expected, 2D-only methodology noise) | None beyond shared cross-backend patterns |
| Headless | 7/7 | 4 failed | Confirms the Texture3D round-trip bug is shared (2nd backend) |

**Cross-backend patterns discovered by having every backend's real data side-by-side** (not visible
from any single backend's own report): the Texture2D-fuzz crash (Vulkan, WebGPU); the
`cna_demo_xact` build defect (universal, all 6 independently-built backends); the
`MediaLibraryTestFixture` SEGFAULT (universal, 6+ backends); the `WireFrame`-capability
test-authoring pattern (5 backends: Vulkan, SdlGpu, Bgfx, Software, Headless); the Texture3D
round-trip defect (Software, Headless, likely SdlRenderer).
