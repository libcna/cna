# GDI renderer

> **Adaptation status (2026-08-08):** the meaningful 34-commit `feature/gdi` history has been
> replayed onto the current integration architecture. The current x64 MinGW Release build produced
> all seventeen focused correctness executables, and all nineteen registered cases passed serially
> through Wine 10/Xvfb. Focused native Software sanitizer and cross-renderer controls also passed as
> detailed below. The manual native-MSVC workflow and physical-Windows lifecycle/DPI and handle
> observation remain pending.

`CNA_GRAPHICS_RENDERER=GDI` selects CNA's Windows-only, 2D-only GDI presentation renderer.

It privately composes the CPU SpriteBatch/textures/render-target services shared with `SOFTWARE`,
then presents the main RGBA8 backbuffer into SDL's native Win32 `HWND` with GDI DIB APIs
(`SetDIBitsToDevice` for a 1:1 blit and `StretchDIBits` when scaling is needed). This is a real GDI
display path; it does not create an SDL renderer, D3D device, OpenGL context or GPU swap chain.

Both the historical feature branch and current adapted branch passed their focused MinGW/Wine
configurations. The native visible-Windows lifecycle/DPI and MSVC gates in `plans/plan_gdi.md` remain
open. Treat GDI as a compatibility renderer under validation, not yet as a release baseline.

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
- Not supported: vertex/index buffers (including dynamic variants), 3D draw/state calls, depth
  buffers, cube/3D textures, cube render targets, occlusion queries and arbitrary custom effects.
  GDI rejects each at its first resource-construction or renderer-invocation boundary with
  `System::NotSupportedException`; it never returns a null-backed object that appears usable.
  `Texture3D` is rejected by its public capability guard before renderer creation, while the other
  resources are rejected by the GDI factory entry. `SupportsCapability(ThreeD)`, `Texture3D`,
  `OcclusionQuery`, and `CustomEffects` remain false.
- Optional 4x CPU MSAA is available for the GDI backbuffer only: request exactly
  `PresentationParameters.MultiSampleCount = 4` (or call `ApplyMultiSampleCount(4)`). It uses
  four 2x2-grid colour samples for filled SpriteBatch triangles, blends each geometrically covered
  sample independently, and resolves before GDI `Present()` and CPU readback.
  `BlendState.MultiSampleMask` bits 0 through 3 enable those four samples; higher bits are ignored.
  Wireframe SpriteBatch quads deliberately remain crisp one-pixel DDA lines: every enabled sample
  in a visited pixel is written, so this mode does not provide subpixel line antialiasing. Stencil
  remains one 8-bit value per pixel, not per sample. One stencil comparison/operation occurs for a
  covered triangle fragment after sample-mask/coverage rejection and gates all active colour
  samples; a fragment with no active samples cannot modify stencil. Requests other than 4 return 0;
  `RenderTarget2D` remains single-sampled and reports 0. There is no per-sample depth/stencil
  attachment. `SupportsCapability(MultiSampleAntiAliasing)` is true for this deliberately narrow,
  real backbuffer path; applications requiring multisampled off-screen targets, antialiased wire
  edges, or per-sample depth/stencil should use another renderer.
- The applied GDI backbuffer is always `SurfaceFormat::Color` with
  `PresentationParameters.DepthStencilFormat=None`; unsupported format/depth requests are
  normalized before the device exposes its active parameters. Direct construction, store-only
  parameter updates, and `Reset()` all report the renderer's actual 0x/4x sample count. This
  `DepthFormat::None` answer does not hide the independent stencil plane, which is queried through
  `GraphicsCapability::StencilBuffer` as described below.
- The feature is intentionally opt-in because it adds 16 bytes/pixel of sample colour storage in
  addition to the ordinary resolved RGBA8 image and standalone stencil plane. GDI allocates no
  unused float-depth plane: its baseline is 5 bytes/pixel and 4x MSAA is 21 bytes/pixel total. At
  800×600 the baseline is 2.29 MiB and MSAA adds 7.32 MiB (9.61 MiB total); at 1280×720 the baseline
  is 4.39 MiB and MSAA adds 14.06 MiB (18.46 MiB total). `cna_bench_gdi_2d --frames 1` measures 0x
  and 4x side by side; a historical feature-branch Wine reference run changed 800×600/12 rotating
  sprites from 7.64 ms to 18.04 ms per frame and 1280×720/20 from 13.08 ms to 31.04 ms. Native
  Windows hardware/compositor measurements remain the decision point for a shipping application.
