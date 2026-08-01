# GDI backend

`CNA_GRAPHICS_BACKEND=GDI` selects CNA's Windows-only, 2D-only GDI presentation backend.

It uses the CPU SpriteBatch/textures/render-target path shared with `SOFTWARE`, then presents the
main RGBA8 backbuffer into SDL's native Win32 `HWND` with GDI DIB APIs (`SetDIBitsToDevice` for a
1:1 blit and `StretchDIBits` when scaling is needed). This is a real GDI display path; it does not
create an SDL renderer, D3D device, OpenGL context or GPU swap chain.

## Scope

- Supported: Clear, RGBA textures, SpriteBatch (including source rectangles, transforms, rotation,
  flips and full XNA 2D `BlendState` factors/equations), 2D render targets, backbuffer/read-target
  readback, viewport/scissor, CPU `RasterizerState::WireFrame` SpriteBatch quads, and CNA
  presentation modes. A `RenderTarget2D` created with
  `mipMap=true` generates an RGBA8 mip chain when it is unbound using a clamped 2×2 box filter;
  its completed levels can then be sampled and read back. Mips are deliberately unavailable while
  that target is actively being rendered.
- `ColorMatrixEffect` is the one fixed CPU effect admitted by `SpriteBatch::Begin(..., &effect)`.
  It transforms the sampled-and-tinted RGBA source by a row-major 4×4 matrix plus RGBA offset,
  clamps each result to `[0,1]`, then uses the ordinary `BlendState`. `SetGrayscale()` selects
  Rec.709 luma and preserves alpha. Its cost is one 4×4 transform per covered sprite pixel
  (16 multiplies, 16 additions and 4 clamps), with no intermediate render target or allocation.
- Not supported: vertex/index buffers, 3D draw calls, depth buffers, MSAA, cube/3D textures,
  occlusion queries and arbitrary custom effects. `SupportsCapability()` returns `false` and
  direct 3D API calls throw rather than silently rendering through the inherited CPU 3D code.
- GDI provides a separate, real 8-bit CPU stencil plane for SpriteBatch 2D masks. `ClearStencil`
  and `ClearColorAndStencil` work; all `StencilOperation` values, compare/read masks, write masks
  and the clockwise stencil state work. `TwoSidedStencilMode` and
  `StencilDepthBufferFail` have no 2D meaning because GDI always disables depth and has no front/
  back-facing 3D primitives. `DepthStencilBuffer` deliberately remains unsupported: that
  capability means a complete depth+stencil attachment, not this stencil-only clipping feature.
- The shared Software core has an internal 3D depth buffer, but GDI forcibly disables only its
  depth state on every application. A `DepthStencilState` therefore cannot change SpriteBatch's
  ordinary submission order, while its independent stencil fields can still clip/mask a later 2D
  draw.
- A custom `ShaderEffect` is deliberately invalid on GDI (`CreateEffectBackend()` returns null).
  GDI does not accept shader source or uniforms and then ignore them. `ColorMatrixEffect` is the
  sole fixed non-shader exception; every other custom `SpriteBatch` effect is rejected.
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

Final-window scaling defaults to nearest-neighbour (`COLORONCOLOR`) so pixel-art stays crisp.
This is separate from `SamplerState`: it never changes the CPU rasterizer's point/linear texture
sampling. For non-pixel-art applications that prefer smoother resizing, opt in before launching:

```bash
# Windows cmd.exe
set CNA_GDI_PRESENT_FILTER=halftone
build-gdi\cna_demo_2d.exe

# Linux host running a MinGW build under Wine
CNA_GDI_PRESENT_FILTER=halftone wine build-gdi/cna_demo_2d.exe
```

Only the exact value `halftone` selects GDI's `HALFTONE` stretch mode; absent or any other value
keeps the nearest-neighbour default. The option has no effect when the backbuffer is already
presented 1:1.

