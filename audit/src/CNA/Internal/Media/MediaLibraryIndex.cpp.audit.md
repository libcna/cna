# Audit: src/CNA/Internal/Media/MediaLibraryIndex.cpp

## Metadata
- Source file: `src/CNA/Internal/Media/MediaLibraryIndex.cpp`
- Audit status: AUDITED (full read, 143 lines)
- Subsystem: `cna-internal-core` shard
- File type: C++ implementation
- XNA/FNA relevance: N/A -- NOXNA
- Main related tests: not independently located in this pass

## Purpose
Implements the recursive Music-root scan: symlink-cycle guard via a canonical-path `visited` set,
permission-denied-tolerant directory iteration, deterministic sort-before-processing, and case-insensitive
Artist/Genre canonicalization.

## Executive Verdict
Healthy.

## Checklist Results

### Symlink-cycle guard: correctly implemented
`ScanDirectory()` (lines 55-107) canonicalizes each directory via `weakly_canonical()` and inserts into a
shared `visited` set before recursing, correctly using the *canonical* (symlink-resolved) path as the
dedup key rather than the raw traversal path -- a naive "have I visited this literal path string" guard
would miss a cycle formed via a symlink pointing back to an ancestor by a different apparent path; this
implementation correctly catches that case.

### Determinism
Directory entries are collected into a vector and sorted by path before processing (lines 82-90) --
correctly compensates for `directory_iterator`'s filesystem-dependent (non-alphabetical) enumeration order,
which matters here specifically because sort order determines which casing variant "wins" the D10
Artist/Genre canonicalization when the same case-folded name appears with different casing across files.

### Minor observation: unbounded recursion depth
`ScanDirectory()` recurses per subdirectory with no explicit depth limit. A pathologically deep directory
tree (thousands of nested folders) could in principle exhaust the call stack. This is a low-likelihood
concern for a real user's Music folder and matches how this kind of recursive scanner is conventionally
written elsewhere; not scored as an actionable defect.

### Supported-extension list
`HasSupportedAudioExtension()` is deliberately narrower than "every audio extension" -- explicitly verified
against the project's actual SDL3_mixer decoder set (documented in the source comment, not merely
asserted), correctly excluding `.m4a`/`.aac` since SDL3_mixer ships no AAC decoder at all.

## Detailed Findings
None rising to actionable severity (see the minor recursion-depth observation above).

## Cross-File Observations
Same symlink-cycle-guard and deterministic-sort pattern independently re-implemented (correctly) in
`PictureLibraryIndex.cpp`.

## Missing or Weak Tests
Not independently located in this pass; a symlink-cycle regression test would be valuable given this is
exactly the kind of hardening that regresses silently if refactored.

## Positive Findings
Correct symlink-cycle guard using canonical paths as the dedup key; deterministic, filesystem-order-
independent scan results.

## Final Assessment
No issues found; one non-actionable observation (unbounded recursion depth) documented above.
