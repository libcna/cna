# GDI backend

`CNA_GRAPHICS_BACKEND=GDI` selects CNA's Windows-only, 2D-only GDI presentation backend.

It uses the CPU SpriteBatch/textures/render-target path shared with `SOFTWARE`, then presents the
main RGBA8 backbuffer into SDL's native Win32 `HWND` with GDI `StretchDIBits`.  This is a real GDI
display path; it does not create an SDL renderer, D3D device, OpenGL context or GPU swap chain.

## Scope

- Supported: Clear, RGBA textures, SpriteBatch (including source rectangles, transforms, rotation,
  flips and full XNA 2D `BlendState` factors/equations), 2D render targets, backbuffer/read-target
  readback, viewport/scissor, and CNA presentation modes.
- Not supported: vertex/index buffers, 3D draw calls, depth/stencil, MSAA, cube/3D textures,
  occlusion queries and custom effects. `SupportsCapability()` returns `false` and direct 3D API
  calls throw rather than silently rendering through the inherited CPU 3D code.
- `PresentInterval` is ignored because GDI has no swap-chain interval control. The backbuffer is
  single-sampled.

## Performance

Every frame is rasterized on the CPU and copied to the window by GDI. It is appropriate for
compatibility applications, UI, retro games and modest-resolution 2D workloads. Large render
targets, extensive alpha blending/rotation, high resolutions or a hard 60/120 FPS requirement are
better served by `SDL_RENDERER`, `SDL_GPU`, or a Direct3D backend.

The bundled `cna_demo_2d` automatically uses a GDI compatibility profile (12–20 animated
sprites at 30 FPS).  Its normal profile is a 50–100-sprite, 60-FPS GPU stress scene, which is not
a useful default workload for a CPU rasterizer.  This keeps the manual display check responsive;
it is not a replacement for the benchmark work described in `plan_gdi.md`.

## Build

```bash
cmake -S . -B build-gdi \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64.cmake \
  -DCNA_GRAPHICS_BACKEND=GDI \
  -DCNA_BUILD_TESTS=ON \
  -DCNA_BUILD_EXAMPLES=ON \
  -DCNA_MAX_VENDORED_BUILD_JOBS=2
CMAKE_BUILD_PARALLEL_LEVEL=2 cmake --build build-gdi \
  --target cna_test_gdi_smoke cna_test_gdi_2d_regression cna_bench_gdi_2d cna_demo_2d -j2
```

The backend is hard-gated to Windows targets. `cna_test_gdi_smoke` runs automatically as `GDI_Smoke`
on native Windows; for MinGW cross-builds, run the produced executable under a Wine setup with an
available display:

```bash
# Native Windows
build-gdi\\cna_test_gdi_smoke.exe
build-gdi\\cna_test_gdi_2d_regression.exe
build-gdi\\cna_bench_gdi_2d.exe --frames 4
build-gdi\\cna_demo_2d.exe

# Linux host, MinGW cross-build, with a real graphical Wine display
wine build-gdi/cna_test_gdi_smoke.exe
wine build-gdi/cna_test_gdi_2d_regression.exe
wine build-gdi/cna_bench_gdi_2d.exe --frames 4
wine build-gdi/cna_demo_2d.exe
```

The smoke executable creates a hidden SDL `HWND`, clears and reads an RGBA pixel, calls GDI
`Present()`, and verifies that 3D stays unavailable.  It exits with status zero on success.  The
2D regression executable adds byte-exact coverage for texture upload, source rectangles, rotation,
both flips, Clamp/Wrap/Mirror sampling, render-target sampling, resize and presentation-coordinate
transforms. It also covers Opaque, premultiplied `AlphaBlend`, straight-alpha `NonPremultiplied`,
Additive saturation, independent colour/alpha equations and a dynamic `BlendFactor`. The 2D demo
is the manual visual check: animated sprites should display, resizing should remain correct, and
the window should close normally.

`cna_bench_gdi_2d` is a short, manual benchmark (four measured frames by default) that reports
CPU raster time and GDI `Present()` time separately for 800×600 and 1280×720 scenes. Always run
it from a **Release** build: an empty CMake build type omits `-O3` and is not a valid performance
measurement. On an AMD Ryzen 7 PRO 7840U, MinGW-w64 14 and hidden-window Wine, the Release
baseline was 9.169 ms raster + 0.026 ms present for 12 rotating alpha sprites at 800×600; the
same unoptimised build took 22.248 ms raster. Wine's hidden presentation number is not a
substitute for a native visible-Windows measurement, but it correctly identifies the CPU
rasterizer rather than the GDI blit as the dominant cost.

On MinGW, the build stages SDL plus the needed GCC/C++ and threading runtime DLLs beside the
executables, so Wine does not rely on a compiler-specific `PATH`.  Keep both the configure-time
vendored-dependency limit and every top-level build at two jobs or fewer as shown above.
