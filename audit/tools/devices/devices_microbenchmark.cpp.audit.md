# Audit: tools/devices/devices_microbenchmark.cpp

## Metadata
- Source file: `tools/devices/devices_microbenchmark.cpp` (232 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tools-devices` shard
- File type: C++ standalone tool (microbenchmark harness)
- XNA/FNA relevance: exercises real `Microsoft::Devices::Sensors::Accelerometer`/`Gyroscope` API
  plus internal `Detail::AndroidCompassMath`/`AndroidMotionMath` pure-computation helpers
- Main related tests: N/A; output consumed by `tools/devices/compare_devices_microbenchmark.py`

## Purpose
Measures p50/p95/p99 wall-clock latency (microseconds) for named benchmark categories — probe
cost, single-instance dispatch, N=1/5/10 event fanout, throttled-reject-path cost, Start/Stop
cycle, and pure Compass/Motion sensor-fusion math — emitting one JSON-Lines object per benchmark.

## Executive Verdict
Correct, with honestly disclosed scope boundaries (top-of-file comment explicitly states
allocation/lock-time are NOT instrumented, and explains why extending scope there would need much
larger, riskier changes outside this task's mandate) rather than silently narrowing coverage.

## Checklist Results
- The N=1/5/10 fanout benchmark (lines 133-157) correctly owns each `Accelerometer` via
  `std::unique_ptr` (`owners`/`instances` parallel vectors) and correctly calls
  `UnregisterStartedInstanceForTesting()` for every instance after the benchmark loop — no leak, no
  dangling registration left behind for the next benchmark iteration.
- `g_sink` (line 109, `volatile double`) as a dead-code-elimination guard for the pure-function
  Compass/Motion fusion benchmarks is a portable technique (works under any compiler, unlike a
  GCC/Clang-specific `asm volatile` clobber) — the comment correctly identifies this project's own
  multi-compiler target set (MSVC/NDK Clang per TEST2-010) as the reason for choosing this over the
  more common inline-asm approach.
- The throttled-reject-path benchmark (lines 164-173) correctly "primes" the throttle window with
  one accepted update before measuring, so every measured iteration hits the cheap early-reject
  path specifically, isolating that cost from full-dispatch cost (already measured separately).
- The Start/Stop-cycle benchmark (lines 179-201) correctly branches on `getIsSupportedProperty()`
  and measures the throw-and-catch path on unsupported hardware as a *real* cost to report, not a
  skipped/faked case — matching the comment's own framing that "every headless CI run will actually
  take" this path.

## Detailed Findings
None.

## Cross-File Observations
JSON-Lines output format confirmed consistent with `compare_devices_microbenchmark.py`'s own
expected input shape (audited alongside this file).

## Missing or Weak Tests
This file IS a benchmark tool; not applicable in the usual correctness-test sense. No test was
located verifying `ComputePercentiles()`'s own percentile-index calculation against a known input/
output pair — a subtle off-by-one there (e.g. in the `fraction * (size-1)` index formula) would
currently only be caught by a human noticing an implausible p95/p99 relationship in real output.

## Positive Findings
The honestly-scoped "not instrumented" disclosure (allocations, lock time) and the portable
`volatile` sink technique are both examples of careful, well-reasoned tool design.

## Final Assessment
No findings.
