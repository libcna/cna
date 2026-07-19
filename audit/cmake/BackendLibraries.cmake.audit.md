# Audit: cmake/BackendLibraries.cmake

## Metadata
- Source file: `cmake/BackendLibraries.cmake` (148 lines)
- Audit status: AUDITED (full read)
- Subsystem: `build-cmake` shard
- File type: CMake module
- XNA/FNA relevance: N/A (build infrastructure — wires up per-backend link dependencies)
- Main related tests: N/A (build configuration, not a test)

## Purpose
Builds the main graphics-backend static library target (`${BACKEND_TARGET}`) and its per-backend
link dependencies (SDL3, Vulkan, bgfx, WebGPU, D3D9/11/12, DirectDraw via `free-direct`,
SDL_GPU + libshaderc), plus two shared-core static libs (`D3DCommon` for D3D11/D3D12,
`sdl_renderer_core` reused by ASCII's own decorator design) and the isolated D3D9 custom-effect
target that keeps d3dcompiler out of the stock D3D9 pipeline.

## Executive Verdict
Very well-documented — nearly every branch cites a specific plan/task ID and a concrete rationale
(often an empirically-found link error, e.g. the D3D9 effect-backend circular-dependency comment
citing an actual "undefined reference to ParseConstantTableEXT" failure). No correctness issues
found; the conditional structure correctly gates each backend's link requirements.

## Checklist Results
- SDL_GPU's libshaderc discovery correctly falls back from `find_library()` to a glob of standard
  library directories when no dev package is present, with a clear `FATAL_ERROR` if neither finds
  it — fails loudly rather than silently producing a broken build.
- D3D9's effect backend is correctly excluded from the main glob and given its own isolated static
  library specifically to keep d3dcompiler out of the stock pipeline (design decision 16) — the
  comment explains this was discovered via a real link failure, not designed in speculatively.

## Detailed Findings
None.

## Cross-File Observations
Complements `cmake/BackendSelection.cmake` (which sets `BACKEND_DIR`/`BACKEND_TARGET` per
`CNA_GRAPHICS_BACKEND` before this file runs) and `cmake/CnaLibrary.cmake` (which links the main
`CNA` target against `${BACKEND_TARGET}`).

## Missing or Weak Tests
N/A (build configuration).

## Positive Findings
Consistently cites the specific empirical failure (e.g. a real link error) that justified each
non-obvious conditional branch, rather than asserting design choices without evidence — a strong
documentation discipline matching this project's per-file audit conventions elsewhere.

## Final Assessment
No findings.
