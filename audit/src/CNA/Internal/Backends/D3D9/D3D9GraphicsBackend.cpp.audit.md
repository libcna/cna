# Audit: src/CNA/Internal/Backends/D3D9/D3D9GraphicsBackend.cpp

## Metadata

- Source file: `src/CNA/Internal/Backends/D3D9/D3D9GraphicsBackend.cpp`
- Audit status: AUDITED
- Subsystem: `backend-d3d9` shard
- File type: C++ implementation
- XNA/FNA relevance: Implements D3D9GraphicsBackend: device/present/device-lost lifecycle, Clear*, Apply*State, resize, resource creation dispatch.
- Graphics backend relevance: D3D9 backend (Windows-only, static-analysis-only from this Linux sandbox, per
  the D3D11/D3D12 precedent already established this session)
- Main related tests: `examples-tests-d3d9` (already audited via mechanical batch earlier this session)

## Purpose

Implements D3D9GraphicsBackend: device/present/device-lost lifecycle, Clear*, Apply*State, resize, resource creation dispatch.

## Executive Verdict

Needs attention, scoped-depth review (1112 lines, priority sections fully read: constructor, ApplyDepthStencilState/ApplyRasterizerState/SetScissorRect) — genuinely strong positive findings; no new defects found in the areas read.

## Checklist Results

### Behavioral correctness / FNA parity / C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness / Testing
**Stencil, Scissor, and DepthBias are all genuinely, natively, and completely functional** (lines 985-1043) via direct D3D9 render states (`D3DRS_STENCILENABLE`/`D3DRS_SCISSORTESTENABLE`/`D3DRS_DEPTHBIAS`/`D3DRS_SLOPESCALEDEPTHBIAS`) — no faked/emulated mechanism needed at all, unlike Bgfx's vertex-shader Z-offset emulation or SdlGpu's/Vulkan's own more involved implementations. This makes D3D9 architecturally the simplest AND most complete backend checked in this audit for this specific combination, a direct consequence of D3D9's fixed-function-adjacent render-state model mapping 1:1 onto XNA's own DepthStencilState/RasterizerState fields — contrast with D3D12's confirmed complete non-functionality of both Stencil and Scissor. **`SetScissorRect()` cannot share Vulkan's own confirmed scissor-ignored-when-RT-bound bug**: D3D9's rendering model is immediate (`device_->SetScissorRect()` calls the driver directly), not deferred/recorded like Vulkan's command-buffer model — there is no "RT-pass loop" for a bug of that shape to hide in. Constructor (line 100) correctly throws on missing window/HWND before any device object is created, and `ComPtr::Attach()` (not assignment) is used for `Direct3DCreate9()`'s already-AddRef'd return, avoiding a double-AddRef leak.

## Detailed Findings

**Stencil, Scissor, and DepthBias are all genuinely, natively, and completely functional** (lines 985-1043) via direct D3D9 render states (`D3DRS_STENCILENABLE`/`D3DRS_SCISSORTESTENABLE`/`D3DRS_DEPTHBIAS`/`D3DRS_SLOPESCALEDEPTHBIAS`) — no faked/emulated mechanism needed at all, unlike Bgfx's vertex-shader Z-offset emulation or SdlGpu's/Vulkan's own more involved implementations. This makes D3D9 architecturally the simplest AND most complete backend checked in this audit for this specific combination, a direct consequence of D3D9's fixed-function-adjacent render-state model mapping 1:1 onto XNA's own DepthStencilState/RasterizerState fields — contrast with D3D12's confirmed complete non-functionality of both Stencil and Scissor. **`SetScissorRect()` cannot share Vulkan's own confirmed scissor-ignored-when-RT-bound bug**: D3D9's rendering model is immediate (`device_->SetScissorRect()` calls the driver directly), not deferred/recorded like Vulkan's command-buffer model — there is no "RT-pass loop" for a bug of that shape to hide in. Constructor (line 100) correctly throws on missing window/HWND before any device object is created, and `ComPtr::Attach()` (not assignment) is used for `Direct3DCreate9()`'s already-AddRef'd return, avoiding a double-AddRef leak.

## Cross-File Observations

Directly contrasts with D3D12's confirmed complete non-functionality of Stencil/Scissor, and with Vulkan's confirmed Scissor-when-RT-bound gap — both structural consequences of those backends' pipeline-baked-state/deferred-recording architectures that D3D9's simpler, immediate model simply does not share.

## Missing or Weak Tests

No dedicated gap identified beyond what's already recorded cross-cuttingly.

## Positive Findings

The most architecturally simple AND complete Stencil+Scissor+DepthBias implementation of any backend checked in this audit; structurally immune to Vulkan's own RT-bound-scissor bug by virtue of its immediate (non-deferred) rendering model; correct ComPtr::Attach adoption avoiding a double-AddRef leak.

## Final Assessment

See `AUDIT_CROSS_CUTTING_FINDINGS.md` for any cross-cutting defects this file instantiates or corroborates; no
other file-local defects found beyond what is stated above.
