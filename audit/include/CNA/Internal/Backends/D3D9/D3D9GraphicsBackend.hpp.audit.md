# Audit: include/CNA/Internal/Backends/D3D9/D3D9GraphicsBackend.hpp

## Metadata

- Source file: `include/CNA/Internal/Backends/D3D9/D3D9GraphicsBackend.hpp`
- Audit status: AUDITED
- Subsystem: `backend-d3d9` shard
- File type: C++ header
- XNA/FNA relevance: Declares D3D9GraphicsBackend: the full IGraphicsBackend implementation over plain Direct3D 9.
- Graphics backend relevance: D3D9 backend (Windows-only, static-analysis-only from this Linux sandbox, per
  the D3D11/D3D12 precedent already established this session)
- Main related tests: `examples-tests-d3d9` (already audited via mechanical batch earlier this session)

## Purpose

Declares D3D9GraphicsBackend: the full IGraphicsBackend implementation over plain Direct3D 9.

## Executive Verdict

Needs attention, scoped-depth review (533 lines, fully read) — no defects found in the header itself; confirms several already-recorded cross-cutting patterns and 1 genuine positive architectural stance.

## Checklist Results

### Behavioral correctness / FNA parity / C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness / Testing
**No `RegisterForWindow` call anywhere** (confirmed via grep) — cannot share the EasyGL-class dangling-window-registry-pointer bug, matching D3D11/D3D12/Bgfx/Vulkan's own confirmed absence. Class-level doc comment states this backend's goal is "pixel-for-pixel indistinguishability from the original XNA 4.0 runtime, not mere feature parity" — a stricter authenticity bar than D3D11/D3D12's own stated "parity, not authenticity" goal, and consistent with several genuinely more-authentic behaviors confirmed in the .cpp (throwing on over-requested MRT count rather than silently degrading, matching real D3D9CAPS9 for Reach/HiDef profile gating). A candid, empirically-found bug is documented in-header (D9-53's cube-face unbind fix, found via `D3D9_Smoke` Check T) — the IGraphicsBackend default unbind path would have left the device pointed at a stale cube-face surface; this backend explicitly overrides it with its own `currentCustomCubeRT_` tracking.

## Detailed Findings

**No `RegisterForWindow` call anywhere** (confirmed via grep) — cannot share the EasyGL-class dangling-window-registry-pointer bug, matching D3D11/D3D12/Bgfx/Vulkan's own confirmed absence. Class-level doc comment states this backend's goal is "pixel-for-pixel indistinguishability from the original XNA 4.0 runtime, not mere feature parity" — a stricter authenticity bar than D3D11/D3D12's own stated "parity, not authenticity" goal, and consistent with several genuinely more-authentic behaviors confirmed in the .cpp (throwing on over-requested MRT count rather than silently degrading, matching real D3D9CAPS9 for Reach/HiDef profile gating). A candid, empirically-found bug is documented in-header (D9-53's cube-face unbind fix, found via `D3D9_Smoke` Check T) — the IGraphicsBackend default unbind path would have left the device pointed at a stale cube-face surface; this backend explicitly overrides it with its own `currentCustomCubeRT_` tracking.

## Cross-File Observations

None beyond what is already recorded in `AUDIT_CROSS_CUTTING_FINDINGS.md`.

## Missing or Weak Tests

No dedicated gap identified beyond what's already recorded cross-cuttingly.

## Positive Findings

A stricter, more XNA-authentic design goal than its D3D11/D3D12 siblings, backed by concrete examples (loud MRT over-request error, real Reach/HiDef profile enforcement); confirmed absence of the EasyGL-class window-registry bug; a candidly documented empirically-found bug-fix.

## Final Assessment

See `AUDIT_CROSS_CUTTING_FINDINGS.md` for any cross-cutting defects this file instantiates or corroborates; no
other file-local defects found beyond what is stated above.
