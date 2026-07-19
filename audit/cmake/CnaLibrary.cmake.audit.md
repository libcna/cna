# Audit: cmake/CnaLibrary.cmake

## Metadata
- Source file: `cmake/CnaLibrary.cmake` (194 lines)
- Audit status: AUDITED (full read)
- Subsystem: `build-cmake` shard
- File type: CMake module
- XNA/FNA relevance: N/A (build infrastructure — defines the main `CNA` static library target)
- Main related tests: N/A (build configuration, not a test)

## Purpose
Defines the main `CNA` static library target: FFmpeg/Draco optional-dependency detection (each
with a documented, honest fallback — a Draco-compressed glTF primitive throws a clear
"not supported" error at import time rather than failing to build when Draco is absent, mirroring
FFmpeg's own `CNA_FFMPEG_AVAILABLE=OFF` source-exclusion pattern), source globbing with
backend/GamerServices/Net exclusions, and the genuinely circular static-library link relationship
between `CNA` and certain backend targets (D3D11/D3D12/D3D9/SDL_GPU) that call back into
`Effect::Apply()`/`CNA::Logger::Warn` — resolved via CMake's documented archive-repeat mechanism.

## Executive Verdict
Correct and unusually well-justified — the circular-dependency comment (lines 100-126) is a strong
example of documenting an empirically-discovered problem (a real "undefined reference" link
failure under MinGW's single-pass archive resolution, affecting even unrelated executables like
`cna_reference_dump`/`cna_demo_2d`/`cna_demo_xact`) and its precise fix, rather than asserting the
fix without evidence.

## Checklist Results
- FFmpeg unavailability correctly excludes exactly the three files that need it
  (`VideoDecoder.cpp`, `VideoPlayer.cpp`, `Video.cpp`) via targeted regex rather than a broad
  Media-directory exclusion that might also drop non-video-dependent Media code.
- Android's `libandroid`/`liblog` link requirement is correctly split into two separate,
  independently-justified additions (NDK sensor APIs vs. a debug-only log call) rather than
  bundled under one comment that might obscure why `liblog` specifically is needed.

## Detailed Findings
None.

## Cross-File Observations
The circular-dependency fix pattern here (repeat static archives on the final link line) is the
same fix applied to the SDL_GPU backend's own `CNA::Logger::Warn` circular reference, both
documented with the specific empirically-found failing executable that motivated the fix.

## Missing or Weak Tests
N/A (build configuration).

## Positive Findings
Documents the FFmpeg/Draco optional-dependency fallback behavior explicitly and symmetrically
(both throw a clear runtime error rather than silently doing nothing or failing to build) — a good
example of consistent optional-dependency design across two unrelated third-party libraries.

## Final Assessment
No findings.
