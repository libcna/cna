# Audit: cmake/Tests/CanvasTests.cmake

## Metadata
- Source file: `cmake/Tests/CanvasTests.cmake` (10 lines)
- Audit status: AUDITED (full read)
- Subsystem: `build-cmake-tests` shard
- File type: CMake build script (test registration)
- XNA/FNA relevance: N/A — build infrastructure
- Main related tests: builds (but deliberately does NOT ctest-register) 2 structural smoke
  executables for the CANVAS (HTML Canvas 2D) backend

## Purpose
Builds `cna_test_canvas_smoke`/`cna_test_canvas_graphics_capability` as plain executables, with no
`add_test()`/`cna_register_backend_test()` call for either.

## Executive Verdict
Correct, and matches exactly the design this project's root `CMakeLists.txt` (already audited)
documents for this backend: `SDL_Init(SDL_INIT_VIDEO)` genuinely throws under `node CnaTests.js`
("window is not defined" — no real browser DOM), so a real PASS/FAIL requires a real browser
(`emrun`), not `ctest`. This file correctly reflects that design by never calling `add_test()` at
all — confirmed by direct read, no test registration exists anywhere in this file.

## Checklist Results
- Both executables correctly link `CNA SHARP_RUNTIME SDL3::SDL3` — consistent minimal dependency
  set for structural build verification only.
- The comment (lines 5-7) accurately cross-references this file's role as "Canvas is 2D-only"
  twin of `cna_test_sdl_graphics_capability`/`cna_test_dx3_graphics_capability` — consistent
  naming convention across backends for the `GraphicsCapability` structural check.

## Detailed Findings
None.

## Cross-File Observations
Directly confirms the design already documented in `CMakeLists.txt`'s own Canvas section (audited
in `build-root`): "Deliberately NOT registered as a CTest ... This target only proves the smoke test
configures and links here." Both files are mutually consistent.

## Missing or Weak Tests
Not applicable — the file's own design deliberately provides no automated pass/fail signal beyond
build success, and this is a documented, accepted limitation (Design decision 9, per `CMakeLists.txt`).

## Positive Findings
Correctly matches its own documented design intent exactly — no accidental `add_test()` call that
would produce a guaranteed-failing or meaningless CTest entry.

## Final Assessment
No findings.
