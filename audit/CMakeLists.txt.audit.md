# Audit: CMakeLists.txt

## Metadata
- Source file: `CMakeLists.txt` (174 lines, repo root)
- Audit status: AUDITED (full read)
- Subsystem: `build-root` shard
- File type: CMake build script
- XNA/FNA relevance: N/A — build infrastructure
- Main related tests: registers every backend's CTest suite via `include(cmake/Tests/*.cmake)`
  (each `.cmake` file audited separately in its own shard)

## Purpose
Root CMake entry point: project/C++23 setup, ccache integration, Emscripten exception-support
workaround, top-level feature options (`CNA_BUILD_TESTS`/`CNA_NOXNA`/`CNA_DEVICES`/`CNA_ENABLE_NET`),
vendored-SDL/ENet bootstrapping, the `sharp-runtime` sibling-repo dependency check, optional
sanitizer wiring, and inclusion of every backend-selection/library/example/test `.cmake` module.

## Executive Verdict
Well-organized and thoroughly self-documented — nearly every non-trivial line has an inline comment
explaining a real historical reason (a specific task ID, a concrete bug once found, or a design
decision), consistent with the general quality level observed across this project's other build
files. The `sharp-runtime` sibling-repo existence check (lines 75-83) fails fast with a precise,
actionable fix command rather than a cryptic downstream CMake error, if the sibling checkout is
missing.

## Checklist Results
- `CNA_TEST_DISPLAY` (line 47) is correctly a cached string (not hardcoded), with the documented
  default (`:0`) matching this audit's own memory of a previously-confirmed real gotcha in this
  project: the value is baked into the CMake cache at configure time and does not update if the
  environment's real `DISPLAY` changes later — the comment here (line 44-46) correctly discloses
  exactly this behavior, matching independent knowledge of the project's Xvfb/display test setup.
- The Emscripten exception-support workaround (lines 29-37) is placed correctly BEFORE
  `add_subdirectory(../sharp-runtime ...)`, with an explicit comment explaining why (every frame in
  the call chain must be compiled with `-fexceptions` for unwinding to work, so it must apply before
  the dependency is configured) — verified logically consistent with the stated constraint.
- The `CNA_SANITIZE` cached string (lines 63-68) is applied via `add_compile_options`/
  `add_link_options` at exactly the point in the file where `sharp-runtime` has already been added
  (line 84) is NOT yet added — wait: re-checking order, `CNA_SANITIZE` (line 63) is applied BEFORE
  `add_subdirectory(../sharp-runtime SHARP_RUNTIME)` (line 84), and the comment (lines 61-62)
  explicitly states this is deliberate ("applied from here on, so CNA, sharp-runtime, and the tests
  are instrumented; the vendored SDL/ENet builds above stay uninstrumented") — correctly ordered
  relative to its own stated intent (SDL/ENet's `include(cmake/ThirdPartySDL.cmake)`/
  `cna_configure_vendored_sdl()` calls at lines 49-50 run BEFORE this point, so they are correctly
  excluded from instrumentation; `sharp-runtime` at line 84 runs AFTER, so it is correctly included).
- The long sequence of `include(cmake/Tests/*Backend*Tests.cmake)` calls (lines 96-167) is
  consistently and individually commented with either a plan/task-ID reference or a concrete,
  previously-found bug (e.g. the DX3 SDL_VIDEODRIVER=dummy-vs-x11 registration bug described at
  lines 122-132, and the Canvas backend's Node.js `window is not defined` limitation at lines
  150-157) — each inclusion's rationale is independently checkable, not merely asserted.

## Detailed Findings
None.

## Cross-File Observations
- The D3D11/D3D12 CTest registration comments (lines 135-148) correctly and specifically reference
  `.github/workflows/d3d-windows-ci.yml` by name and describe the same native-MSVC-vs-Wine-wrapper
  distinction documented in that workflow file's own top comment — cross-verified consistent (see
  `d3d-windows-ci.yml.audit.md`).
- `option(CNA_ENABLE_NET ... ON)` (line 42, default ON) is consistent with
  `.github/workflows/d3d-windows-ci.yml` explicitly passing `-DCNA_ENABLE_NET=OFF` to narrow its own
  build scope — the default here doesn't need to match every specialized CI job's override, and it
  doesn't silently conflict with one either.

## Missing or Weak Tests
Not applicable to a build script — its own correctness is exercised indirectly by every CTest suite
this repository has, all of which depend on this file configuring successfully.

## Positive Findings
Exceptionally thorough inline documentation of non-obvious ordering constraints (Emscripten
exceptions, sanitizer scope) and historical bug rationale (DX3 display registration, Canvas Node.js
limitation) throughout a moderately complex root build file.

## Final Assessment
No findings.
