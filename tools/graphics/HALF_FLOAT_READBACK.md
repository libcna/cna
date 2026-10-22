# GLES float render-target readback diagnostic

The museum's Android guest returned corrupt `RGBA/HALF_FLOAT` data from an
otherwise complete RGBA16F framebuffer, without reporting a GL error. Its
implementation-selected read pair was `RGBA/FLOAT`, which returned exact values.
Desktop Mesa accepted both. GLES does not guarantee the upload format/type pair
for readback: see the floating-point surface error rule in the
[Khronos ES3 reference](https://registry.khronos.org/OpenGL-Refpages/es3.0/html/glReadPixels.xhtml).

`half_float_readback_probe.cpp` isolates this driver behavior from CNA and SDL.
It clears {-2,0.5,2,1}, checks exact representations and destination sentinels,
and prints both transfer results. Default mode fails if either raw transfer is
incorrect; this is diagnostic evidence, not a portable GLES conformance oracle.
`--portable` instead requires exact RGBA/FLOAT values and their conversion through
CNA's existing `HalfTypeHelper`; the raw HALF_FLOAT result is still printed and
is never relabelled as a pass.

From CNA, use an existing build directory and shared cache:

```sh
CCACHE_DIR=/rv/cnaccache CCACHE_BASEDIR=/rv ccache c++ -std=c++20 \
  -Wall -Wextra -Werror -Imodules/graphics/include \
  tools/graphics/half_float_readback_probe.cpp -lEGL -lGLESv2 \
  -o cmake-build-multi/half-float-readback-probe
tools/platform/run_gpu_tests_private.sh --exec \
  cmake-build-multi/half-float-readback-probe --portable
```

For Android, use the NDK's `x86_64-linux-android24-clang++`, add
`-static-libstdc++`, link `-lGLESv3` instead of `-lGLESv2`, and place its output in
the same build directory with an `-android` suffix. Execute only on a dedicated
headless, read-only emulator, never on a user's physical device or shared active
emulator. The campaign used Medium_Phone with guest GLES, two cores and 3072MiB,
under a 6GiB/no-swap cgroup. Stop that specific emulator after the test.

This diagnostic is **not** an Android CNA application test. The actual renderer's
transfer selection, packed subrectangles, odd widths, bounds and cube/2D layouts
are protected by `EasyGLFloatReadbackTest`; HDR drawing and format agreement have
separate native integration suites. Museum application revalidation remains
separate evidence.
