# Audit: examples/demo_devices/android/.../app/jni/CMakeLists.txt

## Metadata
- Source file: `.../app/jni/CMakeLists.txt` (16 lines)
- Audit status: AUDITED (full read)
- Subsystem: `examples-demo_devices` shard
- File type: Android CMake entry point (invoked via gradle's `externalNativeBuild.cmake`, gated
  behind the `-PBUILD_WITH_CMAKE` project property — see `app/build.gradle.audit.md`)
- XNA/FNA relevance: build infrastructure only
- Main related tests: none

## Purpose
Roots the Android CMake build at CNA's own top-level `CMakeLists.txt` (with `CNA_BUILD_TESTS`/
`CNA_BUILD_EXAMPLES` forced off) rather than building a second, separate SDL from the stock
SDL-Android-project-template's vendored `app/jni/SDL` copy, then adds the `src` subdirectory.

## Executive Verdict
Correct and well-justified — the Task DEVICES-0124 comment precisely explains why this reuses CNA's
own root project (avoiding a duplicate SDL3 build and an `SDL3::SDL3` target collision) rather than
following the stock SDL Android template's default of building bundled SDL sources.

## Checklist Results
- `add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../../../.. cna_build)` — the six `../`
  segments correctly walk from `app/jni/` back to the repository root (verified path depth:
  `examples/demo_devices/android/com.openeggbert.cna.demodevices/app/jni` is 5 levels below
  `examples/`, which is 1 level below repo root — 6 total, matching the 6 `../` segments).
- `CNA_BUILD_TESTS`/`CNA_BUILD_EXAMPLES` forced `OFF` via `CACHE BOOL ... FORCE` — correctly avoids
  pulling in this project's entire test/example suite (hundreds of targets) into a mobile app build
  that only needs the core `CNA`/`SHARP_RUNTIME` libraries.

## Detailed Findings
None.

## Cross-File Observations
This file's `add_subdirectory(cna_build)` is the mechanism `app/jni/src/CMakeLists.txt`'s own
comment references when it says the parent already cached `SDL3_DIR` — the two files' comments are
mutually consistent and correctly describe the same shared-cache relationship.

## Missing or Weak Tests
N/A — build configuration.

## Positive Findings
The explicit rationale for reusing the root project instead of the stock template's bundled-SDL
default is exactly the kind of non-obvious "why" comment this codebase's own conventions ask for.

## Final Assessment
No findings.
