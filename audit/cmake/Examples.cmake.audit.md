# Audit: cmake/Examples.cmake

## Metadata
- Source file: `cmake/Examples.cmake` (939 lines)
- Audit status: AUDITED (full read)
- Subsystem: `build-cmake` shard
- File type: CMake module
- XNA/FNA relevance: N/A (build infrastructure — registers every `examples/demo_*` executable target)
- Main related tests: N/A (build configuration; demos themselves are audited separately under `examples-demo_*` shards)

## Purpose
Registers every example/demo executable target (`cna_demo_2d`, `cna_demo_sound`, `cna_demo_xact`,
`cna_demo_input`, `cna_demo_devices`, the input-smoke sample, the NOXNA settings example, the CNA
reference-dump tool, the 3D house demo, and the ~20 Net/GamerServices/Avatar demos), each with its
own platform gating (Emscripten/Android exclusions), Content-directory copy steps, and per-platform
link/runtime-copy logic.

## Executive Verdict
Extremely thorough and consistent — every demo target follows the same well-established pattern
(circular CNA/backend static-lib linking via `-Wl,--start-group`/`--end-group` on GNU/Clang
non-Windows, plain linking elsewhere; `SDL3::SDL3main` when present; `cna_copy_sdl_runtime`/
`WIN32_EXECUTABLE` on Windows), and each Net/GamerServices/Avatar demo's own comment cites the
specific task ID and what XNA API surface it's meant to exercise. `cna_demo_devices`'s own comment
(lines 166-179) explaining exactly why Android needs an entirely separate Gradle/JNI build (a
plain `add_executable()` cannot be a valid Android app target, since `SDL_main.h` `#define`s `main`
to `SDL_main`, confirmed via a real cross-compile link failure) is a strong example of documenting
a genuine platform constraint rather than an assumed one.

## Checklist Results
- Every `CNA_ENABLE_NET`-gated demo correctly links `CNA_GamerServices`/`CNA_Net` rather than the
  plain `CNA` target, consistent with `cmake/CnaLibrary.cmake`'s exclusion of GamerServices/Net
  sources from the main `CNA` glob.
- Each avatar demo (`cna_demo_avatar_animation_gallery`, `_wardrobe_hotswap`,
  `_appearance_tint_studio`, `_dual_compare`, `_multi_attach_stress`, `_bone_state_boundary`)
  correctly reuses `demo_avatar`'s own `Content/` directory via its own `copy_directory` step
  rather than duplicating asset files — a good DRY practice for build-time assets.

## Detailed Findings
None.

## Cross-File Observations
Every Net/GamerServices/Avatar demo registered here was independently deep-audited this session
under its own `examples-demo_*` shard; this file's own gating/link logic is consistent with what
those per-demo audits observed about which libraries (`CNA_GamerServices`/`CNA_Net`) each demo
actually calls into.

## Missing or Weak Tests
N/A (build configuration; demo test coverage is assessed per-demo in the `examples-demo_*` shards).

## Positive Findings
The `cna_demo_devices` Android-vs-desktop build-target explanation is an exceptionally clear
example of documenting a real, empirically-verified platform constraint (linker-level `main` symbol
conflict) rather than a design guess.

## Final Assessment
No findings across this large (939-line) file — consistent, well-documented target registration
throughout.
