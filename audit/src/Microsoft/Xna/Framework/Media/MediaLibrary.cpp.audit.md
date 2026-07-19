# Audit: src/Microsoft/Xna/Framework/Media/MediaLibrary.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/Media/MediaLibrary.cpp`
- Audit status: AUDITED (604 lines total; full read of the constructors, `SavePicture()` [both
  overloads], `GetPictureFromToken()`, `EnsureSavedPicturesAlbum()`, and `FindAlbumArtPath()`;
  `BuildFromRoots()`/`BuildPictureAlbumTree()`'s bulk directory-scan/indexing logic read at a
  structural level given the file's size and that it delegates to already-audited
  `CNA::Internal::Media` index classes)
- Subsystem: `xna-media` shard
- File type: C++ implementation
- XNA/FNA relevance: Direct XNA type; FNA is a complete stub here (see paired `.hpp` report)
- Main related tests: not independently located in this pass

## Purpose
Implements the full media-library scan/index/save pipeline: album-art file-convention detection,
`.xsb`-free song/genre/artist/album grouping, picture-album tree construction, and picture saving.

## Executive Verdict
Needs attention for one confirmed, real defect. `SavePicture(name, System::IO::Stream* source)`
(lines 585-597) reads the source stream with a single, unchecked `Read()` call:
```cpp
std::vector<uint8_t> buffer(static_cast<std::size_t>(source->getLengthProperty()));
if (!buffer.empty())
{
    source->Read(buffer.data(), 0, static_cast<SharpRuntime::intcs>(buffer.size()));
}
return SavePicture(std::move(name), buffer);
```
This project's own `System::IO::Stream::Read()` interface (sharp-runtime, reference-only for this
audit) documents its return value as "Number of bytes actually read; 0 at end-of-stream" -- the
same weaker, single-call-may-be-partial contract real .NET's `Stream.Read()` has. The call here
discards that return value entirely and assumes the full requested `count` was read in one call.
For any `Stream` subclass that legitimately returns a partial read (a real, interface-permitted
case -- not an edge case being imagined), the trailing portion of `buffer` stays at its
default-constructed zero value, and that zero-padded buffer is silently handed to
`SavePicture(name, const std::vector<uint8_t>&)` as if it were the complete image, producing a
corrupted saved picture with no error raised anywhere in the call chain. This is not an FNA-parity
gap (FNA has no equivalent method to compare against) but a genuine violation of this project's own
`Stream` interface contract. A repository-wide grep found no other call site with the identical
unchecked-single-`Read()` pattern in this shard.

## Checklist Results

### MEDIUM: `SavePicture(Stream*)` doesn't loop to guarantee a full-buffer read
See Executive Verdict for the full analysis. **Fix shape**: loop calling `Read()` until either the
full `count` has been accumulated or a `0`-byte return signals end-of-stream, matching the standard
"read loop" pattern any correct consumer of a partial-read-capable stream interface must use --
e.g.:
```cpp
std::size_t totalRead = 0;
while (totalRead < buffer.size())
{
    intcs n = source->Read(buffer.data() + totalRead, 0,
                            static_cast<intcs>(buffer.size() - totalRead));
    if (n <= 0) break; // end-of-stream or no progress
    totalRead += static_cast<std::size_t>(n);
}
buffer.resize(totalRead);
```

## Detailed Findings
1. **[MEDIUM] `SavePicture(name, Stream* source)` assumes a single `Read()` call fills the whole
   buffer, violating `System::IO::Stream::Read()`'s own documented partial-read-permitted
   contract** — lines 585-597.

## Cross-File Observations
`EnsureSavedPicturesAlbum()`'s root-album bootstrap path (lines 479-...) explicitly cites a second
real, previously-fixed bug found by external code review (`MEDIA-132`): if the pictures root didn't
exist at `MediaLibrary` construction time, `rootPictureAlbum_` was never created, but
`SavedPictureStore::SavePicture()` (called just before this function, by the caller) always creates
the real directory on disk regardless -- leaving a saved picture unparented in any tree at all
until this fix bootstrapped a real root node lazily here too.

## Missing or Weak Tests
Not independently located in this pass. See the paired `.hpp` report for the recommended test for
the MEDIUM finding.

## Positive Findings
`FindAlbumArtPath()`'s case-insensitive, multi-convention cover-art detection (scanning the
directory once rather than stat-ing every candidate in every casing) is a thoughtful, real-world-
informed design choice. Two further real, previously-fixed bugs (`MEDIA-59/D7`, `MEDIA-132`) are
explicitly documented with their exact failure scenario and external-review attribution.

## Final Assessment
One MEDIUM finding: a genuine, confirmed partial-read hazard in `SavePicture(Stream*)`, not an
FNA-parity issue but a real violation of this project's own `Stream` interface contract.