- `TextureFilter::Anisotropic` intentionally maps to the same CPU Linear+mip-linear sampler as
  `TextureFilter::Linear`; it does not advertise anisotropic capability. A true anisotropic CPU
  filter would require several directionally distributed texture footprints per covered sample,
  multiplying the dominant raster workload while offering little value for this compatibility-2D
  renderer. The GDI regression pixel-tests that mapping. The benchmark accepts `--linear` and
  `--anisotropic` to compare the paths; they use the same sampler implementation (the small Wine
  run-to-run timing variance is not a feature difference).
- GDI provides a separate, real 8-bit CPU stencil plane for SpriteBatch 2D masks. `ClearStencil`
  and `ClearColorAndStencil` work through both the renderer and public `GraphicsDevice::Clear`
  overloads; all `StencilOperation` values, compare/read masks, write masks and the clockwise
  stencil state work. The plane is always allocated for the backbuffer and every GDI
  `RenderTarget2D`, including a target whose public `DepthFormat` is `None`.
  `SupportsCapability(StencilBuffer)` is therefore true. `TwoSidedStencilMode` and
  `StencilDepthBufferFail` have no 2D meaning because GDI always disables depth and has no front/
  back-facing 3D primitives. `DepthStencilBuffer` deliberately remains unsupported: that
  capability means a complete depth+stencil attachment, not this stencil-only clipping feature.
- Every CPU framebuffer is planned before allocation. Width and height must each be in
  `[1, 16384]`, and resolved colour, selected depth/stencil/sample planes, and a requested generated
  mip chain share a 512 MiB per-resource pixel-storage budget. All `size_t` products/additions and
  the Win32 `DWORD` DIB byte boundary are checked first. Invalid or over-budget requests throw
  `System::ArgumentOutOfRangeException`; an allocator failure throws `System::OutOfMemoryException`.
  A failed resize or 4x-MSAA change retains the previous framebuffer and pixels.
- A GDI `RenderTarget2D` accepts only `SurfaceFormat::Color` (other formats fail construction),
  reports `DepthStencilFormat=None` and `MultiSampleCount=0`, and still owns the independent stencil
  plane. `PreserveContents` and `PlatformContents` both retain color/stencil on rebind;
  `DiscardContents` deterministically replaces them with black/zero. Mipmap and usage properties
  therefore describe the actual CPU storage rather than the original unsupported request.
- `GdiRenderer` derives directly from `IGraphicsRenderer`. Its private `GdiSoftware2DCore`
  forwards only reviewed framebuffer, texture, SpriteBatch, 2D-target, and 2D-state services; no
  complete Software renderer pointer escapes. GDI constructs every surface without a depth plane
  and forcibly disables depth state on every application. A `DepthStencilState` therefore cannot
  change SpriteBatch's ordinary submission order, while its independent stencil fields can still
  clip/mask a later 2D draw. New Software 3D methods cannot enter GDI through inheritance.
- The GDI renderer archive uses an explicit eight-file CPU-2D dependency list
  (`SoftwareFramebufferAllocation.cpp`, `SoftwareTextureAllocation.cpp`,
  `SoftwareFramebuffer.cpp`, `SoftwareTexture2D.cpp`, `SoftwareRenderTarget2D.cpp`,
  `SoftwareRenderer2DState.cpp`, `SoftwareSpriteBatch.cpp`, and
  `SoftwareRenderer2D.cpp`); it does not glob the Software directory and has no
  intermediate `software_core` archive. This prevents future Software files from silently
  entering the Windows build. `cmake/BackendLibraries.cmake`'s `CNA_GDI_SOFTWARE_SOURCES` is the
  source of truth for this count and prints it (plus the three GDI-owned units, eleven total) at
  configure time (GDI-078), rather than relying on this prose being kept in sync by hand. The
  resource/state/SpriteBatch sources are independently compiled by GDI and SOFTWARE; the latter
  wrapper compiles the shared 2D triangle-raster bridge with `CNA_SOFTWARE_2D_ONLY`. Software
  cube/resources, programmable effects, and general-3D draw bodies are not compiled into GDI. The
  full SOFTWARE build retains them through `SoftwareRenderer.cpp`. GDI-074 still tracks
  extracting shared raster helpers from that remaining source-text monolith.
