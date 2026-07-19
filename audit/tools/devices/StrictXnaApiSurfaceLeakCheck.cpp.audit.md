# Audit: tools/devices/StrictXnaApiSurfaceLeakCheck.cpp

## Metadata
- Source file: `tools/devices/StrictXnaApiSurfaceLeakCheck.cpp` (29 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tools-devices` shard
- File type: C++ compile-only check tool, deliberately expected to FAIL to compile
- XNA/FNA relevance: exercises one deliberately-NOXNA member
  (`Accelerometer::InjectSyntheticSensorUpdate`)
- Main related tests: the `cna_strict_xna_api_leak_check` CMake target (`EXCLUDE_FROM_ALL`) plus the
  `StrictXnaApiSurfaceLeakCheck_MustFailToCompile` ctest (`WILL_FAIL TRUE`)

## Purpose
The negative counterpart to `StrictXnaApiSurfaceCheck.cpp` (Task TEST2-010, closing that task's
second acceptance criterion — "a deliberately leaked extension fails every check"): deliberately
calls a `NOXNA`-tagged member and is compiled under the same `CNA_STRICT_XNA_API` +
`-Werror=deprecated-declarations` flags, so it is *expected and required* to fail to build.

## Executive Verdict
Correct, minimal, and precisely scoped. The design (`WILL_FAIL TRUE` flips a successful build into a
reported test failure) is a genuinely clever way to detect a regression in the opposite direction
from the usual case: if a future `NOXNA` macro change or build-option regression ever stops the
strict-mode enforcement from actually catching a `NOXNA` call, *this* file would start compiling
successfully, and the `WILL_FAIL` ctest wrapper would then unexpectedly report a passing build as a
test failure — catching the regression instead of silently missing it.

## Checklist Results
- `InjectSyntheticSensorUpdate` is explicitly named in `StrictXnaApiSurfaceCheck.cpp`'s own
  "Deliberately NOT calling" list, confirming the two files are intentionally paired/cross-
  referenced, not independently authored with potential drift.
- Same orthogonality note as `StrictXnaApiSurfaceCheck.cpp` (audited alongside this file): this file
  is unrelated to the `Dispose(bool)` visibility MEDIUM finding from the `microsoft-devices` shard
  audit — it tests NOXNA-tagging enforcement, not C++ access-specifier correctness.

## Detailed Findings
None.

## Cross-File Observations
See `StrictXnaApiSurfaceCheck.cpp.audit.md` for the full discussion of why this pair of tools is
orthogonal to the `Dispose(bool)` visibility finding.

## Missing or Weak Tests
N/A — this file's entire purpose is to be a (deliberately failing) test.

## Positive Findings
The `WILL_FAIL`-ctest-wrapping-an-expected-compile-failure design is a genuinely elegant way to keep
a negative-space guarantee ("NOXNA enforcement still works") under continuous, automated regression
coverage, rather than relying on a human to periodically re-verify it manually.

## Final Assessment
No findings.
