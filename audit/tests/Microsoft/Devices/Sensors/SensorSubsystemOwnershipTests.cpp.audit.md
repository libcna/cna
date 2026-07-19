# Audit: tests/Microsoft/Devices/Sensors/SensorSubsystemOwnershipTests.cpp

## Metadata
- Source file: `tests/Microsoft/Devices/Sensors/SensorSubsystemOwnershipTests.cpp` (201 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-microsoft-devices` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests cross-class SDL sensor-subsystem sharing between `Accelerometer` and
  `Gyroscope`, and the sensor/haptic mutex unification (NOXNA internal infrastructure, no FNA
  reference)
- Main related tests: N/A (this IS a test file)

## Purpose
Tests that disposing one sensor class's instance doesn't corrupt another's state (Task P4-8, the
shared `SDL_INIT_SENSOR` subsystem ref-counting fix), stresses concurrent cross-class construct/
destroy/probe from separate thread pools (Task P7-1, the shared mutex fix), and directly confirms
the sensor/haptic mutex unification (Task SDLCORE-001) by comparing mutex object addresses.

## Executive Verdict
Correct and notably rigorous for a cross-subsystem interaction test.
`SensorAndHapticSdlCallsShareOneProcessWideMutex`'s own comment (lines 183-195) is a precise,
honest description of what the test actually proves: comparing `&GetGlobalSdlSensorMutex()` against
`&GetGlobalSdlSubsystemMutex()` directly confirms these are now the *same* mutex object (not merely
two mutexes with similar behavior) — a test that would fail immediately against the pre-fix design
and passes only because the sensor mutex accessor now forwards to the haptic one.

## Checklist Results
- `ConcurrentCrossClassConstructDestroyProbeDoesNotCrash`'s own comment correctly distinguishes
  itself from a narrower prior single-class stress test, explicitly noting this test constructs/
  destroys/probes *both* classes concurrently from separate thread pools — the actual scenario the
  shared-mutex fix (Task P7-1) exists for, with an honest caveat that "a single green run does not
  by itself prove this is fixed" (referring readers to a documented stress-loop guidance procedure).
- Both `DisposingAccelerometerDoesNotAffectGyroscopeState`/`DisposingGyroscopeDoesNotAffect...`
  correctly note (in-comment) the real limitation: this headless environment cannot observe SDL's
  internal subsystem ref-count directly (`Start()` always throws before reaching it here), so these
  tests only prove the cross-class code path doesn't crash or corrupt either class's own
  observable state — an honest scope boundary, not an overclaimed proof.

## Detailed Findings
None.

## Cross-File Observations
`SensorAndHapticSdlCallsShareOneProcessWideMutex` directly corroborates the `microsoft-devices`
shard's own already-audited "does NOT share the FileDialog/MessageBox mutex-scoping UAF bug"
positive finding, confirming the shared mutex design (unified across sensors and haptics) is real
and load-bearing, not just documented.

## Missing or Weak Tests
The file's own comments already disclose the real limitation: SDL's internal subsystem ref-count
cannot be observed directly in this environment, so the cross-class tests prove absence-of-crash/
corruption, not the deeper ref-counting invariant itself — an honestly-scoped, not hidden, gap.

## Positive Findings
The mutex-address-identity test is a clean, unambiguous, and rare style of regression test — it
cannot pass by accident or coincidence against the pre-fix design.

## Final Assessment
No findings.
