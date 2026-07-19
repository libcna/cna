# Audit: src/CNA/Internal/Media/SavedPictureStore.cpp

## Metadata
- Source file: `src/CNA/Internal/Media/SavedPictureStore.cpp`
- Audit status: AUDITED (full read, 94 lines)
- Subsystem: `cna-internal-core` shard
- File type: C++ implementation
- XNA/FNA relevance: N/A -- NOXNA
- Main related tests: not independently located in this pass

## Purpose
Implements `GetSavedPicturesDirectory()`/`SavePicture()`: creates a "Saved Pictures" subfolder under the
Pictures root and writes caller-supplied image bytes to a sanitized filename inside it, with the extension
sniffed from the image's own magic bytes.

## Executive Verdict
Healthy -- a genuinely careful path-traversal defense on the untrusted `name` parameter.

## Checklist Results

### Path-traversal defense: correctly implemented
`SanitizePictureName()` (lines 38-50) normalizes backslashes to forward slashes first (defending against
Windows-style traversal separators even where `std::filesystem::path` wouldn't itself treat `\` as a
separator), extracts only `std::filesystem::path::filename()` (discarding any directory component a
caller-supplied name might contain), and rejects empty/"."/".." results in favor of a safe `"picture"`
fallback -- correctly closing off directory-traversal and absolute-path-escape vectors on a name that is
explicitly documented as caller-supplied and untrusted.

### Resource/error handling
`GetSavedPicturesDirectory()` correctly treats "directory creation failed AND doesn't already exist" as
failure (`ec && !std::filesystem::exists(dir)`) rather than failing outright on a benign
already-exists-adjacent race; `SavePicture()` checks both `ofstream::is_open()` and post-write `good()`
before reporting success.

## Detailed Findings
None.

## Cross-File Observations
Similar in spirit to `CnjSourceFile.hpp`'s traversal defense (same shard) but simpler, since this is a
single-filename-segment sanitizer rather than a full relative-path resolver -- both correctly reject
untrusted path input from escaping their respective target directories.

## Missing or Weak Tests
Not independently located in this pass; dedicated tests for `SanitizePictureName()`'s traversal-rejection
paths (`"../../etc/passwd"`, absolute paths, bare `".."`) would be valuable if not already present.

## Positive Findings
Careful, correctly-implemented sanitization of untrusted filename input before it reaches the filesystem.

## Final Assessment
No issues found.
