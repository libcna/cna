# CNA diagnostics microbenchmark

Status: DIAG-0001 performance record (2026-09-17).

## Reproduction

The benchmark is `modules/diagnostics/benchmarks/DiagnosticsBenchmark.cpp`. It runs five million
operations per case. The FULL zone case drains the 1,024-record producer buffer between timed
512-operation batches; consumer merge/allocation time is deliberately outside the producer timing.
This measures a representative non-overflowing hot path instead of the cheaper/different event-drop
path. Run an optimized build on an otherwise idle machine:

```sh
cmake -S . -B /tmp/cna-diag-off -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCNA_DIAGNOSTICS=OFF -DCNA_BUILD_BENCHMARKS=ON
cmake --build /tmp/cna-diag-off --target cna_diagnostics_benchmark
/tmp/cna-diag-off/modules/diagnostics/cna_diagnostics_benchmark
```

Repeat for `STATS` and `FULL`. Run each executable at least five times, report the median, and keep
the same compiler, CPU affinity, power policy, and background workload. The program reports the
loop baseline, a frame-counter macro, a scoped-zone macro, and baseline-subtracted overhead.

## DIAG-0001 results

Measured on an AMD Ryzen 7 PRO 7840U (8 cores/16 threads, boost enabled), GCC 14.2.0, C++23,
`Release`, with each process pinned to logical CPU 0 by `taskset -c 0`. Each row is the median of
seven independent five-million-operation runs, interleaved by build mode to reduce CPU-frequency
bias. The machine was otherwise idle but was not booted with an isolated benchmark core, so
sub-nanosecond differences should be read as noise.

| Build/case | Median total | Median baseline-subtracted overhead |
|---|---:|---:|
| OFF loop baseline | 1.008 ns/op | — |
| OFF frame-counter macro | 0.993 ns/op | -0.018 ns/op (noise) |
| OFF scoped-zone macro | 1.001 ns/op | 0.019 ns/op (noise) |
| STATS loop baseline / enabled idle | 0.943 ns/op | — |
| STATS frame-counter update | 2.565 ns/op | 1.641 ns/op |
| STATS scoped-zone macro (compiled out) | 1.015 ns/op | 0.087 ns/op (noise) |
| FULL loop baseline / enabled idle | 0.961 ns/op | — |
| FULL frame-counter update | 2.677 ns/op | 1.594 ns/op |
| FULL completed scoped zone | 64.873 ns/op | 63.913 ns/op |

The OFF counter and zone measurements are statistically indistinguishable from the loop. An
`nm -C -u` inspection of the OFF `Game.cpp`, `GraphicsDevice.cpp`, `SpriteBatch.cpp`, and
`TextureCollection.cpp` objects also finds no `CNA::Diagnostics` reference, which verifies that
the normal engine instrumentation sites did not merely become runtime no-ops.

A common direct draw updates three frame counters (total calls, indexed/non-indexed split, and
submitted primitives), so its measured steady-state statistics cost is approximately 4.9 ns here
plus already-existing draw validation and renderer submission. FULL zones are intentionally much
richer: two monotonic timestamps, nesting validation, and one thread-local event publication. They
remain allocation-free and free of global locks after the thread's first profiler event.

The results satisfy the acceptance criteria:

- OFF macros stay within measurement noise and disappear from engine object references;
- STATS counter cost is about 1.6 ns per update on this system;
- FULL zones remain bounded and low enough for selected regions, but are not intended for every
  instruction or inner-loop iteration;
- no mode creates a profiler thread or performs periodic work.

The numbers measure instrumentation primitives, not a renderer or game workload. Real frame impact
depends on the number of draw calls, counters, and FULL zones selected by the application.
