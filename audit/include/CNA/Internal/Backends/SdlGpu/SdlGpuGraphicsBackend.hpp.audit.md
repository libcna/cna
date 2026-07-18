# Audit: include/CNA/Internal/Backends/SdlGpu/SdlGpuGraphicsBackend.hpp

## Metadata

- Source file: `include/CNA/Internal/Backends/SdlGpu/SdlGpuGraphicsBackend.hpp`
- Audit status: AUDITED (scoped-depth review — see below; matches the standard already applied to this audit's
  other largest backend files: EasyGL 4733 lines, WebGPU 8805 lines, D3D11 1846+379 lines, D3D12 2331+713 lines)
- Subsystem: `backend-sdlgpu` shard (monolithic — only 3 files total: this header, one 5105-line `.cpp`, and one
  1942-line generated shader header)
- File type: C++ header, 1578 lines
- Related implementation: `src/CNA/Internal/Backends/SdlGpu/SdlGpuGraphicsBackend.cpp` (same shard)
- XNA/FNA relevance: implements the full `IGraphicsBackend` contract for SDL3's GPU API (SDL_GPU, Vulkan-backed
  on Linux via SPIR-V)
- Graphics backend relevance: this is the backend's single, monolithic class
- FNA reference: FNA's own SDL_GPU-equivalent conventions do not exist (FNA predates SDL_GPU); this backend's
  design is compared against this project's own EasyGL/Vulkan/D3D11 precedents instead
- Main related tests: `examples-tests-sdlgpu` (22 files, already audited via mechanical batch this session —
  see `AUDIT_CROSS_CUTTING_FINDINGS.md` and the individual `examples/sdlgpu_*.audit.md` reports for the bugs
  already found: skinned-normal-transform, `EnvironmentMapEffect` emissive-remultiply, and the constructor
  resource-leak risk this file's own audit formalizes)

## Purpose

Declares `SdlGpuGraphicsBackend`: device/window-claim lifecycle, 10 distinct stock-effect pipeline resource
groups (sprite/colored/textured/lit-textured/alpha-test/dual-texture/env-map/skinned/PBR — each with its own
`Create*Resources()`/`Destroy*Resources()` pair), swap-chain/present handling, and the full `IGraphicsBackend`
draw-dispatch surface.

## Executive Verdict

**Needs attention — a real, previously-identified (via the `examples-tests-sdlgpu` mechanical batch this
session) HIGH-severity constructor resource-leak risk, now formally confirmed with exact line references; the
declarations otherwise correctly mirror this project's established multi-backend conventions.**

## Checklist Results

### Confirmed, formalized: constructor resource-leak risk
**F1 (HIGH — already recorded in `AUDIT_CROSS_CUTTING_FINDINGS.md`, now formally confirmed against the actual
constructor):** `SdlGpuGraphicsBackend`'s constructor (`SdlGpuGraphicsBackend.cpp:487-543`) correctly wraps the
one genuinely fallible early step (`SDL_ClaimWindowForGPUDevice`) with explicit cleanup-then-rethrow (lines
513-519), but the subsequent `SetSwapInterval()`/`QueryDepthStencilFormat()`/10 sequential `Create*Resources()`
calls (lines 521-531 — these compile SPIR-V shaders and create GPU pipeline objects, a plausible, reachable
failure mode, and the constructor's own surrounding code acknowledges non-Linux shader-format support is still
incomplete) are entirely unwrapped by any try/catch. If any of these calls throws, the destructor (which
correctly, symmetrically tears down every one of these resource groups plus the device/window claim — verified
in the `.cpp` report) never runs, since a throwing constructor leaves the object never-fully-constructed. Result:
the SDL GPU device, the claimed window, and any GPU pipelines/shaders successfully created by earlier calls in
the sequence all leak. **Contrast with `WebGPUGraphicsBackend`'s constructor** (this audit's model example of
correct exception safety), which wraps the equivalent sequence in a full try/catch with cleanup-then-rethrow.
`IGraphicsBackend::RegisterForWindow()` is correctly called LAST (line 539), after every fallible step — so this
backend does **not** share the separate, already-confirmed EasyGL dangling-window-registry-pointer bug; this is
a distinct, narrower (constructor-scoped resource leak, not a subsequent dangling-pointer dereference) risk.

### Architecture
The one-`Create*`/`Destroy*` pair per stock-effect-family design is consistent with this project's established
per-effect-family resource-grouping convention already seen in D3D11/D3D12's own constant-buffer-per-variant
approach, just organized as member-function pairs instead of separate cache classes given this backend's
monolithic single-class design.

### C++ correctness / Memory/resource lifetime (beyond F1) / Performance / Thread safety / Portability / Maintainability / Robustness / Testing
No issues found in the declarations themselves, beyond F1.

## Detailed Findings

**F1 (HIGH):** constructor resource-leak risk on any of ~12 sequential fallible calls (`SetSwapInterval`,
`QueryDepthStencilFormat`, 10× `Create*Resources()`) — see above and `AUDIT_CROSS_CUTTING_FINDINGS.md`.

## Cross-File Observations

The already-completed `examples-tests-sdlgpu` mechanical batch (this session) independently confirmed, via
direct shader-source reading, that this backend's own `skinned3d.vert.glsl`/`skinned_colored3d.vert.glsl`/
`pbr_skinned3d.vert.glsl` share the cross-cutting skinned-normal-transform bug, and `env_map3d.frag.glsl` shares
the cross-cutting `EnvironmentMapEffect` emissive-remultiply bug — both already recorded in
`AUDIT_CROSS_CUTTING_FINDINGS.md`, not re-derived here.

## Missing or Weak Tests

No test found (in this session's `examples-tests-sdlgpu` batch or elsewhere) exercising a genuine
constructor-failure scenario (e.g. simulating a `Create*Resources()` failure) that would surface F1.

## Positive Findings

`RegisterForWindow()`'s correct placement (last, after every fallible step) avoids the more severe EasyGL-class
dangling-pointer bug even though the narrower resource-leak risk (F1) remains.

## Final Assessment

One HIGH-severity, now-formally-confirmed finding (F1, constructor resource leak) — otherwise consistent with
this project's established multi-backend architectural conventions.
