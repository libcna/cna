# Audit: examples/demo_devices/android/.../app/jni/src/Android.mk

## Metadata
- Source file: `.../app/jni/src/Android.mk` (21 lines)
- Audit status: AUDITED (full read, cross-checked against `app/build.gradle` and the sibling
  `jni/src/CMakeLists.txt`)
- Subsystem: `examples-demo_devices` shard
- File type: legacy ndk-build target definition
- XNA/FNA relevance: build infrastructure only
- Main related tests: none

## Purpose
The ndk-build equivalent of `jni/src/CMakeLists.txt`: intended to build `Main.cpp`/
`DevicesDemo.cpp`/`DevicesDemo.hpp` into the `main` shared library via the legacy Android.mk build
system.

## Executive Verdict
**HIGH — this file never links `CNA` or `SHARP_RUNTIME` at all.** It declares
`LOCAL_SHARED_LIBRARIES := SDL3` and `LOCAL_LDLIBS := -lGLESv1_CM -lGLESv2 -lOpenSLES -llog
-landroid` (lines 17, 19) — stock SDL-Android-template boilerplate, unmodified for this project —
then compiles `Main.cpp`/`DevicesDemo.cpp` (which reference dozens of CNA/Microsoft::Xna/
Microsoft::Devices symbols) with no CNA library, no `SHARP_RUNTIME`, and no `SDL_PATH`/
`LOCAL_C_INCLUDES` pointing at CNA's own headers at all (only `$(SDL_PATH)/include`, and
`SDL_PATH := ../SDL` — a relative path to a vendored SDL copy this project's own `jni/CMakeLists.txt`
explicitly avoids using, per that file's Task DEVICES-0124 comment). **This ndk-build path would
fail to link** (undefined references to every CNA/XNA/Devices symbol the demo uses) if actually
invoked, and per `app/build.gradle`, ndk-build (not CMake) is the **default** external native build
system — CMake is only used if the `BUILD_WITH_CMAKE` gradle project property is explicitly passed.

Severity note: this is mitigated, but not eliminated, by the fact that `docs/devices-android.md`
and `docs/devices-build.md` (outside this shard) both explicitly document
`./gradlew -PBUILD_WITH_CMAKE assembleDebug` as the real build command — so a reader who consults
those docs first will never hit this. But nothing in this shard's own files (`build.gradle`, this
`Android.mk`, `AndroidManifest.xml`) warns against the plain, undocumented default
`./gradlew assembleDebug`, which this file guarantees will fail to link.

## Checklist Results
- `SDL_PATH := ../SDL` (line 13) points at a vendored SDL copy under `app/jni/SDL` that this
  project's own `jni/CMakeLists.txt` explicitly does NOT use (that file's own comment: "avoids
  configuring/building SDL twice ... instead of building a second, separate SDL from the vendored
  app/jni/SDL copy this template generates by default") — confirming this ndk-build path was never
  adapted for CNA at all; it is the stock, unmodified SDL-Android-project-template skeleton.
- No `include $(CLEAR_VARS)`-scoped `LOCAL_STATIC_LIBRARIES`/additional `LOCAL_C_INCLUDES` entry
  references CNA, `SHARP_RUNTIME`, or the CNA repository root anywhere in this file.

## Detailed Findings

### HIGH — Default (non-CMake) Android build path never links CNA/SHARP_RUNTIME; would fail at link time if invoked
See Executive Verdict above for the full analysis. This is a real, confirmed defect in the project's
own build infrastructure for this demo: the stock ndk-build path, which is the gradle **default**,
was never adapted to actually build this demo — it still targets the generic
SDL-application-template shape (link only SDL3 + GLES/OpenSLES/log/android system libs), unaware
that this specific demo needs the entire CNA/SHARP_RUNTIME static library graph. Downgraded from a
build-breaking HIGH to a documented-workaround HIGH by the existence of `docs/devices-android.md`/
`docs/devices-build.md` (outside this shard), which correctly state the actual required command —
but this file and its siblings in this shard give no indication, on their own, that the gradle
default is broken.

## Cross-File Observations
Direct contrast with `jni/src/CMakeLists.txt` (already audited), which correctly performs
`target_link_libraries(main PRIVATE CNA SHARP_RUNTIME SDL3::SDL3)` — the CMake path is the one that
actually works; this ndk-build path is vestigial stock-template boilerplate that was never adapted.

## Missing or Weak Tests
N/A — build configuration; the "test" here would be an actual Android CI build exercising the
default (non-`-PBUILD_WITH_CMAKE`) gradle invocation, which — per this analysis — would be expected
to fail. Not verified by actually invoking gradle in this pass (no Android SDK/NDK toolchain
available in this audit environment); this finding is based on static analysis of the Makefile
content and the documented, working alternative path.

## Positive Findings
None specific to this file — it is unmodified stock template boilerplate, not a CNA-authored
adaptation.

## Final Assessment
One HIGH finding: this file (and by extension, the top-level `app/jni/Android.mk` that includes all
subdirectory makefiles) never links `CNA`/`SHARP_RUNTIME`, so the gradle-default (non-CMake) Android
build of this demo would fail to link. Mitigated by external documentation
(`docs/devices-android.md`/`docs/devices-build.md`) correctly stating the real required command, but
nothing within this shard's own files discourages or warns against the plain default.
