# Audit: src/CNA/Internal/Backends/SdlGpu/shaders/compile_shaders.py

## Metadata

- Source file: `src/CNA/Internal/Backends/SdlGpu/shaders/compile_shaders.py`
- Audit status: AUDITED
- Subsystem: `backend-sdlgpu` shard
- File type: Python build/tooling script (202 lines), not part of any CMake build target (run by hand)
- XNA/FNA relevance: N/A (build tooling)
- Graphics backend relevance: regenerates `spirv_shaders.hpp` from the 23 `.glsl` source files
- Main related tests: N/A (developer tooling, not test-covered)

## Purpose

Compiles GLSL shaders to SPIR-V via `libshaderc.so.1` (loaded through `ctypes`, no build-time C++ dependency
needed) and emits `spirv_shaders.hpp` with one `static constexpr uint32_t` array per shader stage.

## Executive Verdict

**Healthy.** Correctly and clearly documents a real, non-obvious, SdlGpu-specific SPIR-V binding convention that
must NOT be confused with this project's own Vulkan backend's different convention.

## Checklist Results

### Architecture
The module docstring's explicit warning — "these shaders follow SDL_gpu's SPIR-V resource-binding convention...
NOT the plain Vulkan convention this project's `VulkanGraphicsBackend` shaders use... Do not copy a
`VulkanGraphicsBackend` shader's compiled SPIR-V here without re-checking its set numbers" — is a valuable,
proactive warning against a plausible, easy mistake (given how much of this shard's shader *logic* is
deliberately kept parallel to the Vulkan backend's own shaders, per numerous cross-references already found in
this audit's shader-by-shader review). Independently verified this distinction is real: every `.glsl` file in
this shard does declare vertex UBOs at `set = 1`/fragment textures at `set = 2`/fragment UBOs at `set = 3`,
consistent with the documented convention.

### Behavioral correctness / Logic
Uses `libshaderc.so.1` directly via `ctypes` (with a small list of candidate library paths) rather than shelling
out to a separate `glslc` binary or requiring it as a build-time dependency — a reasonable, self-contained choice
for a manually-invoked regeneration script.

### C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness / Testing
N/A (Python tooling) / No issues found.

## Detailed Findings

None.

## Cross-File Observations

The documented SDL_gpu-vs-Vulkan SPIR-V binding-convention distinction is independently corroborated by every
`.glsl` file in this shard, all of which consistently follow the SDL_gpu convention (set 0/1/2/3) documented here,
distinct from the plain Vulkan backend's own shaders (not reviewed in this pass, but referenced consistently by
comments throughout this shard as sharing *logic*, not binding layout).

## Missing or Weak Tests

N/A — this is a manually-invoked regeneration script, not part of any automated build/test target, so (as
already noted for the equivalent D3DCommon/D3D11 tooling) there is no CI verification that `spirv_shaders.hpp` is
actually up to date with the current `.glsl` sources at any given commit — a project-wide process characteristic,
not unique to this file.

## Positive Findings

Proactive, clearly-reasoned warning against a plausible cross-backend copy-paste mistake (SDL_gpu vs. plain
Vulkan SPIR-V binding conventions), backed by consistent actual usage across every shader file in this shard.

## Final Assessment

No issues found; a well-documented, self-contained shader-compilation tool.
