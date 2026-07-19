# Audit: examples/demo_devices/android/.../app/jni/Application.mk

## Metadata
- Source file: `.../app/jni/Application.mk` (11 lines)
- Audit status: AUDITED (full read)
- Subsystem: `examples-demo_devices` shard
- File type: ndk-build application-wide settings
- XNA/FNA relevance: build infrastructure only
- Main related tests: none

## Purpose
Sets ndk-build ABI list (`armeabi-v7a arm64-v8a x86 x86_64`) and minimum platform (`android-21`) for
the (non-functional, see `Android.mk.audit.md`) ndk-build path.

## Executive Verdict
Stock template boilerplate; internally consistent with itself but inconsistent with
`app/build.gradle`'s `defaultConfig.minSdkVersion 24` (this file says `android-21`) and
`abiFilters 'arm64-v8a'` only (this file lists 4 ABIs) — moot in practice since ndk-build is broken
regardless (see `Android.mk.audit.md`), but worth noting as a second, independent sign this file was
never updated to match the rest of the project's actual configuration.

## Checklist Results
- `APP_PLATFORM=android-21` vs. `build.gradle`'s `minSdkVersion 24` — a real, if currently
  inconsequential, mismatch (ndk-build's own `arguments "APP_PLATFORM=android-24"` override in
  `build.gradle`'s `externalNativeBuild.ndkBuild` block actually takes precedence at build time per
  standard AGP behavior, so this file's `android-21` is effectively dead configuration, not a live
  conflict).
- ABI list (4 architectures) vs. `build.gradle`'s `abiFilters 'arm64-v8a'` (1 architecture) — again,
  the gradle-level `abiFilters` restricts what's actually built regardless of what this file lists.

## Detailed Findings

### LOW — Stale, superseded ABI/platform-level settings not updated to match `build.gradle`'s actual configuration
Not a functional defect (gradle's own `externalNativeBuild` arguments/`abiFilters` override this
file's values at build time), but a real staleness signal consistent with this entire ndk-build path
having been left as unmodified template boilerplate (see `Android.mk.audit.md`'s HIGH finding for
the same underlying cause: this path was never adapted for CNA).

## Cross-File Observations
Reinforces the `Android.mk.audit.md` finding: this whole `jni/` (non-`src`) directory subtree is
generic SDL-Android-template scaffolding CNA's own build (`jni/CMakeLists.txt`/`jni/src/CMakeLists.txt`)
correctly bypasses, while the parallel ndk-build path was left unmodified.

## Missing or Weak Tests
N/A.

## Positive Findings
None specific to this file.

## Final Assessment
One LOW finding: stale ABI/platform settings, superseded (but not contradicted at build time) by
`build.gradle`'s own configuration — consistent with the broader ndk-build-path-never-adapted
pattern already flagged as HIGH in `jni/src/Android.mk.audit.md`.
