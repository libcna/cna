# Audit: src/CNA/Internal/Xnb/SongContentTypeReader.cpp

## Metadata
- Source file: `src/CNA/Internal/Xnb/SongContentTypeReader.cpp`
- Audit status: AUDITED (full read, 101 lines)
- Subsystem: `cna-internal-core` shard (Xnb)
- File type: C++ implementation
- XNA/FNA relevance: matches FNA's `SongReader.Normalize()`/`FileHelpers.ResolveRelativePath`
- Main related tests: not independently located in this pass

## Purpose
Implements `SongReader::Read()`: resolves the `.xnb`'s stored reference string to a real file path
(stripping/re-probing supported extensions), falling back to the un-stripped path if nothing resolves.

## Executive Verdict
Healthy -- correct FNA-parity port. One LOW-priority informational cross-reference: `ResolveRelativeFilePath()`
performs no containment check against the content root, the same general shape as `PlaylistParser.cpp`'s
already-flagged MEDIUM finding (different shard) -- but here it is presumably genuine FNA-parity behavior
(matching FNA's own `SongReader`/`FileHelpers.ResolveRelativePath`, which this project's own guidelines
correctly prioritize preserving over an independent CNA security hardening choice), not a NOXNA design gap
this codebase introduced independently. Not independently re-verified against the FNA reference tree in
this pass.

## Checklist Results

### FNA parity: `Normalize()`/extension-reprobe logic verified correct in shape
`kSupportedExtensions` (`.ogg`/`.oga`/`.qoa`), the "return unchanged if it already names a real file, else
try each supported extension, else empty" fallback chain, and the "un-stripped path if nothing resolves"
final fallback all match the documented FNA `SongReader.Normalize()` contract.

### Path-resolution permissiveness: same shape as `PlaylistParser.cpp`, likely faithful FNA parity here
`ResolveRelativeFilePath()` joins the reference string against the asset's own directory and
lexically-normalizes the result with no check that it stays within the content root -- structurally
identical to `PlaylistParser.cpp`'s already-flagged gap (same audit, `cna-internal-core` Media subsystem).
The key difference: `PlaylistParser` is a NOXNA CNA-original feature (no FNA reference to defer to, so the
inconsistency with this codebase's own `CnjSourceFile.hpp`/`SavedPictureStore.cpp` traversal defenses is a
genuine, actionable gap there); `SongReader` is a real FNA class whose own reference behavior this project's
guidelines explicitly prioritize matching. If real FNA's own `SongReader`/`FileHelpers.ResolveRelativePath`
has the identical lack of containment checking (plausible, given `.xnb` content was never designed to be
treated as adversarial input in the original XNA content pipeline's threat model), then this is correct,
intentional FNA-parity behavior, not a bug to independently harden. Flagged as informational rather than
actionable because this was not independently cross-checked against the FNA reference tree in this pass.

### `Song` constructor's own eager validation: correctly documented as pre-existing, not reader-introduced
The header's own doc comment correctly attributes the "throws immediately if the final path doesn't exist"
behavior to `Song`'s own constructor (a pre-existing CNA behavior), not something this reader adds -- an
honest attribution rather than claiming credit/blame for behavior that lives elsewhere.

## Detailed Findings
None actionable in this file; one LOW-priority informational cross-reference (see above).

## Cross-File Observations
See `PlaylistParser.cpp`'s report (Media subsystem, same shard) for the structurally identical
path-containment gap in a NOXNA context, where it IS scored as an actionable MEDIUM finding.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Correct FNA-parity port of a subtle historical extension-resolution quirk, with honest documentation of
which behaviors are this reader's own versus inherited from `Song`'s pre-existing constructor.

## Final Assessment
No actionable issues found in this pass; one informational cross-reference noted for context.
