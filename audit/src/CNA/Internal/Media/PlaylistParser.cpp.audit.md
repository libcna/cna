# Audit: src/CNA/Internal/Media/PlaylistParser.cpp

## Metadata
- Source file: `src/CNA/Internal/Media/PlaylistParser.cpp`
- Audit status: AUDITED (full read, 107 lines)
- Subsystem: `cna-internal-core` shard
- File type: C++ implementation
- XNA/FNA relevance: N/A -- NOXNA
- Main related tests: not independently located in this pass

## Purpose
Implements `Parse()` (reads one `.m3u`/`.m3u8` file, resolving relative entries against the playlist's own
directory, skipping missing entries) and `ScanDirectory()` (non-recursive scan for playlist files, sorted
for deterministic output).

## Executive Verdict
Needs attention -- one MEDIUM-severity path-containment finding, in contrast to sibling files in the same
subsystem that do defend against path escape.

## Checklist Results

### MEDIUM: no path-containment check on playlist entries (confused-deputy potential)
`Parse()` (lines 30-67) resolves each non-comment line as a path with zero containment checking: an
absolute-path entry is used exactly as written (line 53's `is_relative()` check means it is **not** joined
with `baseDir` at all in that case), and a relative entry containing `..` segments is joined with `baseDir`
via plain `operator/` with no subsequent check that the result stays under `baseDir` (or under the Music
root) -- unlike `CnjSourceFile.hpp`'s `ResolveCnjSourceFileSafely()` (root-containment check, absolute-path
rejection) or `SavedPictureStore.cpp`'s `SanitizePictureName()` (single-filename-segment extraction), both
audited earlier in this shard, which sanitize comparable untrusted path input.

This is standard, expected M3U-format behavior (real players like VLC/Winamp also allow absolute and
`..`-relative playlist entries, since users legitimately reference tracks stored outside the playlist's own
folder) -- not a bug relative to the M3U format's own conventions. It is flagged here because of a genuine,
if narrow, confused-deputy angle specific to this codebase: a hostile `.m3u`/`.m3u8` file placed by any
means into the user's Music library (`ScanDirectory()`'s scan root) can reference an arbitrary absolute path
elsewhere on the filesystem; if that path exists (`std::filesystem::exists()`, line 59), it is added to
`songPaths` unconditionally and will subsequently be probed/decoded as audio (via `AudioDurationProbe`/
`AudioTagParser`/SDL3_mixer). A user who could not otherwise cause CNA to open a given file (no direct
write access to trigger it, but knows a path exists and is readable by the running process) could use a
`.m3u` drop to make the trusted process open and feed that file to the FFmpeg/SDL3_mixer decoder --
worthwhile only if a decoder-side vulnerability exists to trigger, but the containment gap itself is real
and inconsistent with this codebase's own established practice elsewhere in the same subsystem.

**Fix shape**: if playlist entries are intended to reference only files within the Music library (matching
this project's own established pattern for comparable untrusted-path features), resolve and then verify
containment the same way `CnjSourceFile.hpp` does; if cross-folder playlist references are an intentional,
accepted feature (plausible, since standard M3U supports it), record that as a deliberate decision in
CHECKLIST.md/AUDIT_DECISIONS.md-equivalent documentation so the asymmetry with `CnjSourceFile`/
`SavedPictureStore` reads as intentional rather than an oversight.

### Everything else: correct
`HasSupportedPlaylistExtension()`-equivalent (`HasPlaylistExtension()`) case-folds correctly;
`ScanDirectory()` correctly sorts directory entries before processing for deterministic, filesystem-order-
independent output (matching `MediaLibraryIndex`/`PictureLibraryIndex`'s own established convention in this
same shard); `Trim()` correctly handles the all-whitespace/empty-string edge case (`npos` check before
`substr`).

## Detailed Findings

1. **[MEDIUM] No path-containment check on `.m3u`/`.m3u8` playlist entries** (see above). File: `Parse()`,
   lines 30-67.

## Cross-File Observations
See `PlaylistParser.hpp`'s report cross-reference to `CnjSourceFile.hpp`/`SavedPictureStore.cpp`.

## Missing or Weak Tests
Not independently located in this pass; no test located exercising an absolute-path or `..`-escaping
playlist entry.

## Positive Findings
Correct, deterministic directory-scan ordering; correct M3U/M3U8 comment-line and blank-line handling.

## Final Assessment
One MEDIUM-severity finding: no path-containment check on playlist entries, inconsistent with this
subsystem's own established untrusted-path-handling practice elsewhere.
