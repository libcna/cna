# Audit: tools/devices/StrictXnaApiSurfaceCheck.cpp

## Metadata
- Source file: `tools/devices/StrictXnaApiSurfaceCheck.cpp` (244 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tools-devices` shard
- File type: C++ compile-only check tool (not a runtime harness — its value is whether it compiles)
- XNA/FNA relevance: directly exercises the real, STRICT-tagged `Microsoft::Devices`/
  `Microsoft::Devices::Sensors` public API surface
- Main related tests: the `cna_strict_xna_api_check` CMake target (per its own comment); compiled
  with `-Werror=deprecated-declarations` under `CNA_STRICT_XNA_API`

## Purpose
A compile-only regression check: calls only members this project's own prior audits
(`DEV-API-001`/`DEV-API-004`/`READINGS-001`/`READINGS-002`) confirmed are genuinely real XNA/WP7
API, never `NOXNA`, so that a clean build of this file under `CNA_STRICT_XNA_API` is itself
evidence the real API surface remains unaffected by strict mode.

## Executive Verdict
Correct, and precisely scoped to its own stated purpose (verifying STRICT/EXT-vs-NOXNA tagging
correctness, not general API-contract correctness). **Explicitly does NOT exercise `Dispose(bool
disposing)` at all** — every sensor's teardown call is the plain, no-argument `Dispose()` (e.g.
`accelerometer.Dispose()`, line 102). This means this tool would **not** catch (and was never
designed to catch) the MEDIUM finding confirmed in this session's `microsoft-devices` shard audit
(`Dispose(bool disposing)` incorrectly re-declared `public` instead of `protected` in all four
sensor classes) — that is a C++ access-specifier/encapsulation concern, orthogonal to this tool's
actual job of confirming that real STRICT XNA members compile under strict-mode enforcement. A
correctly-`protected` `Dispose(bool)` and an incorrectly-`public` one would both compile identically
here, since this file never calls it directly either way.

## Checklist Results
- The "Deliberately NOT calling" comment (lines 22-30) is precise and itself cites specific task IDs
  (`DEV-API-003`'s resolution, `SENSORBASE-007`) for why certain members are excluded — e.g. only
  `Accelerometer::getStateProperty()` is real XNA API; the same-named property on
  `Gyroscope`/`Compass`/`Motion` is NOXNA, and this file correctly calls it only on `Accelerometer`
  (line 76).
- Every sensor's exercise function (`ExerciseAccelerometer`/`Gyroscope`/`Compass`/`Motion`) follows
  an identical, consistent shape: static support check, event subscription (proving the delegate
  type compiles), `Start()`/`Stop()` (with `Start()` wrapped in try/catch since real hardware may be
  unavailable), `getCurrentValueProperty()` (also try/catch-guarded), `getIsDataValidProperty()`,
  `TimeBetweenUpdates` get/set round-trip, then `Dispose()`.
- `ExerciseReadingStructs()` (lines 197-232) exercises every real reading-struct constructor and
  public accessor across all 5 reading types — thorough coverage of the STRICT-tagged data-value
  surface, not just the sensor classes themselves.

## Detailed Findings
None — this file does exactly what it claims to do, correctly and precisely scoped. The
`Dispose(bool)` non-overlap noted above is a scope clarification, not a defect in this file.

## Cross-File Observations
See `StrictXnaApiSurfaceLeakCheck.cpp` (audited alongside this file) for the negative counterpart —
together the pair proves both directions of the STRICT/NOXNA tagging enforcement
(`CNA_STRICT_XNA_API` + `-Werror=deprecated-declarations`), a distinct concern from the
`Dispose(bool)` access-specifier finding in the `microsoft-devices` shard audit.

## Missing or Weak Tests
Given the finding above, a *different* tool (not this one, whose scope doesn't fit) would be needed
to catch the `Dispose(bool)` visibility regression directly — e.g. a black-box test that constructs
a sensor and calls `.Dispose(false)` directly, then asserts real cleanup occurred (a shape closer to
`tools/devices/devices_microbenchmark.cpp`'s testing-hook usage than this file's compile-check
design).

## Positive Findings
Precisely scoped, well-documented compile-only regression check with an explicit "why NOT calling
X" list that is itself useful documentation of prior API-surface investigation.

## Final Assessment
No findings against this file's own stated purpose. Confirmed (for the parent's benefit): this tool
does NOT and was never intended to catch the `Dispose(bool)` visibility MEDIUM finding from the
`microsoft-devices` shard audit — the two concerns are orthogonal.
