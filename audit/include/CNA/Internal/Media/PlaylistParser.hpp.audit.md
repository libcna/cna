# Audit: include/CNA/Internal/Media/PlaylistParser.hpp

## Metadata
- Source file: `include/CNA/Internal/Media/PlaylistParser.hpp`
- Audit status: AUDITED (full read, 36 lines)
- Subsystem: `cna-internal-core` shard
- File type: C++ header
- XNA/FNA relevance: N/A -- NOXNA M3U/M3U8 reader backing `PlaylistCollection` (plans/plan_media.md MEDIA-57/58/D5)
- Main related tests: not independently located in this pass

## Purpose
Declares a minimal M3U/M3U8 playlist reader: path-per-line with `#EXTINF:`-style comments tolerated,
entries resolved relative to the playlist's own directory, missing entries skipped rather than fatal.

## Executive Verdict
Needs attention -- see the paired `.cpp`'s finding: playlist entries are resolved with no containment check
against any root directory, unlike sibling files in this same subsystem area (`CnjSourceFile.hpp`,
`SavedPictureStore.cpp`) that do defend against path escape.

## Checklist Results
Documentation candidly notes the "missing content degrades gracefully" design choice and the byte-
transparent-UTF-8 non-issue for legacy `.m3u` vs `.m3u8` encoding (verified against a real non-ASCII fixture
per the header comment).

## Detailed Findings
See `PlaylistParser.cpp`'s report for the substantive (MEDIUM-severity, confused-deputy-style)
path-containment finding.

## Cross-File Observations
Contrast with `CnjSourceFile.hpp` and `SavedPictureStore.cpp` (same shard), both of which sanitize/confine
untrusted path input; `PlaylistParser` performs no equivalent confinement of `.m3u` entry paths.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
N/A (see .cpp).

## Final Assessment
No issues in this header; see the paired `.cpp` for the substantive finding.
