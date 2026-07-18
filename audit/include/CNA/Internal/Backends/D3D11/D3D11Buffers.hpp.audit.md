# Audit: include/CNA/Internal/Backends/D3D11/D3D11Buffers.hpp

## Metadata

- Source file: `include/CNA/Internal/Backends/D3D11/D3D11Buffers.hpp`
- Audit status: AUDITED
- Subsystem: `backend-d3d11` shard
- File type: C++ header (95 lines)
- Related implementation: `src/CNA/Internal/Backends/D3D11/D3D11Buffers.cpp` (same shard)
- XNA/FNA relevance: `VertexBuffer`/`IndexBuffer`/`DynamicVertexBuffer`/`DynamicIndexBuffer` backend contracts
- Graphics backend relevance: D3D11-specific vertex/index buffer implementation
- FNA reference: FNA's own D3D11 buffer streaming conventions (behavioral reference)
- Main related tests: `examples-tests-d3d11` (not yet audited)

## Purpose

Declares `D3D11VertexBufferBackend`/`D3D11IndexBufferBackend`, both `D3D11_USAGE_DYNAMIC` +
`D3D11_CPU_ACCESS_WRITE`, lazily-sized-and-grown via Map/Unmap.

## Executive Verdict

**Needs attention — one confirmed, plausible (not reproduced) architecture-level synchronization risk affecting
`SetDataOptions::NoOverwrite`, shared with at least one other backend (EasyGL).**

## Checklist Results

### API / XNA / FNA parity
`SetDataOptions::Discard`->`D3D11_MAP_WRITE_DISCARD`, `NoOverwrite`->`D3D11_MAP_WRITE_NO_OVERWRITE`, `None`-> also
`WRITE_DISCARD` (always GPU-sync-safe, matching XNA's own "`None` *may* stall" wording, which permits but doesn't
require a stall) are all individually reasonable, correctly-documented choices.

### Systematic FNA parity gaps (architecture-level, likely multi-backend)
**F1 (MEDIUM, see `AUDIT_CROSS_CUTTING_FINDINGS.md`):** `SetDataWithOptions()` has no destination-offset
parameter — confirmed by reading the full call chain from `VertexBuffer::SetDataWithOptions()` in
`xna-graphics` down to this backend, with **no offset parameter anywhere in the chain**. Every write (whether
`Discard` or `NoOverwrite`) therefore always touches the exact same `[0, byteCount)` region. This makes `NoOverwrite`
architecturally unable to provide its real purpose (streaming new data into an unused region while the GPU still
consumes an earlier region) — and specifically for D3D11, using `D3D11_MAP_WRITE_NO_OVERWRITE` on the *same*
already-drawn-from region without an intervening GPU-completion guarantee is a plausible synchronization risk
(the driver is explicitly told not to rename/resync, unlike `Discard`). Independently confirmed the identical
"always write from offset 0" shape in `EasyGLGraphicsBackend.cpp`'s own `uploadWithOptions()` — not unique to
D3D11, but D3D11's `WRITE_NO_OVERWRITE` contract makes the risk more concrete/well-defined than GL's looser
`glBufferSubData`. Not independently reproduced or traced to a live corrupt-frame repro in this pass.

### Memory/resource lifetime
COM-reference-counted `ComPtr<ID3D11Device>`/`ComPtr<ID3D11DeviceContext>` ownership (rather than a raw pointer +
manual disconnect, `VulkanVertexBufferBackend`'s own pattern) is correctly documented as simpler and equally
correct for D3D11's COM lifetime model.

### C++ correctness / Performance / Thread safety / Portability / Architecture / Maintainability / Robustness / Testing
No issues found otherwise.

## Detailed Findings

**F1 (MEDIUM, architecture-level, shared with EasyGL):** `NoOverwrite` cannot provide real streaming semantics;
plausible (unconfirmed) synchronization risk specific to D3D11's stricter `WRITE_NO_OVERWRITE` contract.

## Cross-File Observations

See `AUDIT_CROSS_CUTTING_FINDINGS.md` for the full write-up, including the `EasyGLGraphicsBackend.cpp`
cross-reference.

## Missing or Weak Tests

No dedicated test found on any backend exercising a genuine same-frame multi-draw `NoOverwrite` streaming
sequence in a way that would surface a real corruption if this risk is live.

## Positive Findings

"Never shrinks, only grows" buffer-resize discipline (documented in this file's own header comment) avoids
needless GPU buffer churn for repeat `SetData()` calls at a stable size.

## Final Assessment

One MEDIUM, architecture-level, plausible-but-unconfirmed finding, shared with at least one other backend.
