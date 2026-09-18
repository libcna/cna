# CNA diagnostics and profiler plan

Status: complete (DIAG-0001, 2026-09-17)

Follow-up fixes from the production-readiness audit are recorded in
[`plan_diagnostics_inspector_audit.md`](plan_diagnostics_inspector_audit.md)
(`AUD-DIAG-INSP-0001` to `AUD-DIAG-INSP-0003`).

## Boundary

This plan owns CNA's renderer-independent profiler and diagnostics foundation. It does not own an
Inspector transport or UI: no HTTP, WebSocket, HTML, JavaScript, browser runtime, GPU readback, or
mandatory third-party profiler belongs here.

## Architecture

1. `modules/diagnostics` is a standard-library-only module below runtime, graphics, audio, and
   future tools. Its public API lives in `CNA::Diagnostics`; XNA semantics remain unchanged.
2. `CNA_DIAGNOSTICS=OFF|STATS|FULL` is a public build fact. OFF instrumentation macros expand to
   nothing without evaluating arguments, engine resource bookkeeping is compiled out, and direct
   diagnostics API calls are inert. STATS retains bounded frame histories and cheap counters/gauges.
   FULL additionally retains bounded per-thread event streams, hierarchical CPU zones, markers,
   resource metadata, and recording.
3. Hot FULL events enter a fixed-capacity thread-local SPSC buffer. Producers do not allocate or
   lock after first use. Frame boundaries merge buffers into a bounded process history; pull
   consumers read it by sequence without becoming callbacks on instrumentation paths.
4. Metrics use fixed registry slots and relaxed atomics. Registration may lock and allocate the
   metric name once; updates never traverse the registry. Frame counters reset at `EndFrame`.
5. Engine integration is limited to common seams: `Game`, `GraphicsDevice`, `GraphicsResource`,
   and the common audio mixer facade. Renderer-specific asynchronous GPU metrics plug in through
   the provider/source boundary; this task will not make a renderer wait for a query.
6. Recording snapshots the bounded binary event representation and writes a versioned,
   little-endian CNA trace. Chrome Trace JSON is an offline streaming export only.
7. Resource diagnostics expose stable IDs and metadata only. Byte sizes are explicitly classified
   as exact, estimated, or unknown; no GPU readback or global `new`/`delete` replacement is used.

## Task

| ID | Deliverable | Status |
|---|---|---|
| DIAG-0001 | Build modes, core APIs, engine integration, bounded recording, tests, benchmarks, and documentation | Complete |

## Completion evidence

- OFF, STATS, and FULL focused suites: 2/2, 7/7, and 17/17 passed.
- FULL HEADLESS regression suites: graphics 2,322 passed with 454 capability skips; runtime 174
  passed with 2 capability skips.
- OFF engine-object symbol inspection found no `CNA::Diagnostics` references in `Game`,
  `GraphicsDevice`, `SpriteBatch`, or `TextureCollection` instrumentation objects.
- FULL builds compile both real audio paths used by the integration: SDL3_mixer and CnaMixer/ALSA.
- The optimized seven-run, mode-interleaved benchmark is recorded in
  [`../docs/diagnostics-benchmark.md`](../docs/diagnostics-benchmark.md); architecture, API,
  accuracy boundaries, limitations, and Inspector handoff are in
  [`../docs/diagnostics.md`](../docs/diagnostics.md).