- GDI-076 gave CPU `Texture2D` allocation the same checked layout/byte-budget discipline GDI-067
  gave framebuffers: `SoftwareTextureAllocation.hpp`/`.cpp` plans positive dimensions, the shared
  16,384-axis ceiling, every declared mip level's bytes, and a 512 MiB per-resource budget before
  `SoftwareTextureRenderer` allocates; a caller-supplied pixel buffer smaller than its own level is
  rejected (`System::ArgumentException`), a larger one is truncated rather than retained verbatim,
  and `std::bad_alloc`/`std::length_error` become `System::OutOfMemoryException`. This is enforced
  at the GDI/Software CPU-texture boundary only -- the shared `Texture2D.cpp` public constructors
  (used identically by every CNA renderer) still allocate their own first `width*height*4` vector
  before that boundary is reached, so an over-budget request still pays for one wasted transient
  allocation before being rejected; closing that remains open, cross-renderer scope.
- REMED-GFX-229 closes the supported CPU upload-pitch gap: a positive `Texture2D` stride must be
  at least `width * 4`, while zero or a negative value retains the established tight-row default.
  Padded rows are copied without their padding, and a rejected short stride leaves the previous
  texture bytes unchanged. The texture-allocation executable covers an odd three-pixel width with
  asymmetric RGBA channels so row overlap, padding ingestion, and channel swaps are observable.
- REMED-GFX-230 applies the same contract to `RenderTarget2D::UpdatePixels`, which formerly ignored
  its stride and treated every source as tight. It now copies only the RGBA bytes from each valid
  padded row and rejects a positive pitch below `width * 4` before changing color, MSAA, or mip
  state. The 2D regression covers exact readback and transactional rejection on a 3×2 target.
- REMED-GFX-231 restores the XNA `SourceAlphaSaturation` source factor for the private CPU lane:
  RGB uses `min(sourceAlpha, 1-destinationAlpha)` and alpha uses one. The 2D regression uses
  asymmetric RGB plus distinct nontrivial source/destination alpha values so substituting inverse
  source alpha cannot pass. Current GDI and Software blend controls pass.
- REMED-GFX-232 keeps the integration branch's depth-only DIRECTX3 renderer truthful after introducing
  the standalone stencil hook: `SupportsStencilBuffer()` now returns false, matching its
  `GraphicsCapability::StencilBuffer` answer and documented lack of a stencil plane. The focused
  DIRECTX3 capability executable compares the two answers directly and passes 1/1 through Wine/Xvfb
  after the x64 MinGW build, with the DirectDraw-engagement wrapper active.
- REMED-GFX-233 restores shared Software's legacy persistent-buffer compatibility. The CNAEXT
  `VertexBuffer(device, count)` constructor deliberately carries an empty, zero-stride public
  declaration while typed `SetData` uploads real packed records. The immutable stream snapshot
  introduced before this adaptation copied that zero stride and repeatedly fetched record zero,
  leaving classic non-indexed/indexed draws empty. That exact one-buffer shape now uses the
  existing named-`vb` renderer-stride fallback; all declared and multistream bindings retain the
  current authoritative stream semantics. The shared Additive contract asserts the legacy
  declaration precondition and exact pixels for both persistent routes. Current Software effects
  7/7, Additive 29/29, and scissor 44/44 controls pass. The faulty mechanism pre-existed at
  integration base `677f4c59` and was exposed and closed by this lane.
- REMED-BUILD-018 makes the shared capability test self-contained by directly including
  `IGraphicsRenderer.hpp` before invoking `GetRenderer().SupportsStencilBuffer()`; the current native
  sanitizer harness compiles and runs, closing the incomplete-type build failure.
- A custom `ShaderEffect` throws `System::NotSupportedException` during construction on GDI. GDI
  does not create an invalid placeholder, accept shader source or uniforms, and then ignore them.
  `ColorMatrixEffect` is the sole fixed non-shader exception; every other custom `SpriteBatch`
  effect is rejected.
- `PresentInterval` is ignored because GDI has no swap-chain interval control. The backbuffer is
  single-sampled unless the explicit 4x CPU-MSAA option is active.

## Performance

