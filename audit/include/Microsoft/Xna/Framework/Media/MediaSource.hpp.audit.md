# Audit: include/Microsoft/Xna/Framework/Media/MediaSource.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Media/MediaSource.hpp`
- Audit status: AUDITED (full read, 57 lines)
- Subsystem: `xna-media` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; **FNA is largely NOT authoritative** -- FNA's real
  `MediaSource.cs` is a near-complete stub (5 `NotImplementedException` throws in a 36-line file).
  See `Genre.hpp.audit.md`.
- Main related tests: not independently located in this pass

## Purpose
Represents a media source device; `GetAvailableMediaSources()` enumerates them.

## Executive Verdict
Needs a minor note. `GetAvailableMediaSources()`'s doc comment ("Returns all available media
sources on the current device. @return Vector of available MediaSource pointers.") does not state
who owns the returned pointers, unlike several sibling factory-style methods elsewhere in this
codebase that are explicit about caller ownership (e.g. `Song::FromUri()`'s equivalent doc, and
`Video::FromUriEXT()` in this same shard). The implementation (audited in the paired `.cpp`)
allocates with `new` and hands ownership to the caller, so this is a real, if minor, documentation
gap rather than a functional defect.

## Checklist Results

### LOW: `GetAvailableMediaSources()`'s doc comment doesn't state pointer ownership
See Executive Verdict. A further instance of the same "raw owning pointer without an explicit
ownership disclosure" pattern already noted for `SoundBank::GetCue()` in the `xna-audio` shard,
though here the gap is in the doc comment specifically (other similar factory methods in this same
namespace, e.g. `Video::FromUriEXT()`, do include the disclosure).

## Detailed Findings
1. **[LOW] `GetAvailableMediaSources()`'s doc comment omits an ownership statement present on
   sibling factory methods** — declared line 38.

## Cross-File Observations
Contrast with `Video::FromUriEXT()` (audited separately, same shard), whose doc comment explicitly
states "Pointer to the newly created Video" with the implicit caller-owns convention this whole
codebase uses consistently for factory-style raw-pointer returns -- `MediaSource::GetAvailableMediaSources()`
is the one instance in this shard missing that same disclosure.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Correct implementation; the one-real-source-on-desktop design (audited in the `.cpp`) is a
reasonable, disclosed simplification for a platform with no real device-enumeration concept.

## Final Assessment
One LOW, documentation-only finding.
