# GDI backend

`CNA_GRAPHICS_BACKEND=GDI` selects CNA's Windows-only, 2D-only GDI presentation backend.

It uses the CPU SpriteBatch/textures/render-target path shared with `SOFTWARE`, then presents the
main RGBA8 backbuffer into SDL's native Win32 `HWND` with GDI DIB APIs (`SetDIBitsToDevice` for a
1:1 blit and `StretchDIBits` when scaling is needed). This is a real GDI display path; it does not
create an SDL renderer, D3D device, OpenGL context or GPU swap chain.

The focused automated correctness suite passes in the MinGW/Wine configuration documented below,
but the native visible Windows lifecycle/DPI gate in `plan_gdi.md` is still open. Treat GDI as a
compatibility backend under validation, not yet as a release baseline.

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
- Not supported: vertex/index buffers, 3D draw calls, depth buffers, cube/3D textures,
  occlusion queries and arbitrary custom effects. `SupportsCapability()` returns `false` and
  direct 3D API calls throw rather than silently rendering through the inherited CPU 3D code.
- Optional 4x CPU MSAA is available for the GDI backbuffer only: request exactly
  `PresentationParameters.MultiSampleCount = 4` (or call `ApplyMultiSampleCount(4)`). It uses
  four 2x2-grid coverage samples, blends each covered sample independently, and resolves before
  GDI `Present()` and CPU readback. Requests other than 4 return 0; `RenderTarget2D` remains
  single-sampled and reports 0. `SupportsCapability(MultiSampleAntiAliasing)` is true because the
  backbuffer path is real, but applications that require multisampled off-screen targets should
  use another backend.
- The feature is intentionally opt-in because it adds 16 bytes/pixel of sample colour storage in
  addition to the ordinary resolved RGBA8 image. At 800×600 this is 7.32 MiB extra (11.44 MiB for
  the complete colour/depth/stencil framebuffer), and at 1280×720 it is 14.06 MiB extra (21.97
  MiB complete). `cna_bench_gdi_2d --frames 1` measures 0x and 4x side by side; the current Wine
  reference run changed 800×600/12 rotating sprites from 7.64 ms to 18.04 ms per frame and
  1280×720/20 from 13.08 ms to 31.04 ms. Native Windows hardware/compositor measurements remain
  the decision point for a shipping application.
- `TextureFilter::Anisotropic` intentionally maps to the same CPU Linear+mip-linear sampler as
  `TextureFilter::Linear`; it does not advertise anisotropic capability. A true anisotropic CPU
  filter would require several directionally distributed texture footprints per covered sample,
  multiplying the dominant raster workload while offering little value for this compatibility-2D
  backend. The GDI regression pixel-tests that mapping. The benchmark accepts `--linear` and
  `--anisotropic` to compare the paths; they use the same sampler implementation (the small Wine
  run-to-run timing variance is not a feature difference).
- GDI provides a separate, real 8-bit CPU stencil plane for SpriteBatch 2D masks. `ClearStencil`
  and `ClearColorAndStencil` work through both the backend and public `GraphicsDevice::Clear`
  overloads; all `StencilOperation` values, compare/read masks, write masks and the clockwise
  stencil state work. The plane is always allocated for the backbuffer and every GDI
  `RenderTarget2D`, including a target whose public `DepthFormat` is `None`.
  `SupportsCapability(StencilBuffer)` is therefore true. `TwoSidedStencilMode` and
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
  single-sampled unless the explicit 4x CPU-MSAA option is active.

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
dirty-rectangle blit. At 1:1 it sends the union of the actual clipped SpriteBatch raster bounds,
reported after origin, rotation, batch transform, viewport and scissor are applied. A clear,
backbuffer resize, presentation-mode change, scaled output, or watched native-client invalidation
uses a complete frame. Expose, restore, resize, focus, display-scale and fullscreen lifecycle events
advance a per-window invalidation generation; repaint no longer depends only on Win32's possibly
already-validated update region. It defaults off; use it only after measuring your actual UI:

```bash
CNA_GDI_DIRTY_PRESENTATION=1 wine build-gdi/cna_demo_2d.exe
```

Presentation is committed transactionally: scoped DC ownership and correctness-relevant GDI calls
are checked, and CPU damage plus native-client invalidation remain pending after a failed or
zero-line DIB transfer. Geometry/path selection lives in a pure planner shared with a memory-DC
pixel oracle; this is what verifies channel order, top-down rows, scaling, bars/cropping and exact
non-zero-Y dirty bands independently of CPU backbuffer readback.

The current path deliberately does **not** use a persistent `DIBSection`: the CPU rasterizer owns
the authoritative RGBA8 vector and submits it directly. Whether a mapped DIBSection should instead
become authoritative storage remains GDI-066 and requires native visible measurements; hidden-Wine
timings are not a release decision.

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
  --target cna_test_gdi_smoke cna_test_gdi_2d_regression \
           cna_test_gdi_colormatrix_effect cna_test_gdi_public_stencil \
           cna_test_gdi_public_api \
           cna_test_gdi_dirty_damage cna_test_gdi_repaint_invalidation \
           cna_test_gdi_presentation_oracle \
           cna_test_gdi_presentation_configuration \
           cna_bench_gdi_2d cna_demo_2d -j2
