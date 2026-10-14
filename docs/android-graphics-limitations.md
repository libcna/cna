# Android Graphics — Status and Limitations

`OPENGLES3` (EasyGL over OpenGL ES 3.0) is the default renderer on Android and the only validated
one: when no renderer is named, `cmake/RendererIdentityDefault.cmake` picks `OPENGLES3` for Android
as for Linux. Audio, input and sensors on Android are covered in `docs/devices-android.md` and
`docs/devices-build.md` (§4 has the NDK cross-compile recipe).

## Verified

- NDK cross-builds of CNA and its C API (NDK 29, `x86_64`, `android-24`,
  `cmake-build-android-x86_64`; `plans/plan_binding.md` CBIND-136).
- Running on an x86_64 emulator: cna-cs samples (AimingSample, SimpleAnimation, ColorReplacement,
  SnowShovel) draw as on the desktop, portrait games in portrait, and Android's Back press reaches
  them (CBIND-137..139).
- The emulator's GLES encoder answers vertex-attribute queries from state that does not follow the
  bound VAO, so EasyGL rebases base-vertex draws on the CPU there (CBIND-139);
  `CNA_EASYGL_CPU_BASE_VERTEX=1` forces that path on any driver.

## Not verified

- Physical devices: only the emulator's GLES (`swiftshader_indirect`/`swangle_indirect`) has run
  CNA.
- `SDL_GPU`: unqualified on Android; no Android build or run of it is recorded.
- CI: no job builds for Android.

`SDL_RENDERER`, the former Android default, was retired on 2026-10-13
([`removed-renderers.md`](removed-renderers.md)).
