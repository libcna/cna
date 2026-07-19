# Audit: include/Microsoft/Xna/Framework/Media/Song.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Media/Song.hpp`
- Audit status: AUDITED (full read, 251 lines)
- Subsystem: `xna-media` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Media/Song.cs` (195 lines, read in
  full) -- **FNA is only partially authoritative here**: per this project's established feedback
  ("FNA is NOT authoritative for API surface" -- FNA omits real XNA members
  `Song.Album`/`Artist`/`Genre`/`ToString`, verified previously against the xn65 reference), and
  confirmed directly in this pass: FNA's real `Song.cs` has no `Album`/`Artist`/`Genre` properties
  and no `ToString()` override at all. CNA correctly includes all four, matching real documented
  XNA rather than FNA's incomplete port.
- Main related tests: not independently located in this pass

## Purpose
Represents a playable song: name, album/artist/genre (when from a library scan), duration,
protection/rating/track-number metadata, play count.

## Executive Verdict
Correct, and this file is a strong example of principled divergence from FNA where FNA itself is
known to be incomplete. `getIsProtectedProperty()`'s comment is a good example of an honest,
reasoned "always false" answer rather than an unimplemented stub: "the *correct* answer for
everything CNA can index... a DRM-wrapped file... is not indexable in the first place, so no
indexed song can ever be protected." `getIsRatedProperty()`/`getRatingProperty()`/
`getTrackNumberProperty()` are genuinely populated from real file tag metadata (via
`CNA::Internal::Media::AudioTagParser`, audited under `cna-internal-core`) for library-scanned
songs -- real functionality beyond FNA's hardcoded `false`/`0`/`0` stub values for these same
properties. `GetHashCode()`'s doc comment (lines 174-185) explicitly and correctly identifies a real
`Equals`/`GetHashCode` contract violation in FNA's own implementation (`base.GetHashCode()` is
identity-based while `Equals()` is handle-based, so two FNA Songs that are `Equals`-equal can
legitimately hash differently) and states plainly this is "a documented, beneficial deviation, not
an unported detail" -- correctly choosing NOT to replicate a real FNA bug.

## Checklist Results
No issues found.

## Detailed Findings
None.

## Cross-File Observations
`rating_`/`trackNumber_`'s tag-parsing source, `CNA::Internal::Media::AudioTagParser`, carries a
previously-recorded HIGH-severity 32-bit-overflow finding (from `cna-internal-core`) -- confirms
that finding is reachable via `Song`'s own XNA-facing properties for a library-scanned song, not
merely a theoretical internal-only concern.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Correctly includes real XNA members FNA itself omits (`Album`/`Artist`/`Genre`/`ToString`); provides
genuine tag-based metadata where FNA hardcodes stub values; explicitly and correctly declines to
replicate a real FNA `Equals`/`GetHashCode` contract violation, with clear reasoning for the
deviation.

## Final Assessment
No findings (cross-references an already-recorded finding in a dependency).