For retained-mode UI-like workloads, `CNA_GDI_DIRTY_PRESENTATION=1` opts into a conservative
dirty-rectangle blit. It sends only the union of simple axis-aligned SpriteBatch destinations when
the output is 1:1. A clear, resize, presentation-mode change, scaling, rotation, non-identity
SpriteBatch transform or an invalidated Win32 client region automatically falls back to a complete
frame, so no stale pixels remain. It defaults off; use it only after measuring your actual UI:

```bash
CNA_GDI_DIRTY_PRESENTATION=1 wine build-gdi/cna_demo_2d.exe
```

GDI-011 was evaluated but deliberately does **not** use a persistent `DIBSection`: the CPU
rasterizer already owns the RGBA8 vector, so a DIBSection would add a full backbuffer copy before
every blit. The measured presentation time is tiny compared with CPU rasterization; the 1:1
`SetDIBitsToDevice` path avoids scaling without that additional copy.

GDI has no swap interval. An application that prefers best-effort compositor pacing may opt in to
one `DwmFlush()` after every GDI present:

```bash
# Windows cmd.exe
set CNA_GDI_DWM_FLUSH=1
build-gdi\cna_demo_2d.exe

# Linux host running a MinGW build under Wine
CNA_GDI_DWM_FLUSH=1 wine build-gdi/cna_demo_2d.exe
```

This is **not VSync** and can increase latency or block the CPU. It defaults off. The GDI backend
loads DWM dynamically; if composition is disabled or `dwmapi.dll`/`DwmFlush` is unavailable, it
falls back to normal non-blocking GDI presentation.

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
  --target cna_test_gdi_smoke cna_test_gdi_2d_regression cna_test_gdi_colormatrix_effect cna_bench_gdi_2d cna_demo_2d -j2
```

The backend is hard-gated to Windows targets. `cna_test_gdi_smoke` runs automatically as `GDI_Smoke`
on native Windows; for MinGW cross-builds, run the produced executable under a Wine setup with an
available display:

```bash
# Native Windows
build-gdi\\cna_test_gdi_smoke.exe
build-gdi\\cna_test_gdi_2d_regression.exe
build-gdi\\cna_test_gdi_colormatrix_effect.exe
build-gdi\\cna_bench_gdi_2d.exe --frames 4
build-gdi\\cna_demo_2d.exe

# Linux host, MinGW cross-build, with a real graphical Wine display
wine build-gdi/cna_test_gdi_smoke.exe
wine build-gdi/cna_test_gdi_2d_regression.exe
wine build-gdi/cna_test_gdi_colormatrix_effect.exe
wine build-gdi/cna_bench_gdi_2d.exe --frames 4
wine build-gdi/cna_demo_2d.exe
```

The smoke executable creates a hidden SDL `HWND`, clears and reads an RGBA pixel, calls GDI
`Present()`, and verifies that 3D stays unavailable.  It exits with status zero on success.  The
2D regression executable adds byte-exact coverage for texture upload, source rectangles, rotation,
both flips, Clamp/Wrap/Mirror sampling, render-target sampling and generated mips, resize and
presentation-coordinate transforms. It also covers Opaque, premultiplied `AlphaBlend`, straight-alpha
`NonPremultiplied`, Additive saturation, independent colour/alpha equations and a dynamic
`BlendFactor`, plus the explicit custom-`ShaderEffect` rejection. The 2D demo is the manual visual
check: animated sprites should display, resizing should remain correct, and the window should close
normally.

`cna_test_gdi_colormatrix_effect` is a high-level hidden-window integration test. It drives the
public `GraphicsDevice`, `Texture2D` and `SpriteBatch` APIs, then proves `ColorMatrixEffect`'s
Rec.709 grayscale, arbitrary channel matrix/offset and alpha preservation through readback. It
also proves the fixed effect did not broaden the contract by checking that `ShaderEffect` is still
rejected.

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
