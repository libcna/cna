# Audit: cmake/Tests/D3D12Tests.cmake

## Metadata
- Source file: `cmake/Tests/D3D12Tests.cmake` (36 lines)
- Audit status: AUDITED (full read)
- Subsystem: `build-cmake-tests` shard
- File type: CMake build script (test registration)
- XNA/FNA relevance: N/A — build infrastructure
- Main related tests: registers the D3D12 graphics backend's CTest suite (1 test: `D3D12_Smoke`)
  plus a deliberately-unregistered diagnostic executable

## Purpose
Registers `D3D12_Smoke` (off-screen device/queue/heap/command-list/fence proof, no window/swap
chain) with the same `CMAKE_CROSSCOMPILING`-aware Wine+vkd3d-proton wrapper pattern as D3D9/D3D11,
and builds (but does not `add_test()`) `cna_diag_d3d12_swapchain_diag`, a real window-attached
swap-chain diagnostic.

## Executive Verdict
Correct, and honest about a known real-environment limitation: the comment (lines 30-34) explains
`cna_diag_d3d12_swapchain_diag` is deliberately NOT registered as a CTest because "DX-100's own
spike already found this crashes under vanilla Wine's dxgi.dll" — a permanently-registered,
always-crashing CTest would just be noise. This mirrors the same "real executable, not a ctest"
precedent established by `cna_diag_compare`/`cna_diag_software` in `cmake/Harnesses.cmake`/
`SoftwareTests.cmake` (already audited).

## Checklist Results
- `cna_d3d12_test()` macro (lines 4-16) is structurally identical to D3D11's/D3D9's own test-build
  macros in this shard (same MinGW COMDAT-folding workaround, same SDL3main linkage) — consistent.
- The inline `if(CMAKE_CROSSCOMPILING)` branch for `_d3d12_smoke_cmd` (lines 22-26) is functionally
  equivalent to D3D9's/D3D11's own dedicated `cna_d3d9_ctest_command()`/`cna_d3d11_ctest_command()`
  macros, just written inline rather than factored into a named macro — a minor stylistic
  inconsistency (2 of 3 D3D backends factor this into a macro, D3D12 does it inline), not a
  functional defect since the actual logic is identical and correct in all three.
- `D3D12_Smoke`'s own comment (lines 18-20) correctly cites its source file's own header comment as
  the authority for why it deliberately never constructs a window/swap chain — cross-checkable,
  not merely asserted here.

## Detailed Findings
None.

## Cross-File Observations
The always-crashing-diagnostic-not-registered-as-ctest pattern here is the third instance of this
exact convention in this shard (after `cna_diag_compare` and `cna_diag_software`) — a consistent,
deliberate project-wide choice for tools whose failure mode is expected/known rather than a
regression signal.

## Missing or Weak Tests
Not applicable to a build script.

## Positive Findings
Honest, well-justified non-registration of a known-crashing diagnostic tool, consistent with this
project's established convention.

## Final Assessment
No findings.