Every frame is rasterized on the CPU and copied to the window by GDI. It is appropriate for
compatibility applications, UI, retro games and modest-resolution 2D workloads. Large render
targets, extensive alpha blending/rotation, high resolutions or a hard 60/120 FPS requirement are
better served by `SDL_RENDERER`, `SDL_GPU`, or a Direct3D renderer.

The bundled `cna_demo_2d` automatically uses a GDI compatibility profile (12–20 animated
sprites at 30 FPS).  Its normal profile is a 50–100-sprite, 60-FPS GPU stress scene, which is not
a useful default workload for a CPU rasterizer.  This keeps the manual display check responsive;
it is not a replacement for the benchmark work described in `plans/plan_gdi.md`.

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

The accepted values are exactly `nearest` and `halftone`; absence selects `nearest`. An invalid
value retains that safe default and contributes to one configuration warning. The option has no
effect on pixels when the backbuffer is already presented 1:1.

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

The accepted switch values are exactly `0` and `1`; absence selects `0`. Invalid values retain the
off default and are included in the same configuration warning.

Presentation is committed transactionally: scoped DC ownership and correctness-relevant GDI calls
are checked, and CPU damage plus native-client invalidation remain pending after a failed or
zero-line DIB transfer. Geometry/path selection lives in a pure planner shared with a memory-DC
pixel oracle; this is what verifies channel order, top-down rows, scaling, bars/cropping and exact
non-zero-Y dirty bands independently of CPU backbuffer readback.

`SDL_GetWindowSizeInPixels()` is the single source of truth for GDI backbuffer sizing and final
client geometry. SDL mouse events and `SDL_WarpMouseInWindow`, however, use SDL window-coordinate
space, so the GDI coordinate transforms explicitly scale between `SDL_GetWindowSize()` and the
drawable-pixel size before applying presentation bars/cropping. This keeps input and display
aligned when pixel density is not 1. Caller-provided SDL windows are published to `Mouse` and
`TextInputEXT` for the lifetime of their `GraphicsDevice`, but remain caller-owned.

A minimized window is treated as temporarily non-drawable even if SDL/Win32 retains or synthesizes
a non-zero cached pixel size. In particular, Wine can report a different minimized aspect ratio.
GDI therefore retains the last logical dimensions and pixel storage until a valid restored
drawable exists; it does not reallocate or clear during that transition.

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

This is **not VSync** and can increase latency or block the CPU. It defaults off. The GDI renderer
loads DWM dynamically; if composition is disabled or `dwmapi.dll`/`DwmFlush` is unavailable, it
falls back to normal non-blocking GDI presentation. `CNA_GDI_DWM_FLUSH` accepts exactly `0` or `1`;
an invalid value retains the off default.

All three `CNA_GDI_*` settings are captured once when the GDI renderer is constructed. Changing the
process environment later cannot change an existing `GraphicsDevice`; recreate the device/renderer
to apply new values. Invalid settings are sanitized and combined into one single-line diagnostic
at construction rather than being reported every frame. Focused internal tests can inject the same
typed configuration directly without mutating process-global state.

## Build

```bash
cmake -S . -B build-gdi \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64.cmake \
  -DCNA_GRAPHICS_RENDERER=GDI \
  -DCNA_BUILD_TESTS=ON \
  -DCNA_BUILD_EXAMPLES=ON \
  -DCNA_MAX_VENDORED_BUILD_JOBS=2
CMAKE_BUILD_PARALLEL_LEVEL=2 cmake --build build-gdi \
  --target cna_test_gdi_smoke cna_test_gdi_2d_regression \
           cna_test_gdi_colormatrix_effect cna_test_gdi_public_stencil \
           cna_test_gdi_public_api \
           cna_test_gdi_applied_state \
           cna_test_gdi_unsupported_features \
           cna_test_gdi_dirty_damage cna_test_gdi_repaint_invalidation \
           cna_test_gdi_presentation_oracle \
           cna_test_gdi_presentation_configuration \
           cna_test_gdi_window_metrics \
           cna_test_gdi_framebuffer_allocation \
           cna_test_gdi_msaa_contract \
           cna_test_gdi_presentation_mode_transaction \
           cna_test_gdi_dc_release_transaction \
           cna_test_gdi_texture_allocation \
           cna_bench_gdi_2d cna_demo_2d -j2
```

