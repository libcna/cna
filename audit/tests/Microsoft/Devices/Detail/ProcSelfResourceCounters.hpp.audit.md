# Audit: tests/Microsoft/Devices/Detail/ProcSelfResourceCounters.hpp

## Metadata
- Source file: `tests/Microsoft/Devices/Detail/ProcSelfResourceCounters.hpp` (73 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-microsoft-devices` shard
- File type: C++ test-support header (not itself a test file)
- XNA/FNA relevance: Test-only support utility (NOXNA, no FNA reference), Linux-only
  (`#if defined(__linux__)`)
- Main related tests: shared by `AccelerometerTests.cpp`, `GyroscopeTests.cpp`,
  `CompassTests.cpp`, `MotionTests.cpp`, `VibrateControllerTests.cpp` (their own 100,000-cycle
  leak-stress tests)

## Purpose
Provides `CountOpenFileDescriptors()` (via `/proc/self/fd`) and `GetThreadCount()` (via
`/proc/self/status`'s `Threads:` line) — minimal, deliberately scoped Linux-specific resource-usage
counters used as a LeakSanitizer substitute in this container (which the shard's own tests disclose
cannot run real LSan, lacking `ptrace`).

## Executive Verdict
Correct, careful, minimal. `CountOpenFileDescriptors()` correctly accounts for and subtracts the
transient `/proc/self/fd` directory handle `opendir()` itself opens for the duration of the
traversal — a real, easy-to-miss off-by-one that would otherwise make every call over-report by
exactly one fd. Both functions correctly return `-1` on failure rather than silently returning a
garbage/zero count a caller might mistake for "no descriptors" or "no threads."

## Checklist Results
- `CountOpenFileDescriptors()`'s directory-entry filter (`entry->d_name[0] != '.'`) correctly skips
  `.`/`..` without needing `strcmp`.
- `GetThreadCount()`'s `std::stoi` call is wrapped in `try`/`catch` to safely return `-1` on a
  malformed line rather than letting `std::invalid_argument`/`std::out_of_range` propagate
  unexpectedly to a caller not expecting an exception from this header-only helper.

## Detailed Findings
None.

## Cross-File Observations
Every consuming test file's own 100,000-cycle stress test correctly checks both counters against a
pre-loop baseline (not an absolute zero), and each includes an explicit warm-up cycle outside the
measured window to avoid misattributing legitimate one-time lazy-initialization costs (e.g. a
function-local static's first-touch cost) as a per-cycle leak.

## Missing or Weak Tests
Not applicable — this is a support header, not independently unit-tested; its correctness is
implicitly exercised by every consuming test file's own leak-stress tests, which would fail if this
header's counting were unreliable.

## Positive Findings
The `opendir()` self-fd accounting fix and the `-1`-on-failure contract are both examples of careful
attention to a small utility's own correctness, not just "good enough for a test helper."

## Final Assessment
No findings.
