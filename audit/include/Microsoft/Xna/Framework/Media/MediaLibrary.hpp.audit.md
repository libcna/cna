# Audit: include/Microsoft/Xna/Framework/Media/MediaLibrary.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Media/MediaLibrary.hpp`
- Audit status: AUDITED (full read, 202 lines)
- Subsystem: `xna-media` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; **FNA is NOT authoritative** -- FNA's real `MediaLibrary.cs`
  is a complete stub (15 `NotImplementedException` throws). See `Genre.hpp.audit.md` for the
  shared audit-approach note applying to this whole family.
- Main related tests: not independently located in this pass

## Purpose
Provides the real media-library catalog: scans music/picture roots, owns every `Song`/`Genre`/
`Artist`/`Album`/`Picture`/`PictureAlbum`/`Playlist` object and their collections, and supports
saving new pictures.

## Executive Verdict
Needs attention for one confirmed finding (detailed fully in the paired `.cpp` report):
`SavePicture(name, Stream* source)` doesn't loop to guarantee a full-buffer read, despite this
project's own `System::IO::Stream::Read()` interface explicitly documenting a single call may
return fewer bytes than requested. Everything else in this header is a genuine, careful,
from-scratch implementation of an API surface FNA itself leaves entirely unimplemented -- clear
ownership tracking (every owned object lives in a `std::vector<std::unique_ptr<T>>`, with raw
non-owning pointers handed out through the public collection types), and `EnsureSavedPicturesAlbum()`'s
doc comment explicitly cites a real, previously-fixed bug found by external code review
(`MEDIA-59/D7`: `SavePicture()` used to silently fall back to the root album forever instead of
ever creating the dedicated "Saved Pictures" node).

## Checklist Results

### MEDIUM: `SavePicture(name, Stream*)` assumes a single `Read()` call fills the whole buffer
See `src/Microsoft/Xna/Framework/Media/MediaLibrary.cpp.audit.md` for the full analysis -- the
`Stream*` overload's implementation ignores `Stream::Read()`'s return value and its own documented
weaker contract (a single call may return fewer bytes than requested, per this project's own
`System::IO::Stream::Read()` doc comment), risking a silently-truncated saved picture for any
`Stream` subclass that legitimately returns a partial read.

## Detailed Findings
1. **[MEDIUM] `SavePicture(name, Stream*)` doesn't loop to guarantee a full-buffer read** —
   declared line 148; full analysis in the paired `.cpp` report.

## Cross-File Observations
`BuildPictureAlbumTree()`/`BuildFromRoots()` verified in the paired `.cpp` to correctly delegate to
already-audited `CNA::Internal::Media::MediaLibraryIndex`/`PictureLibraryIndex` (both audited under
`cna-internal-core`).

## Missing or Weak Tests
Not independently located in this pass. A test constructing a `Stream` subclass that deliberately
returns a partial read on its first `Read()` call, then calling `SavePicture(name, &stream)`, would
directly catch the MEDIUM finding.

## Positive Findings
Careful, well-documented ownership design; a real, previously-fixed bug (MEDIA-59/D7) explicitly
attributed to external code review.

## Final Assessment
One MEDIUM finding, detailed fully in the paired `.cpp` report.
