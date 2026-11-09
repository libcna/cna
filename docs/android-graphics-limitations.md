# Android Graphics — Status and Limitations

## Current selection

Android defaults to `OPENGLES3` (EasyGL over OpenGL ES 3.0) when
`CNA_GRAPHICS_RENDERER` is unset. The rule is in
[RendererIdentityDefault.cmake](../cmake/RendererIdentityDefault.cmake).
SDL3 supplies the Android platform/window integration. Device input, audio and sensors have
separate qualification in [devices-android.md](devices-android.md) and the NDK build recipe in
[devices-build.md §4](devices-build.md#4-android-cross-compile).

## Evidence and its limits

NDK x86_64/android-24 cross-builds of CNA and its C API (CBIND-136) and application runs of external
samples on an x86_64 Android emulator (CBIND-137–139, covering graphics, portrait orientation and
Back-key delivery) are recorded at their revisions; they are not a qualification of the current
tree. Keep a platform build, a full CNA/C API build and an application run distinct. No physical
Android device qualification is recorded, `SDLGPU` is unqualified on Android, and no workflow under
`.github/workflows/` builds Android.

## Emulator base-vertex workaround

The Android emulator's GLES encoder returned vertex-attribute query state that did not follow
the bound VAO, breaking the pointer-rebase path. EasyGL recognizes the emulator through
`ro.boot.qemu` / `ro.kernel.qemu` and uses CPU base-vertex rebasing there.

`CNA_EASYGL_CPU_BASE_VERTEX=1` forces that path on any driver for diagnosis and regression tests.
This is an emulator-specific selection rule, not a measured defect on every physical GLES driver.
The implementation is `EasyGLRenderer::DetectAttribPointerQueries`.

## Floating-point render-target readback

EasyGL uses `RGBA/FLOAT` readback for GLES half/reduced-channel floating-point render targets,
then packs the declared layout. The upload format/type pair is insufficient to choose readback:
the recorded Android guest SwiftShader4.0.0.1 returned corrupt `HALF_FLOAT` data without a GL error,
while `RGBA/FLOAT` returned the expected values.

The renderer's transfer selection and packed subrectangle/cube layouts have a native regression,
`EasyGLFloatReadbackTest.EsFloatTransfersUseRgbaFloatAndPreservePackedSubrectangles`, which
observes the actual GL transfer pair. The diagnostic and recorded NDK/emulator evidence are in
[HALF_FLOAT_READBACK.md](../tools/graphics/HALF_FLOAT_READBACK.md). A standalone driver probe does
not establish a complete CNA Android application run or physical-device qualification.

## Open behavior boundary

EasyGL Texture3D resources participate in the same recovery registry as cube textures. Their
native names are discarded on loss, then every mip is restored from exact declared-format CPU
data. The OpenGLES3/OpenGL33 regressions recreate real GL contexts on Linux and sample the same
volume objects afterwards; see [volume recovery](texture3d-texturecube-support.md#easygl-context-recovery).
No Android volume context-loss regression is recorded, so those native checks do not establish
Android activity lifecycle or physical-device recovery qualification.
