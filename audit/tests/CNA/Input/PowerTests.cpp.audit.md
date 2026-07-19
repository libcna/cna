# Audit: tests/CNA/Input/PowerTests.cpp

## Metadata
- Source file: `tests/CNA/Input/PowerTests.cpp` (93 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-cna-input` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `CNA::Input::Power` (NOXNA extension)
- Main related tests: N/A (this IS a test file)

## Purpose
Verifies `Power::GetInfoEXT` correctly maps the full `SDL_PowerState` enum to `PowerStateEXT`
(6 cases table-driven) and forwards the out-params (`secondsLeft`/`percent`), plus a case where the
backend leaves the out-params untouched and they correctly default to "unknown" (-1).

## Executive Verdict
Good, complete coverage of the full SDL-state-to-XNA-equivalent-enum mapping via a table-driven
test and dependency-injected fake backend — deterministic despite CI having no real battery.

## Checklist Results
No issues found. All 6 `SDL_PowerState` enumerators are exercised.

## Detailed Findings
None.

## Cross-File Observations
Same fake-backend dependency-injection pattern as `InputDevicesTests.cpp`/`SensorsTests.cpp`.

## Missing or Weak Tests
Not identified — the full enum's cases are all covered.

## Positive Findings
Table-driven full-enum-coverage test plus the untouched-out-param default case are both good
defensive-test design.

## Final Assessment
No findings.
