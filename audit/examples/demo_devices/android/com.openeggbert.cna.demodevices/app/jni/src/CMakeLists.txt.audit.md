# Audit: examples/demo_devices/android/.../app/jni/src/CMakeLists.txt

## Metadata
- Source file: `.../app/jni/src/CMakeLists.txt` (21 lines)
- Audit status: AUDITED (full read)
- Subsystem: `examples-demo_devices` shard
- File type: Android CMake target definition (builds the actual game shared library)
- XNA/FNA relevance: build infrastructure only
- Main related tests: none

## Purpose
Builds `Main.cpp`/`DevicesDemo.cpp`/`DevicesDemo.hpp` into a shared library named `main` (the name
Android's `SDLActivity` expects to `dlopen()`), linking `CNA`, `SHARP_RUNTIME`, and `SDL3::SDL3`.

## Executive Verdict
Correct — this is the file that actually links the CNA library into the Android build (unlike the
sibling ndk-build path, see `jni/src/Android.mk.audit.md` for a real finding about that path's
incompleteness).

## Checklist Results
- `if(NOT TARGET SDL3::SDL3) find_package(SDL3 CONFIG REQUIRED) endif()` (lines 11-13) — the
  guard-then-re-find pattern is correctly explained (idempotent re-import against the parent's
  already-cached `SDL3_DIR`, not a second build) and is a sound, minimal fix for CMake's
  subdirectory-scoped `IMPORTED` target visibility rule.
- `target_link_libraries(main PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)` (line 20) correctly links both
  `CNA` and `SHARP_RUNTIME` — this is what makes this build path actually work, in contrast to the
  sibling `Android.mk`'s equivalent step, which never links either.

## Detailed Findings
None in this file itself.

## Cross-File Observations
**This file's correct `target_link_libraries(main PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)` is the
concrete point of contrast for the real finding in `jni/src/Android.mk.audit.md`**: the legacy
ndk-build path builds the same three sources but only links `SDL3`, never `CNA`/`SHARP_RUNTIME` —
meaning ndk-build (the gradle default unless `-PBUILD_WITH_CMAKE` is passed) would fail to link,
while this CMake path is the one that actually works. See that report for the full write-up.

## Missing or Weak Tests
N/A — build configuration.

## Positive Findings
The `SDL3::SDL3` re-`find_package()` guard comment is a precise, correct explanation of a genuinely
non-obvious CMake scoping subtlety (parent-scope `IMPORTED` targets aren't visible in a sibling
`add_subdirectory()`, even though the underlying cache variable is shared).

## Final Assessment
No findings in this file; it is the correctly-working half of a two-build-system pair whose other
half (`Android.mk`) is broken. See `Android.mk.audit.md` for that finding.
