# Audit: src/CNA/Internal/Backends/SdlGpu/SdlGpuGraphicsBackend.cpp

## Metadata

- Source file: `src/CNA/Internal/Backends/SdlGpu/SdlGpuGraphicsBackend.cpp`
- Audit status: AUDITED (scoped-depth review — see below; matches the standard already applied to this audit's
  other largest files: EasyGL 4733 lines, WebGPU 8805 lines, D3D11 1846 lines, D3D12 2331 lines)
- Subsystem: `backend-sdlgpu` shard
- File type: C++ implementation, 5105 lines — the 3rd-largest single file in this audit
- Related header: `include/CNA/Internal/Backends/SdlGpu/SdlGpuGraphicsBackend.hpp` (same shard)
- XNA/FNA relevance: implements the entire SDL_GPU (Vulkan-backed via SPIR-V) backend
- Graphics backend relevance: this is the backend's single, monolithic class
- FNA reference: N/A directly (SDL_GPU postdates FNA); compared against this project's own established
  conventions
- Main related tests: `examples-tests-sdlgpu` (22 files, already audited via mechanical batch this session)

## Purpose

Implements device/window-claim construction and symmetric teardown, all 10 stock-effect resource-group
create/destroy pairs, `Apply*State`/`SetScissorRect`/`ApplySamplerState`, `CaptureRenderState()`-driven pipeline
selection, and the full draw-dispatch surface.

## Executive Verdict

**Needs attention, scoped-depth review.** Constructor (confirms the HIGH-severity resource-leak finding already
recorded), destructor (confirms symmetric teardown), `RegisterForWindow` ordering, `SetTransformMatrix`,
`ApplyDepthStencilState`/`ApplyRasterizerState`'s stencil/scissor tracking (confirmed genuinely functional,
unlike D3D12's confirmed non-functional equivalent), and the skinned/PBR ambient-vs-emissive constant-buffer fill
were all read and verified. The remaining ~4500 lines (full non-skinned draw-dispatch variants, device-lost
recovery, resize handling, the 10 individual `Create*Resources()` pipeline-setup bodies) were not exhaustively
traced.

## Checklist Results

### Confirmed, formalized: constructor resource-leak risk (HIGH)
Constructor (lines 487-543): `SDL_ClaimWindowForGPUDevice`'s failure is correctly handled with explicit
cleanup-then-rethrow (lines 513-519), but `SetSwapInterval()`/`QueryDepthStencilFormat()`/10 sequential
`Create*Resources()` calls (lines 521-531) are entirely unwrapped by any try/catch. `RegisterForWindow()` is
correctly called LAST (line 539) — confirming this backend does NOT share the EasyGL dangling-pointer bug, but
DOES share a distinct, narrower resource-leak risk if any of those ~12 calls throws. See the paired header's
report and `AUDIT_CROSS_CUTTING_FINDINGS.md` for full detail.

### Confirmed: destructor symmetry
Destructor (lines 545-570) correctly, symmetrically tears down every resource group the constructor creates (10
`Destroy*Resources()` calls in reverse order, plus deferred-texture-release cleanup, plus device/window release)
— confirms exactly what would be lost if the constructor threw partway through: this is real, substantial,
working teardown logic that simply never gets a chance to run on a failed construction.

### Systematic FNA parity gaps — POSITIVE COUNTER-EXAMPLE to D3D12's confirmed gap
**`ApplyDepthStencilState()`/`ApplyRasterizerState()` (lines 1145-1183) correctly track ALL stencil fields and
`scissorTestEnable`** into `stencilParams_`/`scissorEnabled_` member state, and both are confirmed **genuinely
consumed**, not just stored-and-ignored: `scissorEnabled_` gates a real `SDL_SetGPUScissor()` call (line
1213/1230), and `stencilParams_` is threaded into `CaptureRenderState()`'s `RenderStateSnapshot` (line 1240),
which (by its name and structure) drives real pipeline selection — **this backend does NOT share D3D12's
confirmed HIGH-severity Stencil/Scissor-non-functional gap.** One narrower, honestly-disclosed gap: `depthBias_`/
`slopeScaleDepthBias_` are "stored but deliberately not yet applied" (the method's own comment: SDL_GPU has no
per-draw-dynamic depth-bias equivalent to Vulkan's `vkCmdSetDepthBias`) — a real, narrower, disclosed gap, not a
silent one.

### API / FNA parity — SkinnedEffect ambient/emissive
Confirmed the skinned constant-buffer-fill code (lines ~314, ~334) sets both `ambientColor` and `emissiveColor`
for at least one draw path — **worth a closer confirming pass on whether this specific fill applies to the
`SkinnedEffect` (non-PBR) path or a different effect family**, since this audit's cross-cutting finding is that
`SkinnedEffect`'s `EmissiveColor` is dropped on D3D11/D3D12/Vulkan specifically (each for its own reason) — this
scoped-depth pass did not fully trace which effect variant lines 314/334 belong to; flagged as an open item for a
future, more exhaustive pass on this file rather than asserted either way.

### C++ correctness / Memory/resource lifetime (beyond the constructor leak) / Performance / Thread safety / Portability / Maintainability / Robustness
No issues found in the areas read.

## Detailed Findings

**F1 (HIGH):** constructor resource-leak risk — see above and `AUDIT_CROSS_CUTTING_FINDINGS.md`.
**F2 (open item, not a confirmed finding):** whether `SkinnedEffect`'s `EmissiveColor` reaches the constant buffer
on this backend was not conclusively traced in this pass — flagged for a future check.

## Cross-File Observations

Corroborates (does not introduce) the already-recorded shader-level findings from the `examples-tests-sdlgpu`
batch: `skinned3d.vert.glsl`/`skinned_colored3d.vert.glsl`/`pbr_skinned3d.vert.glsl` share the cross-cutting
skinned-normal-transform bug; `env_map3d.frag.glsl` shares the `EnvironmentMapEffect` emissive-remultiply bug.

## Missing or Weak Tests

No test found exercising a constructor-failure scenario (F1), or `StencilState`/`ScissorRectangle` behavior
specifically on this backend (though the C++-level tracking is confirmed genuinely functional, unlike D3D12).

## Positive Findings

**Genuinely functional Stencil + Scissor support**, in contrast to D3D12's confirmed HIGH-severity non-functional
equivalent — a valuable positive cross-backend finding. Correct destructor symmetry; correct
`RegisterForWindow()` ordering avoiding the more severe EasyGL-class bug.

## Final Assessment

One HIGH-severity, now-formally-confirmed finding (F1, constructor resource leak); one open item for future
tracing (F2); a genuine positive cross-backend finding (functional Stencil/Scissor, unlike D3D12). The untraced
~4500 lines remain a gap for a future, more exhaustive pass.
