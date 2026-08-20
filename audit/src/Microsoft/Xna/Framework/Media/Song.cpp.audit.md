# Audit: src/Microsoft/Xna/Framework/Media/Song.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/Media/Song.cpp`
- Audit status: AUDITED (full read)
- Subsystem: `xna-media` shard
- File type: C++ implementation
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Media/Song.cs` (read in full) --
  constructor's `FileNotFoundException` behavior verified matching; see paired `.hpp` report for
  the `Album`/`Artist`/`Genre`/`ToString`/`GetHashCode` FNA-non-authoritative caveats
- Main related tests: not independently located in this pass

## Purpose
Implements both constructors (with the file-existence check matching FNA's real behavior),
`Equals`/`GetHashCode`, `ToString()`, `getHandle()`, and `FromUri()`.

## Executive Verdict
Correct, and `ToString()`'s comment is a model of intellectual honesty: it explicitly states this
is "a documented inference from sibling types [Album/Artist/Genre/Playlist, which all return their
own `name_`], NOT a behavior verified against a decompiled XNA binary" -- correctly distinguishing
a reasoned inference from a verified fact, given FNA itself has no `ToString()` to check against.
`FromUri()`'s URI-scheme-detection logic (colon-position heuristic) correctly handles the Windows
drive-letter-vs-URI-scheme ambiguity (`"C:/x"` is a path, not a scheme, since a single-character
"scheme" is never real), citing the specific prior bug this fixes (`plans/plan_media.md MEDIA-217/219`:
the raw string used to go straight to the constructor, which then failed against a literal
`"file:///..."` string every time).

## Checklist Results
No issues found.

## Detailed Findings
None.

## Cross-File Observations
None beyond the paired `.hpp` report.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Explicit acknowledgment of an unverified-but-reasoned inference (`ToString()`'s format) rather than
overstating confidence; a real, well-documented URI-scheme-detection bug fix.

## Final Assessment
No findings.