The renderer is hard-gated to Windows targets. A native Windows build registers nineteen `GDI`
CTest cases (GDI-078: up from sixteen after GDI-075/076/077 added three focused executables),
including separate default, dirty and halftone configurations; run them with
`ctest -L GDI --output-on-failure`.

The GitHub Actions workflow `GDI Windows CI (MSVC)` is deliberately manual (`workflow_dispatch`).
Its one Windows job builds only CNA plus all seventeen focused correctness executables and runs the
nineteen-case `GDI` CTest label with native MSVC. REMED-BUILD-017 added the presentation-mode
transaction, DC-release transaction, and texture-allocation executables that the explicit workflow
list formerly omitted. This is a compiler and hidden-window correctness gate; it does not replace
the visible lifecycle/DPI checklist required by GDI-061.

GDI and the shared CPU sources live in one renderer archive. CMake declares the real static-library
cycle between that archive and `CNA`, so GNU/MinGW emits a portable repeated-archive link line
without a third archive. The standalone SOFTWARE configuration declares its corresponding cycle
centrally as well; its focused tests no longer depend on a GNU-only `--start-group` workaround.
The preserved feature-branch gates and current adapted MinGW GDI/native Software gates passed.
GDI-071 remains provisional until the manual workflow confirms this layout with native MSVC.

Ordinary cross configurations do not register PE files as Linux-host CTest commands. The final
validation harness exposed the complete nineteen-case inventory and ran it serially through Wine;
for a normal cross build, run the produced executables with an available display:

