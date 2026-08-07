# Skia sanitizer and recreation validation

SKIA-110 validates the accepted CPU-raster backend with AddressSanitizer,
UndefinedBehaviorSanitizer, and LeakSanitizer. It does not claim an accelerated comparison: the
pinned Skia archives and CNA backend deliberately disable Ganesh, Graphite, GL, Vulkan, and Dawn,
and `ctest -N -L Accelerated` currently reports zero tests.

## Instrumentation boundary

A fresh sanitizer configuration uses CNA's common switch:

```sh
cmake -S . -B cmake-build-skia-sanitize \
  -DCNA_GRAPHICS_BACKEND=SKIA \
  -DCNA_SKIA_ROOT=/path/to/skia \
  -DCNA_SKIA_BUILD_DIR=/path/to/skia-out/raster \
  -DCNA_BUILD_TESTS=ON \
  -DCNA_USE_CCACHE=OFF \
  -DCNA_SANITIZE=address,undefined \
  -DCNA_TEST_DISPLAY=:0
cmake --build cmake-build-skia-sanitize --parallel 8
```

The pinned upstream Skia archives are built without RTTI. GCC and Clang include `vptr` in the
broad `undefined` group; instrumenting calls across this boundary requires absent symbols such as
`typeinfo for SkCanvas` and fails at link time. The build therefore applies
`-fno-sanitize=vptr` only to the Skia adapter and Skia fixture executables. Address checks and all
other UBSan checks remain enabled there, while CNA, sharp-runtime, and the rest of each fixture
retain the full requested sanitizer set. This is a toolchain/third-party ABI constraint, not a
runtime suppression.

## Paired recreation gate

`Skia_ResourceBudget` now performs 64 combined cycles. Every cycle:

1. creates and binds a PreserveContents `RenderTarget2D`;
2. clears it and creates its immutable sampling snapshot;
3. reconstructs SDL's renderer and streaming presentation texture while both objects are live;
4. clears the backbuffer, resamples the retained target, presents, and checks the exact pixel;
5. proves every resource counter is unchanged across recovery and returns to its baseline after
   target destruction.

The complete sequence also requires exactly 64 `DeviceResetting`/`DeviceReset` pairs and no
fabricated `DeviceLost` event. This couples target/snapshot reuse to presenter reconstruction;
the earlier target loop and context-recovery tests exercised those dimensions separately.

## Results

- Debug: the full Skia suite passes 132/132 in 21.53 seconds with `--parallel 8` (16 Raster,
  113 Display, three Audit).
- ASan+UBSan: the same full suite passes 132/132 in 26.85 seconds with
  `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1` and
  `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`.
- Release: the expanded `Skia_ResourceBudget` gate passes in 3.16 seconds.
- LSan: all 16 display-free Raster tests pass with `detect_leaks=1`; the paired 64-cycle gate also
  passes clean when launched with `SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software`, which
  isolates CNA/Skia ownership from the host X11 stack.

With the default Xvfb/X11 presentation path, both the 64-cycle gate and the one-presenter
`Skia_Presentation_Edge` control report the exact same process-exit residual: 100,956 bytes in
449 allocations, with every reported allocation rooted in `libGLX_mesa.so.0`. The residual does
not grow with 64 renderer reconstructions and disappears from the dummy/software isolation run.
It is therefore recorded as the current host Mesa GLX baseline rather than hidden by a broad LSan
suppression. Older sessions on a different display-stack build measured a smaller 2,864-byte X11
baseline; the symbolized current measurement supersedes that byte count for this host.

All persistent Debug, Release, sanitizer, and EasyGL CMake caches were restored to
`CNA_TEST_DISPLAY=:0` after the Xvfb runs.
