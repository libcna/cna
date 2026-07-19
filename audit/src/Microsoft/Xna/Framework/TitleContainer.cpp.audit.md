# Audit: src/Microsoft/Xna/Framework/TitleContainer.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/TitleContainer.cpp`
- Audit status: AUDITED (full read, 189 lines; path-resolution/containment behavior cross-checked directly
  against `/rv/data/library/github.com/FNA-XNA/FNA/src/TitleContainer.cs`)
- Subsystem: `xna-framework-core` shard
- File type: C++ implementation
- XNA/FNA relevance: matches real XNA `TitleContainer.OpenStream()`'s exact path-resolution behavior,
  including its intentional lack of path-containment restriction
- Main related tests: not independently located in this pass

## Purpose
Implements `OpenStream()` (file-path normalization, absolute-vs-relative resolution against
`TitleLocation::Path`, Android SDL-asset fallback) and the `NOXNA` raw-pointer `ReadToPointer`/`FreePointer`
helpers.

## Executive Verdict
Healthy -- directly verified this file's path resolution (no containment check against the title root;
absolute paths are honored as-is) is byte-for-byte consistent with FNA's own real `TitleContainer.
OpenStream()`, which has the identical lack of restriction by design.

## Checklist Results

### CONFIRMED FNA-faithful: no path-containment restriction, by design
`ResolveRealPath()`/`CombineTitlePath()` perform no containment check against `TitleLocation::Path` --
a relative `name` containing `..` segments can resolve outside the title directory, and an absolute `name`
is honored exactly as given. Directly verified against FNA's real `TitleContainer.OpenStream()`
(`/rv/data/library/github.com/FNA-XNA/FNA/src/TitleContainer.cs`, lines 32-51): FNA's own implementation
does exactly the same thing -- `Path.IsPathRooted(safeName)` directly opens an absolute path with zero
restriction, and a relative path is simply `Path.Combine`d with `TitleLocation.Path` with no subsequent
containment check either. This is intentional, documented XNA behavior (a low-level Stream-opening
primitive meant to let trusted, first-party game code open any file it chooses -- not a boundary meant to
resist adversarial path input), not an oversight this port introduced. Correctly contrasts with this
project's own NOXNA `PlaylistParser.cpp` (different shard), where the identical "no containment check"
shape *was* scored as an actionable gap specifically because that file is CNA-original code without an FNA
reference to defer to.

### `NormalizeFilePathSeparators`/`IsPathRooted`: correct, matching FNA's own equivalent helpers
Backslash-to-forward-slash normalization and the rooted-path detection (leading `/`, or a Windows drive
letter + `:` + separator) match the semantics FNA's own `MonoGame.Utilities.FileHelpers`/`Path.IsPathRooted`
provide.

### `ReadToPointer()`: correct manual memory management
`malloc`/`free`-based raw buffer allocation with correct error handling on both allocation failure and a
short/failed read (freeing the partially-used buffer before throwing) -- appropriate for a `NOXNA` C-style
interop helper explicitly documented as needing manual `FreePointer()` cleanup by its caller.

## Detailed Findings
None.

## Cross-File Observations
This is a second, source-verified confirmation (after `TitleLocation`-adjacent path handling) that XNA's
own `TitleContainer`/content-loading primitives are intentionally unrestricted with respect to path
containment -- reinforces that the `PlaylistParser.cpp`/`SongContentTypeReader.cpp`/
`VideoContentTypeReader.cpp` findings elsewhere in this audit should be judged by whether they're genuine
XNA API surface (where this lack of restriction is expected/faithful) or NOXNA CNA-original code (where the
project has full latitude to choose stricter behavior, and the established sibling files that DO restrict
make the inconsistency worth a decision).

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Confirmed, via direct FNA source comparison, that this file's path-handling behavior exactly matches real
XNA's own intentional design rather than being an accidental gap.

## Final Assessment
No issues found; confirms this file's path-resolution behavior is correct, FNA-faithful design, not a
security gap.