```bash
# Native Windows
build-gdi\cna_test_gdi_smoke.exe
build-gdi\cna_test_gdi_2d_regression.exe
build-gdi\cna_test_gdi_colormatrix_effect.exe
build-gdi\cna_test_gdi_public_stencil.exe
build-gdi\cna_test_gdi_public_api.exe
build-gdi\cna_test_gdi_applied_state.exe
build-gdi\cna_test_gdi_unsupported_features.exe
build-gdi\cna_test_gdi_dirty_damage.exe
build-gdi\cna_test_gdi_repaint_invalidation.exe
build-gdi\cna_test_gdi_presentation_oracle.exe
build-gdi\cna_test_gdi_window_metrics.exe
build-gdi\cna_test_gdi_framebuffer_allocation.exe
build-gdi\cna_test_gdi_msaa_contract.exe
build-gdi\cna_test_gdi_presentation_mode_transaction.exe
build-gdi\cna_test_gdi_dc_release_transaction.exe
build-gdi\cna_test_gdi_texture_allocation.exe
build-gdi\cna_bench_gdi_2d.exe --frames 4
build-gdi\cna_demo_2d.exe

# Linux host, MinGW cross-build, with a real graphical Wine display
wine build-gdi/cna_test_gdi_smoke.exe
wine build-gdi/cna_test_gdi_2d_regression.exe
wine build-gdi/cna_test_gdi_colormatrix_effect.exe
wine build-gdi/cna_test_gdi_public_stencil.exe
wine build-gdi/cna_test_gdi_public_api.exe
wine build-gdi/cna_test_gdi_applied_state.exe
wine build-gdi/cna_test_gdi_unsupported_features.exe
wine build-gdi/cna_test_gdi_dirty_damage.exe
wine build-gdi/cna_test_gdi_repaint_invalidation.exe
wine build-gdi/cna_test_gdi_presentation_oracle.exe
wine build-gdi/cna_test_gdi_window_metrics.exe
wine build-gdi/cna_test_gdi_framebuffer_allocation.exe
wine build-gdi/cna_test_gdi_msaa_contract.exe
wine build-gdi/cna_test_gdi_presentation_mode_transaction.exe
wine build-gdi/cna_test_gdi_dc_release_transaction.exe
wine build-gdi/cna_test_gdi_texture_allocation.exe
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

## Adapted validation (2026-08-08)

- The current x64 MinGW Release GDI configuration built all seventeen focused correctness
  executables. CTest registered nineteen cases, including the three presentation variants, and all
  **19/19** passed serially through Wine 10 on Linux Xvfb display `:104`. `cna_demo_2d` was
  compile-covered only. The benchmark completed four frames per case and passed; timings are not
  compared because the run was quota-constrained.
- Both genuine PE32 Intel i386 allocation planners passed under Wine: framebuffer **5/5** and
  texture **7/7**. Pixel/oracle coverage passed asymmetric RGBA channels, a top-down
  negative-height DIB, corner/orientation probes, nonzero-Y dirty-band clipping, odd-width upload
  padding, and transactional short-stride rejection. CPU alpha/blend coverage, including corrected
  `SourceAlphaSaturation`, passed. Final GDI presentation is opaque `SRCCOPY` rather than
  desktop-composited alpha.
- Repeated memory-surface create/destroy and injected `GetDC`, `CreateDIBSection`, and
  `SelectObject` failure paths passed, including selected-object restoration and authoritative
  operation counters. The `GetGuiResources` live-delta subcheck skipped under Wine because the API
  was not stable/available; these results do not prove physical-Windows kernel-object leak absence.
- A native SOFTWARE Debug ASan+UBSan focused harness loaded `libasan.so.8` and `libubsan.so.1` as
  confirmed by `ldd`. It selected 151 tests: **149 passed, 2 intentionally skipped, zero CNA
  ASan/UBSan reports**. Six standalone controls passed: effects **7/7**, Additive **29/29**,
  scissor **44/44**, render-target readback **102/102**, SpriteBatch viewport **19/19**, and
  Texture2D GetData **40/40**. LeakSanitizer with `detect_leaks=1` was unusable under ptrace, so the
  valid rerun used `detect_leaks=0`. Full native `CnaTests` cannot compile because the accepted
  Glide `FakeGlide3xDll` fixture includes `windows.h`; Glide was not reopened. The focused run
  excluded the unrelated integration-baseline Software `SetRenderTargets_FourTargets` expectation
  mismatch and Pulse-sensitive capability matrix; neither is a GDI finding.
- REMED-GFX-223's principal current OPENGLES3/EasyGL control passed **8/8** focused runtime
  pixel/state tests: TexturedQuad, BlendState Additive, RT2D readback, render-target
  viewport/scissor reset, InstancedModel, MRT, AdditiveBlendContract, and Texture2D GetData. The
  actual `CnjCacheIsolationTest` passed **2/2** on Mesa OpenGL ES 3.2 llvmpipe/Xvfb display `:105`.
  Shared Texture2D cache code is unchanged, REMED-GFX-223 is preserved, and REMED-GFX-224 remains
  open.
- DIRECTX3 built for x64 MinGW and its `DirectX3_GraphicsCapability` runtime passed **1/1** through
  Wine/Xvfb with the DirectDraw-engagement wrapper, closing REMED-GFX-232 validation. Sokol at
  pinned `27b4960` received a current-source native GLCORE build and passed Smoke, Instanced3D, and
  WireFrame **3/3** on llvmpipe/Xvfb.
- Diligent pinned v2.5.6 `b036337` passed exact generated compile probes for the current
  `DiligentRenderer.cpp` and shared `GraphicsDevice.cpp` under `CNA_RENDERER_DILIGENT`; this
  was compile-only, with no current runtime or full DiligentCore rebuild. Skia pinned `ebf5052`,
  with matching local raster archives, passed equivalent current-source probes under
  `CNA_RENDERER_SKIA`; it was compile-only and emitted only external Skia
  `clang::reinitializes` warnings under GCC. Current 32-bit i686 MinGW
  `GlideRenderer.cpp` and shared `GraphicsDevice.cpp` probes passed under
  `CNA_RENDERER_GLIDE`; Glide was compile-only because `glide3x.dll` was unavailable, and the
  accepted renderer was not reopened.
- REMED-GFX-229/230/231/232/233 and REMED-BUILD-017/018 are resolved for their automated scope;
  REMED-GFX-233 was pre-existing at the integration base and exposed and closed here. GDI-054's
  handle-oracle hardening also passes. No unresolved GDI supported-path finding remains.

This evidence is Linux cross/Wine plus native Linux sanitizer/control coverage, not native MSVC,
physical-Windows lifecycle/DPI, or physical-Windows kernel-object leak proof. All compilation used
explicit numeric parallelism of at most two across the session; final current-tree runs used one
job under `CPUQuota=50%`. No helper was unbounded, `j8` was never reached, and compilation never
exceeded two jobs.

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

`cna_test_gdi_public_api` covers the rest of the advertised public surface: applied presentation
properties and the complete capability matrix, texture upload/readback, SpriteBatch source
selection, viewport/scissor clipping, render-target binding/preservation/sampling, 4x MSAA reset
and resolve, rejected 2x write-back, resize, and backbuffer readback at the new edge.

`cna_test_gdi_applied_state` deliberately requests unsupported backbuffer/RT format, depth and
sample combinations, then compares the normalized public properties with real readback, mip and
rebind behavior. The low-level 2D regression separately checks all five presentation-mode ordinals,
both invalid boundaries, and transactional rejection that leaves the prior mode active.

`cna_test_gdi_unsupported_features` verifies public resources/draws and the complete direct renderer
boundary. Its 42 checks cover cube/3D textures, cube targets and bindings, MRT/array slices,
`ShaderEffect`, occlusion queries, 16/32-bit and dynamic buffers, every depth-only clear/toggle, and
all five draw entries with exact `System::NotSupportedException` diagnostics. A compile-time
assertion also requires GDI to compose, never inherit, `SoftwareRenderer`.

`cna_test_gdi_dirty_damage` verifies the raster-derived rectangle after origin, viewport, scissor,
transform and rotation, plus negative, off-screen and overflow-sized geometry. The repaint test
verifies window-specific lifecycle invalidation, a real SDL-validated Win32 `WM_PAINT`, a real
minimize/restore round-trip, no-draw retained repaint, zero-line DIB failure diagnostics, damage
retention and successful retry. `cna_test_gdi_presentation_oracle` uses a real memory DC/DIBSection
to verify RGBA channel order, top-down rows, both filters and DIB paths, presentation geometry,
bars/cropping, clipping, an exact non-zero-Y dirty band, and deterministic SDL/drawable coordinate
conversion at 100%, 150%, and 200% pixel-density ratios. Its adapted handle-lifetime checks restore
the prior selected object, delete each bitmap/DC across repeated construction and injected failure
checkpoints, and compare `GetGuiResources` before/after only when the API first demonstrates a
stable live DC/DIB delta. Repeated normal/failure cleanup and operation-counter checks pass; the
live-delta subcheck skips under Wine because the API is not stable/available there, so physical
Windows leak absence is not claimed. `cna_test_gdi_window_metrics` adds a live SDL/Win32 client: it
checks external-window input ownership, repeated odd resizes, all presentation
mode round-trips and bars, drawable/client agreement, fullscreen entry/exit where the display
session permits it, and retained pixels across minimize/restore. These automated checks do not
replace GDI-061's visible multi-DPI Windows inspection. The configuration executable then proves
that the registered environment cases select NativeFull, None and Stretch with the requested
filter through renderer telemetry. It also covers strict/default/invalid parsing, one sanitized
aggregate diagnostic, immutable behavior after all process settings change, and a deterministic
typed test override.

`cna_test_gdi_framebuffer_allocation` verifies exact 5-byte and 21-byte GDI layouts, absence of
unused depth storage on both the backbuffer and render targets, optional 4x sample allocation and
release, mip accounting, dimension/budget rejection, and transactional resize failure. Its pure
planner is also compiled and run by the standalone genuine-32-bit arithmetic workflow, which
distinguishes `size_t` overflow from a multiplication-safe request above the byte budget.

`cna_test_gdi_msaa_contract` locks down the deliberately narrow backbuffer contract in 19 checks:
four individual sample-mask bits and fractional resolves, geometric coverage intersection, ignored
high mask bits, crisp wireframe behavior, per-pixel stencil operations, zero-sample stencil
suppression, and all-active-sample gating after a matching or failing stencil comparison. It also
proves that render targets remain single-sampled and disabling backbuffer MSAA releases the sample
plane.

`cna_test_gdi_presentation_mode_transaction` fault-injects an allocation-rejected valid mode
change and proves dimensions, pixels, transforms, damage, and later presentation remain on the
last committed state. `cna_test_gdi_dc_release_transaction` covers checked `ReleaseDC` success,
repeated forced failure, exactly-once release attempts, retained damage/generation, retry, and the
non-throwing fallback destructor. `cna_test_gdi_texture_allocation` covers the CPU texture planner,
malformed/over-budget creation, the REMED-GFX-229 pitched upload contract, and retained pixels.
Together these are the last three of the seventeen correctness executables that REMED-BUILD-017
restored to the native workflow and manual command lists.

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
