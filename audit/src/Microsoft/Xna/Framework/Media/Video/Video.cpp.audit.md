# Audit: src/Microsoft/Xna/Framework/Media/Video/Video.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/Media/Video/Video.cpp`
- Audit status: AUDITED (full read, 114 lines)
- Subsystem: `xna-media` shard
- File type: C++ implementation
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Media/Video/Video.cs` -- raw-file
  constructor's `FileNotFoundException` check verified matching
- Main related tests: not independently located in this pass

## Purpose
Implements both constructors (the raw-file one probing via `CNA::Internal::Media::VideoDecoder`),
property getters, `SetAudioTrackEXT`/`SetVideoTrackEXT` (which correctly propagate to an active
`VideoPlayer` parent), `FromUriEXT()`.

## Executive Verdict
Correct, and confirms a genuine, previously-fixed defect. The raw-file constructor's comment
explicitly states: "CNA's equivalent previously just probed via `VideoDecoder::Open` and silently
left `width_`/`height_`/`duration_` at 0 on failure, with no exception at all -- a real fidelity gap
(`plans/plan_media.md MEDIA-44`)" -- now fixed to check `std::filesystem::exists()` and throw
`FileNotFoundException` before probing, matching FNA's real `Video.cs` exactly.

## Checklist Results
No issues found.

## Detailed Findings
None.

## Cross-File Observations
`SetAudioTrackEXT`/`SetVideoTrackEXT` correctly propagate to `parent_` (the owning `VideoPlayer`,
set via the `friend class VideoPlayer` relationship declared in the header) -- verified consistent
with `VideoPlayer::OpenDecoder()`'s own track-preference application (audited separately).

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
A genuine, well-documented FNA-fidelity fix (MEDIA-44: missing `FileNotFoundException`).

## Final Assessment
No findings.
