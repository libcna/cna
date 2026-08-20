# Audit: include/CNA/Internal/Media/AudioDurationProbe.hpp

## Metadata
- Source file: `include/CNA/Internal/Media/AudioDurationProbe.hpp`
- Audit status: AUDITED (full read, 27 lines)
- Subsystem: `cna-internal-core` shard
- File type: C++ header
- XNA/FNA relevance: N/A -- NOXNA support for real Song/Album/Playlist `Duration` (plans/plan_media.md MEDIA-65/68)
- Main related tests: not independently located in this pass

## Purpose
Declares a lightweight FFmpeg-container-metadata-only duration probe (no full decode) used once per song
during `MediaLibrary`'s synchronous constructor-time scan.

## Executive Verdict
Healthy.

## Checklist Results
Documentation correctly explains the `0 = unknown` convention (matching `Song`'s pre-existing 2-arg
constructor convention) and correctly notes platform unavailability (Windows/Android/Emscripten, matching
Video/VideoPlayer's own `CNA_FFMPEG_AVAILABLE` gating).

## Detailed Findings
None.

## Cross-File Observations
Consistent `CNA_FFMPEG_AVAILABLE` gating convention shared with `VideoDecoder`.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Correct, minimal, well-documented platform-availability convention.

## Final Assessment
No issues found.
