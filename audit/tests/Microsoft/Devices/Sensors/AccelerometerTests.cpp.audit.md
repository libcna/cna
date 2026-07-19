# Audit: tests/Microsoft/Devices/Sensors/AccelerometerTests.cpp

## Metadata
- Source file: `tests/Microsoft/Devices/Sensors/AccelerometerTests.cpp` (1505 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-microsoft-devices` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `Microsoft::Devices::Sensors::Accelerometer` (WP7-only API, no FNA reference)
- Main related tests: N/A (this IS a test file)

## Purpose
Exhaustive test suite for `Accelerometer`: support probing, instance-limit enforcement, Start/Stop/
Dispose lifecycle, event dispatch (both `CurrentValueChanged` and legacy `ReadingChanged`), and an
extensive set of concurrency/use-after-free regression tests tied to specific, named prior defects
(Task P3-9 through P8-5, SDLCORE-003/004/009, DEVPERF-005, PERF2-002).

## Executive Verdict
An exceptionally mature, thorough test file — one of the most rigorously constructed in this audit
so far. Every non-trivial test cites the specific defect/task it regression-tests, several are
tool-verifiable (ThreadSanitizer, UBSan, `/proc`-based leak tracking), and multiple tests
deliberately reconstruct historical race conditions deterministically (via `SetDisposalCleanupHookForTesting`,
placement-new ABA-hazard reproduction) rather than relying on timing luck. **Confirms the
already-known cross-cutting `Dispose(bool)` public-visibility MEDIUM finding's coverage gap**: no
test in this file calls `Dispose(false)` directly — every disposal path goes through the no-arg
`Dispose()` (which internally calls `Dispose(true)`), so the defect (a caller invoking
`Dispose(false)` directly, skipping real cleanup) is never exercised.

## Checklist Results
- `DisposeSucceedsAndSecondDisposeThrows`/`StopAfterDisposeThrows`/`StartAfterDisposeThrows` all
  call the no-arg `Dispose()` — confirmed via full-file read, `Dispose(false)` never appears
  anywhere in this file.
- Concurrency tests (`ConcurrentConstructDestroyKeepsInstanceCountBalanced`,
  `ConcurrentDisposeFromMultipleThreadsNeverCorruptsInstanceCount`,
  `ConcurrentDisposeLoserWaitsForWinnerCleanupToFinishBeforeStateAppearsDisposed`) directly target
  the specific races `SensorBase<T>`'s already-audited-and-praised concurrency design closes (Task
  P6-1, P6-3, P7-2) — this file's tests are the actual regression proof behind that praise, not
  just documentation claims.
- ABA-hazard tests (`DispatchDoesNotDeliverStaleEventToUnrelatedInstanceReusingSameAddress`) use
  deterministic placement-new address reuse rather than relying on the allocator's incidental
  behavior — a genuinely rigorous reproduction technique.
- Resource-leak test (`OneHundredThousandConstructProbeStartStopDisposeCyclesLeaveNoResourceLeak`,
  100,000 cycles) is explicitly scoped as a `/proc`-based substitute for LeakSanitizer, which the
  file's own comment discloses is non-functional in this container (needs `ptrace`) — an honest
  disclosure of a real tooling limitation rather than a silently weaker test passed off as
  equivalent.

## Detailed Findings
None new beyond the already-documented cross-cutting `Dispose(bool)` visibility finding (see
`include/Microsoft/Devices/Sensors/Accelerometer.hpp.audit.md`) — this file's coverage gap (no
`Dispose(false)` test) is a direct consequence of that production-code defect never having been
tested for, not a separate issue.

## Cross-File Observations
Directly confirms the coverage-gap half of the `Dispose(bool)` cross-cutting finding: the defect
went undetected specifically because no test anywhere exercises the public reachability of
`Dispose(false)` or its consequence (skipped cleanup). A regression test asserting that
`accel.Dispose(false)` either isn't callable, or — if it remains callable per the current
(defective) design — still performs real cleanup, would have caught this defect directly.

## Missing or Weak Tests
The one concrete gap: no test calls `Dispose(false)` directly to assert on its (currently
incorrect) behavior. Everything else in this file's own stated scope is thoroughly covered.

## Positive Findings
The concurrency/UAF/ABA regression tests in this file are genuinely exemplary: deterministic
reproduction of narrow race windows via test-only synchronization hooks (`SetDisposalCleanupHookForTesting`),
rather than "run it many times and hope," and explicit citation of the exact defect/task each test
guards against.

## Final Assessment
No new findings beyond the already-documented `Dispose(bool)` visibility gap's coverage
consequence, which this file's own content directly confirms.
