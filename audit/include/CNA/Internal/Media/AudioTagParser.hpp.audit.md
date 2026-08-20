# Audit: include/CNA/Internal/Media/AudioTagParser.hpp

## Metadata
- Source file: `include/CNA/Internal/Media/AudioTagParser.hpp`
- Audit status: AUDITED (full read, 87 lines, header-only declarations)
- Subsystem: `cna-internal-core` shard (paired with `src/CNA/Internal/Media/AudioTagParser.cpp`, audited
  together — see that file's report for the substantive findings)
- File type: C++ header
- XNA/FNA relevance: N/A -- NOXNA, entirely CNA-original functionality (neither XNA nor FNA parses audio tag
  metadata; `Song`'s Title/Artist/Album/Genre/Rating are populated from this from-scratch tag reader,
  plans/plan_media.md MEDIA-47..51/182/183/200/202/206/207/D2/D11)
- Main related tests: not independently located in this pass

## Purpose
Declares a from-scratch audio-tag reader covering Ogg Vorbis-comments, Ogg-Opus tags, native-FLAC
VORBIS_COMMENT blocks, and ID3v2.3/2.4 text frames, with a filename/folder-heuristic fallback, plus embedded
cover-art extraction (ID3v2 APIC / FLAC METADATA_BLOCK_PICTURE).

## Executive Verdict
See `AudioTagParser.cpp`'s report — this header is a clean declaration surface with no logic of its own;
the substantive finding (a 32-bit-only integer-overflow bounds-check bypass) is in the .cpp.

## Checklist Results
Documentation is thorough and candid about CNA-original design decisions (e.g. the POPM 0-255→XNA 0-10
rating conversion, and the Vorbis `RATING` tag's genuinely unstandardized scale, both explicitly flagged as
CNA judgment calls recorded in CHECKLIST.md rather than presented as settled XNA/FNA behavior).

## Detailed Findings
None in this header — see `AudioTagParser.cpp`.

## Cross-File Observations
`AudioTags::hasRating` is correctly distinguished from `rating != 0`, matching XNA's actual
`Song.IsRated`/`Song.Rating` semantic split (a real, easy-to-get-wrong distinction, called out explicitly in
the struct's own doc comment).

## Missing or Weak Tests
Not independently located in this pass; given this is a hand-rolled binary-format parser accepting file
data, fuzz-style/malformed-input test coverage (truncated tags, oversized length fields, wrong encoding
bytes) would be valuable if not already present.

## Positive Findings
Clear, honest documentation distinguishing CNA-original design choices from XNA/FNA-defined behavior.

## Final Assessment
No issues in this header; see the paired `.cpp` report for the shard's substantive finding.
