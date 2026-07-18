# AUDIT_FINDINGS_INDEX.md

**Status: SKELETON — populated incrementally as per-file audits surface findings worth surfacing globally (not
every `INFO`-level note needs an index entry; use judgment — this index is for anything `MEDIUM`+ severity, or
`LOW` if it recurs across many files).**

Recommendations recorded here are for future prioritization only — **no implementation work is performed as part
of this audit** (see `CLAUDE.md`/audit prompt "No-development rule").

## By severity

### CRITICAL
_(none recorded yet)_

### HIGH
_(none recorded yet)_

### MEDIUM

- **Headless backend: `HeadlessStatistics::primitiveCount` undercounts instanced draws by a factor of
  `instanceCount`.** `src/CNA/Internal/Backends/Headless/HeadlessGraphicsBackend.cpp` `DrawInstancedPrimitivesEx`
  (lines 819-830) corrects `drawCallCount` for instancing but not `primitiveCount`. See
  [audit report](src/CNA/Internal/Backends/Headless/HeadlessGraphicsBackend.cpp.audit.md) F1.
- **Software backend: `DepthBufferWriteEnable`/`SetDepthWriteEnabled` have no effect — depth is always written
  when the test passes.** `src/CNA/Internal/Backends/Software/SoftwareGraphicsBackend.cpp` `ApplyDepthStencilState`
  (lines 1095-1099) never stores the write-enable flag; both rasterizer cores write depth unconditionally. See
  [audit report](src/CNA/Internal/Backends/Software/SoftwareGraphicsBackend.cpp.audit.md) F1.
- **Software backend: `DepthStencilState.DepthBufferFunction` is ignored — depth test is hardcoded to
  LessEqual.** Same file, same method; the `depthFunc` parameter is discarded and the rasterizer always does
  `reject if depth > stored`. See
  [audit report](src/CNA/Internal/Backends/Software/SoftwareGraphicsBackend.cpp.audit.md) F2.

### LOW
_(none recorded yet)_

## By subsystem
_(index rebuilt from the severity table above once populated)_

## By category
_(correctness / FNA-parity / architecture / performance / memory / portability / testing — rebuilt once populated)_
