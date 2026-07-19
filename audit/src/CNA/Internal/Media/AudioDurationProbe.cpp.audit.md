# Audit: src/CNA/Internal/Media/AudioDurationProbe.cpp

## Metadata
- Source file: `src/CNA/Internal/Media/AudioDurationProbe.cpp`
- Audit status: AUDITED (full read, 41 lines)
- Subsystem: `cna-internal-core` shard
- File type: C++ implementation
- XNA/FNA relevance: N/A -- NOXNA
- Main related tests: not independently located in this pass

## Purpose
Implements `ProbeDurationMS()` via `avformat_open_input`/`avformat_find_stream_info` (container metadata
only, no decode), with a no-op stub when built without FFmpeg.

## Executive Verdict
Healthy.

## Checklist Results

### Resource lifetime
`avformat_close_input(&fmtCtx)` is correctly called on every path that successfully opened a context
(both the stream-info-failure/`AV_NOPTS_VALUE` early-return and the success path); `avformat_open_input`'s
own failure path needs no cleanup since FFmpeg guarantees `fmtCtx` stays null on failure, so no leak exists
there either.

### Behavioral correctness
`durationMs > 0 ? ... : 0` correctly maps any non-positive computed duration to the "unknown" convention
rather than propagating a nonsensical negative value.

## Detailed Findings
None.

## Cross-File Observations
Same FFmpeg-availability gating (`CNA_FFMPEG_AVAILABLE`) convention as `VideoDecoder.cpp`.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Clean, correct resource lifetime management across all three exit paths.

## Final Assessment
No issues found.
