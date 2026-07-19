# Audit: cmake/TestHelpers.cmake

## Metadata
- Source file: `cmake/TestHelpers.cmake` (49 lines)
- Audit status: AUDITED (full read)
- Subsystem: `build-cmake` shard
- File type: CMake module (helper function)
- XNA/FNA relevance: N/A (build infrastructure — shared CTest-registration helper)
- Main related tests: N/A (used to register ~600 backend test registrations across `cmake/Tests/*.cmake`)

## Purpose
Defines `cna_register_backend_test()`, a single helper collapsing the repeated
`add_test()`+`set_tests_properties()` pair used at every one of this project's ~600 per-backend
CTest registrations.

## Executive Verdict
Correct and well-designed — the semicolon-escaping logic (`string(REPLACE ";" "\\;" ...)`) for
`LABELS`/`ENVIRONMENT`/`SKIP_REGULAR_EXPRESSION` values is a genuinely easy CMake pitfall to miss
(a literal semicolon in a value like `"SDL_VIDEODRIVER=x11;DISPLAY=:0"` would otherwise be
flattened into extra list elements by `list(APPEND)`, corrupting the final
`set_tests_properties()` argument list) and the file's own comment explains exactly why the escape
is necessary.

## Checklist Results
- Deliberately does NOT unify the `add_executable()`/`target_link_libraries()` half (left to each
  backend's own `cna_<backend>_test()` macro) since linking genuinely differs per backend (Wine/DXVK
  wrapping, `-Wl,--start-group` circular-dependency links, extra libs) — a sound scope boundary,
  not an accidental inconsistency.
- Every optional property (`TIMEOUT`/`LABELS`/`ENVIRONMENT`/`WORKING_DIRECTORY`/
  `SKIP_REGULAR_EXPRESSION`) is correctly guarded by its own `if(...)` before appending, and the
  final `set_tests_properties()` call is itself guarded by `if(_cna_test_props)` — never called
  with an empty property list.

## Detailed Findings
None.

## Cross-File Observations
Used throughout `cmake/Tests/*.cmake` (all 14 per-backend test-registration files) and directly in
`cmake/UnitTests.cmake` (`CnaInputTests`'s own registration).

## Missing or Weak Tests
N/A (build helper, not a test).

## Positive Findings
The semicolon-escaping fix is a subtle, easy-to-miss CMake correctness detail handled properly with
a clear explanatory comment.

## Final Assessment
No findings.
