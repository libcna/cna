# Android Graphics Renderer — Status and Limitations

CNA defaults to the `SDL_RENDERER` graphics renderer on Android (confirmed directly: `CMakeLists.txt`'s
renderer-selection logic is `if(EMSCRIPTEN) WEBGL2 elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux") OPENGLES3 else
SDL_RENDERER` — Android's `CMAKE_SYSTEM_NAME` is `"Android"`, not `"Linux"`, even under the NDK
toolchain, so it falls into the `SDL_RENDERER` branch). This document is the Task 460 status
write-up for Android graphics support specifically — audio, input, and sensor/device support on
Android are already covered in detail elsewhere (`docs/devices-build.md`, `noxna_devices.md`,
`plan_devices*.md`) and are out of scope here.

## Status headline: the Android cross-compile currently fails before reaching any CNA/graphics code

Re-ran the exact Android cross-compile steps documented in `docs/devices-build.md` §4 live, against
this session's own checkout, to verify current status rather than trust the existing (2026-07-05/06
dated) write-up:

```bash
cmake -S . -B cmake-build-android -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$HOME/Android/Sdk/ndk/30.0.14904198/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 -DCNA_BUILD_TESTS=OFF
cmake --build cmake-build-android -j"$(nproc)"
```

Configure succeeds and confirms `SDL_RENDERER` is selected, as expected. **The build itself fails**
— not in any CNA or graphics-renderer source file, but in 2 unrelated files belonging to the sibling
`sharp-runtime` repository, before CNA's configured library graph can even
finish, so **`SdlRenderer.cpp`'s own Android buildability is currently unverified** — the
build never gets far enough to attempt it:

1. `sharp-runtime/src/System/IO/FileStream.cpp` (via `FileStream.hpp:25`): `error: private field
   'mode_' is not used [-Werror,-Wunused-private-field]`. This build enables `-Werror`; the desktop
   Linux build apparently does not trip this specific warning (likely a `clang`-version or
   optimization-level difference between the NDK's bundled `clang` and the host compiler used for
   the desktop `cmake-build-debug`/etc. configurations in this same session).
2. `sharp-runtime/src/System/IO/FileSystemInfo.cpp:31,90`: `error: no member named 'clock_cast' in
   namespace 'std::chrono'`. The Android NDK's bundled libc++ (as of NDK 30.0.14904198, the version
   used here) does not yet implement C++20's `std::chrono::clock_cast`, which this file uses
   unconditionally to convert between `std::filesystem::file_clock` and `std::chrono::system_clock`.

**Both bugs are in `sharp-runtime`, a separate sibling repository — not in this repository
(`cna_graphics`) — and are unrelated to any graphics-specific code or feature.** Fixing them is
outside this documentation task's own scope (a different repo, unrelated to XNA/graphics API work)
and is tracked separately as new Task 920 below rather than fixed here.

This is a **regression against `docs/devices-build.md`'s own dated write-up** (§4, dated
2026-07-05/06 under `plan_devices_phase4.md`/`plan_devices_phase7.md`/`plan_devices_phase8.md`,
which documented this cross-compile succeeding for the configured CNA libraries at that
time) — `sharp-runtime` is a separate, independently-evolving repository, and something committed
there since has broken NDK compatibility. This document does not attempt to identify which
`sharp-runtime` commit introduced it (out of scope — a different repo's own history).

## What is and isn't verified as a result

- **Verified**: `SDL_RENDERER` is genuinely selected as the Android default (configure-time
  confirmation, `-- CNA: Using SDL_RENDERER graphics renderer`).
- **NOT verified, currently unknown**: whether `SdlRenderer.cpp` (or any other CNA graphics
  source file) compiles cleanly for `arm64-v8a`/Android at all — the build fails one dependency
  layer before ever reaching it. Every claim in `docs/sdl-renderer-2d-completeness.md` (the detailed
  Tasks 666–731 audit) was verified against the **desktop Linux/X11 build only**; none of it has
  been re-verified against a real Android device/emulator, or even a successful Android
  cross-compile.
- **NOT verified**: whether SDL3's own Android renderer driver selection (`SDL_RENDERER` renderer,
  Android side) actually uses OpenGL ES as this document's title assumes, versus some other
  driver SDL3 might pick on a given device — this project has never run far enough on Android to
  check `SDL_GetRendererInfo` there, unlike the desktop build's own confirmed `SDL_Renderer uses
  OpenGL` startup log line (Task 456).
- **No real Android graphics demo project exists.** The only Android Gradle/NDK project in this
  repository is `modules/devices/examples/demo_devices/android/` (`docs/devices-build.md` §4.1), which packages the
  **devices/sensors demo**, not a graphics demo — confirmed via `CMakeLists.txt`'s own comment
  explaining why a plain `add_executable()` demo target is skipped entirely on Android
  (`SDL_main`/`SDLActivity.java` `dlsym` linkage requirements mean a real Android app needs its own
  separate Gradle project producing a shared library, not a plain ELF executable — the same
  constraint would apply to any future Android graphics demo, not just the devices one).
- **No CI job builds for Android at all** — `.github/workflows/*.yml` has no Android-targeting job;
  the cross-compile is a manual, occasional, human-run verification per `docs/devices-build.md`.

## Summary

| Area | Status |
|---|---|
| Default renderer selection (`SDL_RENDERER`) | Confirmed correct at CMake configure time |
| `CNA`/`SHARP_RUNTIME` Android cross-compile | **Currently broken** — 2 unrelated `sharp-runtime` NDK-portability bugs (Task 920, not fixed here) |
| `SdlRenderer.cpp` Android buildability | Unknown — build never reaches it |
| Real device/emulator graphics execution | Never attempted in this project's history |
| Android graphics demo (Gradle/NDK project) | Does not exist (only the devices demo does) |
| CI coverage | None |

**Recommendation for whoever picks up Task 920** (fixing the 2 `sharp-runtime` bugs): once fixed,
re-run this exact cross-compile command and confirm `SdlRenderer.cpp` itself compiles cleanly
before drawing any further conclusions about Android graphics readiness — this document intentionally
stops at "currently blocked" rather than guessing at what would happen next.
