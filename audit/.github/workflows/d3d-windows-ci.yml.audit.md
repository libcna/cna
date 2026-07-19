# Audit: .github/workflows/d3d-windows-ci.yml

## Metadata
- Source file: `.github/workflows/d3d-windows-ci.yml` (129 lines)
- Audit status: AUDITED (full read)
- Subsystem: `build-ci` shard
- File type: GitHub Actions workflow (YAML)
- XNA/FNA relevance: N/A — CI infrastructure, not an XNA/FNA-facing source file
- Main related tests: exercises the D3D11/D3D12 backends' own `cna_d3d11_test()`/`cna_d3d12_test()`
  CTest suites (registered in `cmake/Tests/D3D11Tests.cmake`/`D3D12Tests.cmake`, not audited here)

## Purpose
Manual (`workflow_dispatch`-only) CI job that builds the D3D11/D3D12 backends with native MSVC on a
real `windows-latest` runner and runs their self-contained CTest suites directly against real
Windows D3D11/D3D12 runtime DLLs (no Wine/DXVK/vkd3d-proton translation layer), plus recompiles
every checked-in HLSL shader against a real `D3DCOMPILER_47.dll`.

## Executive Verdict
Well-scoped and unusually well-documented for a CI file — its own top comment precisely states what
real, new verification value this job adds (native-MSVC compilation and real-driver execution,
distinct from the existing Wine-based cross-compile path) and, just as importantly, what it does
NOT claim to prove (DX-90/DX-114's swap-chain/tearing/device-lost/driver-parity items still need a
machine with a real display; `CnaTests` itself is not built here due to ~10 test files' POSIX-only
`::setenv()` dependency, a known, disclosed, out-of-scope gap on any Windows toolchain).

## Checklist Results
- Correctly scoped to `workflow_dispatch` only, with an explicit rationale (cost/speed vs. the
  existing Linux CI) rather than silently under-triggering — avoids surprising maintainers who might
  expect this to run on every push given the project's other two workflows do.
- `CNA_BUILD_EXAMPLES=OFF`/`CNA_ENABLE_NET=OFF` are both deliberately scoped with an explicit
  rationale in-comment (avoid unrelated demo/networking build-failure surface) rather than left
  unexplained.
- The SDL prebuilt-tree cache key (`sdl-prebuilt-windows-msvc-${{ steps.sdlsha.outputs.sha }}`)
  correctly derives from the actual submodule commit SHA (via a dedicated step reading
  `git -C cna_graphics/third_party/SDL rev-parse HEAD`) rather than a static string, so a submodule
  bump correctly invalidates the cache instead of silently reusing a stale prebuilt SDL tree.
- The HLSL-shader-recompile step is correctly gated to run only once (`if: matrix.backend ==
  'D3D11'`) across the 2-entry backend matrix, with an explicit comment explaining the shaders are
  shared `D3DCommon` content, not backend-specific — avoids redundant, wasted CI time without being
  an unexplained/mysterious conditional.

## Detailed Findings
None.

## Cross-File Observations
Cross-referenced against `CMakeLists.txt` (`build-root` shard): the D3D11/D3D12 CTest registration
comments there (`include(cmake/Tests/D3D11Tests.cmake)`/`D3D12Tests.cmake`) explicitly reference this
exact workflow file by name and describe the same native-MSVC-vs-Wine-cross-compile distinction —
the two files' documentation is mutually consistent, not just independently plausible.

## Missing or Weak Tests
Not applicable to a CI workflow file — its own scope boundary (documented above) is honest, not a
gap in the file itself.

## Positive Findings
Exceptionally clear, honest self-documentation of exactly what this job proves and does not prove —
a model example of CI-workflow-as-documentation for a narrowly-scoped verification job.

## Final Assessment
No findings.
