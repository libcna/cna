# Audit: src/CNA/Internal/Media/PictureLibraryIndex.cpp

## Metadata
- Source file: `src/CNA/Internal/Media/PictureLibraryIndex.cpp`
- Audit status: AUDITED (full read, 136 lines)
- Subsystem: `cna-internal-core` shard
- File type: C++ implementation
- XNA/FNA relevance: N/A -- NOXNA
- Main related tests: not independently located in this pass

## Purpose
Implements the recursive Pictures-root scan: builds one `PictureAlbumNode` per real subdirectory, indexes
supported image files (`.png`/`.jpg`/`.jpeg`/`.bmp`) via `ImageLoader`, with the same symlink-cycle guard
and deterministic-sort conventions as `MediaLibraryIndex`.

## Executive Verdict
Healthy.

## Checklist Results

### Symlink-cycle guard and determinism: correctly re-implemented
Same correct pattern as `MediaLibraryIndex.cpp` (canonical-path `visited` set, sort-before-process) --
independently re-verified here rather than assumed identical.

### Error tolerance
A corrupted/partially-written image file (`ImageLoader::Load` throwing `std::runtime_error`) is correctly
caught per-file (lines 114-131) so one bad picture doesn't abort the whole scan -- matches the class's own
documented "real Pictures folder can contain a corrupted file" rationale.

### `FileLastWriteTime()`'s clock conversion (lines 21-34)
Converts `std::filesystem::file_time_type` to `std::chrono::system_clock::time_point` via
`ftime - file_clock::now() + system_clock::now()` rather than C++20's `clock_cast` (the header comment notes
`clock_cast` isn't universally available across this project's supported toolchains). This is the standard
pre-`clock_cast` workaround idiom; it introduces a sub-microsecond timing error between the two back-to-back
`now()` calls, which is immaterial for a picture's displayed "date taken" field. Not scored as a defect.

## Detailed Findings
None.

## Cross-File Observations
Correctly reuses the same hardening patterns established in `MediaLibraryIndex.cpp` (same shard) rather than
diverging on cycle-safety or ordering.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Correct per-file error tolerance (one corrupt image doesn't abort the scan); correctly re-applies this
shard's established symlink-cycle and determinism hardening.

## Final Assessment
No issues found.
