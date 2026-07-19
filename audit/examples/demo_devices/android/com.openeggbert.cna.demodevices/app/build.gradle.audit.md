# Audit: examples/demo_devices/android/.../app/build.gradle

## Metadata
- Source file: `.../app/build.gradle` (67 lines)
- Audit status: AUDITED (full read)
- Subsystem: `examples-demo_devices` shard
- File type: Gradle module build script
- XNA/FNA relevance: build infrastructure only
- Main related tests: none

## Purpose
Configures the Android app module: SDK/NDK versions, ABI filters, and the choice between the
CMake (`jni/CMakeLists.txt`) and ndk-build (`jni/Android.mk`) native build systems, selected via the
`BUILD_WITH_CMAKE` gradle project property (`def buildWithCMake = project.hasProperty('BUILD_WITH_CMAKE')`,
line 5).

## Executive Verdict
**This is the file that establishes the confirmed HIGH finding in `app/jni/src/Android.mk.audit.md`:
ndk-build (not CMake) is the default** (`buildWithCMake` is only true if the caller explicitly
passes `-PBUILD_WITH_CMAKE`), and the ndk-build path never links `CNA`/`SHARP_RUNTIME`. Everything
else in this file (SDK/NDK version pinning, ABI filter, proguard config) is correct and consistent
with the project's own documented build requirements (`docs/devices-build.md` Section 4, cited in
the `ndkVersion` comment).

## Checklist Results
- `ndkVersion = "30.0.14904198"` (line 13) is explicitly justified by comment as matching the NDK
  version this project already builds/tests CNA against, with a citation
  (`docs/devices-build.md` Section 4) rather than an arbitrary pin.
- `abiFilters 'arm64-v8a'` is applied identically to both the `ndkBuild` and `cmake`
  `externalNativeBuild.defaultConfig` blocks (lines 23, 28) — consistent between the two paths,
  even though only one is ever actually used per build invocation.
- `minifyEnabled false` in the `release` build type (line 34) means the carefully-scoped
  `-keep` rules in `proguard-rules.pro` are currently inert (ProGuard/R8 minification is off) — not
  a defect per se (a reasonable, conservative default for an example demo), but worth noting
  alongside that file's own report.

## Detailed Findings
See `app/jni/src/Android.mk.audit.md` for the full HIGH write-up this file's `buildWithCMake` default
directly causes. Not re-scored here to avoid double-counting the same underlying defect across
multiple files in this shard.

## Cross-File Observations
- The `buildWithCMake` conditional (lines 46-56) is the single point of control for which of the two
  build systems (only one of which actually works, per `Android.mk.audit.md`) gets invoked.
- Confirms `proguard-rules.pro`'s keep rules are currently inert due to `minifyEnabled false`.

## Missing or Weak Tests
N/A — build configuration; not independently verified against a real gradle invocation in this
audit environment (no Android SDK/NDK toolchain available).

## Positive Findings
The `ndkVersion`/SDK-level pins are precisely justified with a citation to the project's own build
documentation, not arbitrary magic numbers.

## Final Assessment
No new findings scored here (see `Android.mk.audit.md` for the HIGH this file's default causes);
one Cross-File Observation about `proguard-rules.pro`'s currently-inert keep rules.
