# Audit: tests/Microsoft/Xna/Framework/Content/CnjEffectTests.cpp

## Metadata
- Source file: `tests/Microsoft/Xna/Framework/Content/CnjEffectTests.cpp` (127 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-xna-content` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `.cnj` `Effect`/`ShaderEffect` loading (NOXNA content pipeline
  extension, migrated from `.shader.json`)
- Main related tests: N/A (this IS a test file)

## Purpose
Tests `.cnj` `Effect` document loading: real GLSL vertex/fragment source compiled through
`ShaderEffect`, and mismatched-type rejection.

## Executive Verdict
Correct. `LoadsRealCnjFixture` uses real, valid, minimal GLSL 300 es shader source (not a stub or
mock), asserting both successful `dynamic_cast` to `ShaderEffect` and `IsEffectValid()` — a genuine
end-to-end compile-and-link verification, not merely "didn't throw."

## Checklist Results
No issues found.

## Detailed Findings
None.

## Cross-File Observations
None.

## Missing or Weak Tests
Only 2 tests for a shader-compiling reader; a test for a deliberately-invalid GLSL source (compile
failure) is not present and would be a reasonable addition, though not independently confirmed as
missing versus covered elsewhere (e.g. `ShaderEffectTests.cpp`, in the sibling `tests-xna-graphics`
shard, not part of this batch).

## Positive Findings
Real shader compilation is exercised end-to-end, not mocked.

## Final Assessment
No findings.