```

The backend is hard-gated to Windows targets. A native Windows build registers eleven `GDI` CTest
cases, including separate default, dirty and halftone configurations; run them with
`ctest -L GDI --output-on-failure`. Cross-built PE files are intentionally not registered as Linux
CTest commands, so run the produced executables under a Wine setup with an available display:

```bash
# Native Windows
build-gdi\cna_test_gdi_smoke.exe
build-gdi\cna_test_gdi_2d_regression.exe
build-gdi\cna_test_gdi_colormatrix_effect.exe
build-gdi\cna_test_gdi_public_stencil.exe
build-gdi\cna_test_gdi_public_api.exe
build-gdi\cna_test_gdi_dirty_damage.exe
build-gdi\cna_test_gdi_repaint_invalidation.exe
build-gdi\cna_test_gdi_presentation_oracle.exe
build-gdi\cna_bench_gdi_2d.exe --frames 4
build-gdi\cna_demo_2d.exe

# Linux host, MinGW cross-build, with a real graphical Wine display
wine build-gdi/cna_test_gdi_smoke.exe
wine build-gdi/cna_test_gdi_2d_regression.exe
wine build-gdi/cna_test_gdi_colormatrix_effect.exe
wine build-gdi/cna_test_gdi_public_stencil.exe
wine build-gdi/cna_test_gdi_public_api.exe
wine build-gdi/cna_test_gdi_dirty_damage.exe
wine build-gdi/cna_test_gdi_repaint_invalidation.exe
wine build-gdi/cna_test_gdi_presentation_oracle.exe
wine build-gdi/cna_bench_gdi_2d.exe --frames 4
wine build-gdi/cna_demo_2d.exe
```

The three configuration cases set their process environment explicitly and keep compositor pacing
out of deterministic tests. Equivalent manual Wine invocations are:

```bash
CNA_GDI_DIRTY_PRESENTATION=0 CNA_GDI_PRESENT_FILTER=nearest CNA_GDI_DWM_FLUSH=0 \
  wine build-gdi/cna_test_gdi_presentation_configuration.exe default
CNA_GDI_DIRTY_PRESENTATION=1 CNA_GDI_PRESENT_FILTER=nearest CNA_GDI_DWM_FLUSH=0 \
  wine build-gdi/cna_test_gdi_presentation_configuration.exe dirty
CNA_GDI_DIRTY_PRESENTATION=1 CNA_GDI_PRESENT_FILTER=halftone CNA_GDI_DWM_FLUSH=0 \
  wine build-gdi/cna_test_gdi_presentation_configuration.exe halftone
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

`cna_test_gdi_public_stencil` closes the former public-API coverage gap: it creates masks through
`GraphicsDevice`, `SpriteBatch` and `DepthStencilState`, then proves stencil-only and single-colour
clears on both the backbuffer and a depthless `RenderTarget2D`. It also checks the separate
`StencilBuffer=true` and combined `DepthStencilBuffer=false` capability answers.

`cna_test_gdi_dirty_damage` verifies the raster-derived rectangle after origin, viewport, scissor,
transform and rotation, plus negative, off-screen and overflow-sized geometry. The repaint test
verifies window-specific lifecycle invalidation, a real SDL-validated Win32 `WM_PAINT`, a real
minimize/restore round-trip, no-draw retained repaint, zero-line DIB failure diagnostics, damage
retention and successful retry. `cna_test_gdi_presentation_oracle` uses a real memory DC/DIBSection
to verify RGBA channel order, top-down rows, both filters and DIB paths, presentation geometry,
bars/cropping, clipping and an exact non-zero-Y dirty band. The configuration executable then proves
that the registered environment cases select NativeFull, None and Stretch with the requested filter
through backend telemetry.

`cna_bench_gdi_2d` is a short, manual benchmark (four measured frames by default) that reports
CPU raster time and GDI `Present()` time separately for 800×600 and 1280×720 scenes. Always run
it from a **Release** build: an empty CMake build type omits `-O3` and is not a valid performance
measurement. On an AMD Ryzen 7 PRO 7840U, MinGW-w64 14 and hidden-window Wine, the Release
baseline was 9.169 ms raster + 0.026 ms present for 12 rotating alpha sprites at 800×600; the
same unoptimised build took 22.248 ms raster. Wine's hidden presentation number is not a
substitute for a native visible-Windows measurement and cannot establish compositor cost or the
shipping bottleneck; GDI-062 owns that decision.

On MinGW, the build stages SDL plus the needed GCC/C++ and threading runtime DLLs beside the
executables, so Wine does not rely on a compiler-specific `PATH`.  Keep both the configure-time
vendored-dependency limit and every top-level build at two jobs or fewer as shown above.
