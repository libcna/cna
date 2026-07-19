# Audit: cmake/Tests/D3D11Tests.cmake

## Metadata
- Source file: `cmake/Tests/D3D11Tests.cmake` (77 lines)
- Audit status: AUDITED (full read)
- Subsystem: `build-cmake-tests` shard
- File type: CMake build script (test registration)
- XNA/FNA relevance: N/A — build infrastructure
- Main related tests: registers the D3D11 graphics backend's own CTest suite (5 tests: Smoke,
  Common, BlendState Opaque/AlphaBlend, DepthStencilState StencilEnable, RasterizerState CullMode,
  Pbr_VertexColor)

## Purpose
Registers the D3D11 backend's CTest suite, with a `cna_d3d11_ctest_command()` macro that wraps the
built executable in `scripts/run-wine-dxvk.sh` when cross-compiling from Linux, or runs it directly
under native MSVC.

## Executive Verdict
Correct. `cna_d3d11_ctest_command()` (lines 21-27) correctly branches on `CMAKE_CROSSCOMPILING` —
Wine+DXVK wrapper when cross-compiled, direct `$<TARGET_FILE:...>` when native — matching exactly
the design `.github/workflows/d3d-windows-ci.yml` and `CMakeLists.txt` (both already audited)
describe for this backend.

## Checklist Results
- `D3D11_Common` (lines 36-43) correctly sets `CNA_D3D11_SKIP_DXVK_GATE=1` with an accurate comment
  explaining why: this binary is pure-function mapping-table checks that never creates a real D3D11
  device, so it would never print the "DXVK: <version>" line the wrapper's engagement gate normally
  expects — and the comment correctly notes this env var is a harmless no-op under native MSVC
  (`CMAKE_CROSSCOMPILING` false, no wrapper script involved at all).
- 4 of the 6 tests (`BlendState_Opaque/AlphaBlend`, `DepthStencilState_StencilEnable`,
  `RasterizerState_CullMode`) explicitly reuse the shared, backend-agnostic EasyGL test sources
  (`examples/easygl_*_test.cpp`) — consistent with the same reuse pattern seen extensively in
  Vulkan's/Bgfx's own registrations (this shard's sibling files).
- The MinGW-specific block in `cna_d3d11_test()` (lines 10-18) correctly mirrors `CnaTests`'s own
  documented COMDAT-folding workaround (`-Wl,--allow-multiple-definition`) from
  `cmake/UnitTests.cmake` (already audited), with an explicit cross-reference comment rather than
  an unexplained duplicate flag.

## Detailed Findings
None.

## Cross-File Observations
`cna_d3d11_ctest_command()`'s Wine-wrapper branching is structurally identical to D3D9's/D3D12's own
equivalents in this same shard — consistent three-way convention.

## Missing or Weak Tests
Not applicable to a build script.

## Positive Findings
Correct, explained `CMAKE_CROSSCOMPILING`-aware branching and an accurate, specific
DXVK-gate-skip rationale for the one test that legitimately never engages DXVK.

## Final Assessment
No findings.
