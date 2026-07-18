# Audit: src/CNA/Internal/Backends/D3D9/D3D9InstancedDraw.cpp

## Metadata

- Source file: `src/CNA/Internal/Backends/D3D9/D3D9InstancedDraw.cpp`
- Audit status: AUDITED
- Subsystem: `backend-d3d9` shard
- File type: C++ implementation, 141 lines (fully read)
- XNA/FNA relevance: N/A stock-effect-wise (real XNA 4.0 has no per-instance-aware Stock Effect at all); a
  CNA-original (NOXNA) hardware-instancing extension
- Graphics backend relevance: implements `DrawInstancedPrimitivesEx()` via real D3D9 `SetStreamSourceFreq`
  hardware instancing
- Main related tests: `examples-tests-d3d9` (already audited via mechanical batch earlier this session)

## Purpose

Implements real D3D9 hardware instancing (`SetStreamSourceFreq`) for CNA's own `Instanced3D.hlsl` shader, plus
`GetOrCreateInstancedVertexDeclarationEXT()`.

## Executive Verdict

**Healthy.**

## Checklist Results

### Correctness
Genuine, correct D3D9 hardware instancing via the MSDN-documented "Efficiently Drawing Multiple Instances of
Geometry" convention: stream 0 (per-vertex geometry) set to `D3DSTREAMSOURCE_INDEXEDDATA | instanceCount`,
stream 1 (per-instance world-matrix data) set to `D3DSTREAMSOURCE_INSTANCEDATA | 1`. `world` parameter is
correctly and deliberately unused (per-instance world comes from the instance stream instead), explicitly
documented as matching `D3D11GraphicsBackend::DrawInstancedPrimitivesEx()`'s own identical choice. Falls back
to `DrawIndexedPrimitivesEx()` when `params.instanceVb == nullptr` (not really an instanced draw), also
matching D3D11's own precedent.

### Robustness — a real, non-obvious D3D9 gotcha correctly handled
Stream-frequency state (line 138-139) is explicitly reset to `1` (per-vertex) on BOTH streams immediately
after the instanced draw, with a comment correctly explaining why: D3D9 stream-frequency state persists on the
device until explicitly changed, and every other draw path in this backend reuses stream 0 — leaving instancing
semantics active would silently corrupt every subsequent non-instanced draw call. This is a real, easy-to-miss
D3D9 API behavior correctly anticipated and handled, not discovered via a bug report.

### C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness / Testing
No issues found.

## Detailed Findings

None.

## Cross-File Observations

Consistent with `Instanced3D.hlsl`'s own scope (flat diffuse color only, no lighting/texture/fog) and with
every other backend's own equivalent minimal "instanced3d" shader choice (D3D11/Vulkan/Bgfx).

## Missing or Weak Tests

No dedicated test found in this audit exercising a non-instanced draw call issued immediately after an
instanced one on this backend (the exact scenario this file's own stream-frequency reset defends against).

## Positive Findings

Correctly anticipates and defends against a real, non-obvious D3D9 stream-frequency-state persistence gotcha
that would otherwise silently corrupt every subsequent non-instanced draw.

## Final Assessment

No issues found.
