# Win32 GDI Graphics Backend — Audit and Remediation Plan

> **Integration adaptation (2026-08-08):** the meaningful 34-commit `feature/gdi` history was
> recreated on `adapt/gdi` from integration base
> `677f4c59e066fc9a7ed79430d0fee5ffd69b531c`. Final adapted cross-validation is now recorded in
> §3: all seventeen x64 MinGW Release correctness executables built and all nineteen registered
> cases passed serially through Wine 10/Xvfb, the two PE32 allocation planners passed, and focused
> native SOFTWARE sanitizer plus current cross-backend controls passed within their stated scope.
> REMED-GFX-229 through REMED-GFX-233 and REMED-BUILD-017/018 are closed by that evidence. Manual
> native MSVC, physical-Windows kernel-object observation, and the visible Windows lifecycle/DPI
> gates remain open.

> **Audit snapshot:** 2026-08-01, commit `56bd8961` (`feature/gdi`).
>
> **Follow-up re-audit:** 2026-08-03, commit `83194d58` (`feature/gdi`). A fresh MinGW-w64
> Release build of CNA plus all fourteen focused correctness executables passes at `-j2`, and all
> sixteen cases pass under Wine on an Xvfb virtual display with `CNA_GDI_DWM_FLUSH=0`. The re-audit
> also found three unremediated implementation gaps and one evidence-maintenance gap, now tracked as
> GDI-075 through GDI-078: valid presentation-mode changes are not transactional when their implicit
> resize fails, the CPU `Texture2D` path does not share GDI-067's allocation safety, `ReleaseDC`
> remains outside the checked presentation commit, and the handoff's source/object-count claims have
> drifted from CMake. Passing the existing suite therefore does not close these new tasks.
>
> **2026-08-03 remediation of the follow-up findings:** GDI-075, GDI-076, and GDI-077 are now
> implemented and GDI-078's documentation/evidence reconciliation is complete (see their entries in
> §6 and §4). Three new focused executables were added
> (`cna_test_gdi_presentation_mode_transaction`, `cna_test_gdi_dc_release_transaction`,
> `cna_test_gdi_texture_allocation`), each proven to fail against the pre-fix code and pass against
> the fix. A fresh MinGW-w64 Release reconfigure plus `-j2` build of all seventeen focused
> executables, and all nineteen native `GDI` CTest cases under Wine/Xvfb with
> `CNA_GDI_DWM_FLUSH=0`, pass. The GDI backend archive now compiles eight shared CPU-2D
> translation units (GDI-076 added `SoftwareTextureAllocation.cpp` to GDI-074's seven) plus GDI's
> own three, a fact `cmake/BackendLibraries.cmake` now prints at configure time instead of relying
> on hand-copied prose (GDI-078). A genuine i686-w64-mingw32 32-bit `size_t` harness for the new
> texture planner (`tools/graphics/texture_allocation_32bit_check.cpp`) closes GDI-076's remaining
> 32-bit-coverage gap; only the shared, cross-backend `Texture2D.cpp` allocation-order improvement
> stays open there. GDI-061 (visible native-Windows lifecycle/DPI gate) and GDI-062 through
> GDI-066 (native visible performance data) remain gates this Linux/Wine sandbox genuinely cannot
> close; GDI-071/GDI-074 additionally need a manual native-MSVC workflow dispatch. The backend is
> still not a release baseline until those pass.
>
> **Assessment at the audited commit:** the backend was a sound Windows-only CPU-2D compatibility
> prototype, but **not a release baseline**. The audit found three correctness blockers: the public
> stencil-clear path was disconnected, dirty damage could omit pixels, and Win32 expose/paint repair
> was not reliable. Its tests predominantly inspected the CPU framebuffer and did not prove what GDI
> put in the window.
>
> **Implementation update (2026-08-01 working tree):** GDI-050 through GDI-060, GDI-067, GDI-070,
> GDI-072, and GDI-073 are implemented. GDI-074 now gives GDI a 2D-only shared-CPU translation
> unit; the remaining textual split of the shared monolith is in progress. GDI-071's source/archive
> boundary is implemented and locally verified with native GCC plus MinGW, but its first native-MSVC
> workflow result is still pending.
> The focused MinGW Release build, all fourteen GDI correctness
> executables, and all three configuration variants pass under Wine. The suite includes a real
> memory-DC/DIBSection pixel oracle, complete public-path coverage for advertised features, exact
> dirty-damage coverage, event/failure-retention integration, DPI-coordinate oracles, live
> odd-resize/fullscreen/minimize
> lifecycle coverage, and distinct default/dirty/halftone cases. The first manual
> native-MSVC workflow result and the visible Windows lifecycle/DPI gate remain open, so the backend
> is still not a release baseline. The follow-up GDI-075 through GDI-078 findings above are also open
> and must be resolved before release-baseline status.
>
> **Status legend:** ✅ implemented and adequately verified for its stated scope; 🟨 implemented but
> incompletely verified or with a provisional conclusion; 🔴 confirmed correctness defect;
> ⬜ not started; ⏸ blocked by an external prerequisite; 🚫 intentionally out of scope.

---

## 1. Intended contract

GDI remains a modest-workload Windows 2D backend. It must use a real Win32 GDI display path; an
SDL renderer, OpenGL context, Direct3D device, or GPU swap chain would be another backend rather
than an improvement to this one.

The supported slice is:

- RGBA8 `Texture2D`, CPU `SpriteBatch`, source rectangles, origin/rotation/flip/transform,
  viewport/scissor, XNA blend factors/equations, and point/linear CPU sampling;
- one RGBA8 `RenderTarget2D`, readback, and generated render-target mips;
- the fixed CPU `ColorMatrixEffect`;
- optional backbuffer-only 4x CPU MSAA; and
- a CNA-specific 8-bit, stencil-only 2D masking plane.

The default boundary remains no general 3D pipeline, no depth, MRT, cube/volume resources,
occlusion queries, anisotropic filtering, or arbitrary shader effects. The audit does not authorize
GDI-024, GDI-030 through GDI-034, or GDI-040 through GDI-043.

All build and test commands for this plan use explicit numeric parallelism of at most two jobs
(`-j2`, `--parallel 2`, or lower). Across the complete adaptation/validation session the maximum
compilation parallelism was two; the final current-tree runs used one job under `CPUQuota=50%`.
No helper was unbounded, and the obsolete historical `-j8` value was never reached.

---

## 2. Current backend implementation

```text
GraphicsDevice / SpriteBatch public API
                 |
                 v
GdiGraphicsBackend : IGraphicsBackend
  - HWND acquisition and presentation policy
  - typed construction-time environment configuration
  - window/logical coordinate transforms
  - SDL window-event invalidation generation
  - raster-derived damage and 2D-only capability boundary
                 | explicit reviewed 2D forwards only
                 v
GdiSoftware2DCore (private composition adapter)
  - owns the reusable SoftwareGraphicsBackend services
  - CPU texture/SpriteBatch/render-target rasterization
  - RGBA8 + independently optional float depth, 8-bit stencil, and 4x colour samples
                 |
                 v
ResolveColor -> GdiPresentation planner -> scoped GetDC
             -> SetDIBitsToDevice (1:1 full/dirty)
                or StretchDIBits (scaled) -> GdiFlush -> optional DwmFlush
```

### Build integration

- [`cmake/BackendSelection.cmake`](../cmake/BackendSelection.cmake) hard-gates `GDI` to a Windows
  target and selects `cna_backend_graphics_gdi`.
- [`cmake/BackendLibraries.cmake`](../cmake/BackendLibraries.cmake) puts the three GDI translation
  units and an explicit, reviewed eight-file CPU-2D dependency list in one GDI backend archive.
  No Software glob or intermediate `software_core` archive remains. `SoftwareFramebuffer.cpp`,
  `SoftwareTexture2D.cpp`, and `SoftwareRenderTarget2D.cpp` are independently compiled shared
  resource units; `SoftwareGraphicsBackend2DState.cpp` owns backend lifecycle, binding, readback,
  and state application; `SoftwareSpriteBatch.cpp` owns SpriteBatch geometry/transform/effect
  preparation. `SoftwareGraphicsBackend2D.cpp` retains the shared triangle raster bridge while
  deliberately excluding Software cube/resources and 3D draw bodies. The full SOFTWARE backend
  compiles those shared units plus `SoftwareGraphicsBackend.cpp`; its remaining source-text
  monolith is tracked by GDI-074.
- [`cmake/CnaLibrary.cmake`](../cmake/CnaLibrary.cmake) declares the real `CNA` ↔ GDI and `CNA` ↔
  SOFTWARE static-library cycles needed by GNU/MinGW archive scanning. Software tests no longer
  carry their own GNU-only `--start-group` workaround.

### Raster and resource path

- [`GdiGraphicsBackend`](../include/CNA/Internal/Backends/Gdi/GdiGraphicsBackend.hpp) derives directly
  from `IGraphicsBackend` and owns a private `GdiSoftware2DCore`. Only reviewed texture,
  SpriteBatch, render-target, state, and framebuffer operations are forwarded; the complete
  resource/3D boundary is implemented explicitly on GDI.
- [`SoftwareFramebuffer`](../include/CNA/Internal/Backends/Software/SoftwareGraphicsBackend.hpp) owns
  the resolved RGBA8 image plus independently selected float-depth, 8-bit-stencil, and four-sample
  RGBA planes. GDI selects RGBA8 plus stencil and therefore pays no depth allocation.
- GDI render targets deliberately force depth and MSAA to zero and explicitly expose their
  always-present standalone Software stencil allocation. `GraphicsDevice` now asks depth and
  stencil attachment questions independently.
- Backbuffer and render-target allocations use GDI-067's checked 512 MiB framebuffer planner.
  GDI-076 gave the composed `SoftwareTextureBackend` an equivalent checked 512 MiB
  `SoftwareTextureAllocation` layout/budget, pixel-length agreement validation, and
  `bad_alloc`/`length_error` translation at the GDI/Software CPU-texture boundary
  (`GdiGraphicsBackend::CreateTexture`). The public per-axis maximum is still 16,384, but a request
  whose byte cost exceeds the budget (a square level at that limit alone would be 1 GiB) is now
  rejected there rather than silently allocated. The ordinary public `Texture2D` constructors
  (shared by every CNA backend) still allocate their own first `width*height*4` vector before
  that boundary is reached, so an over-budget public request still pays for one wasted transient
  allocation first; GDI-076 remains 🟨 for that deliberately cross-backend residue.

### Presentation path

- `Present()` synchronizes the logical backbuffer, resolves MSAA, reads SDL's drawable-pixel size,
  and delegates Native/Stretch/Letterbox/Overscan/FixedHeightDynamicWidth geometry and path
  selection to a deterministic presentation planner.
- SDL event/warp positions remain in SDL window-coordinate space. The backend converts them through
  the `SDL_GetWindowSize()` to `SDL_GetWindowSizeInPixels()` ratio before/after presentation
  geometry, and treats a minimized window as non-drawable so cached dimensions cannot erase the
  retained dynamic-width framebuffer.
- A 1:1 result uses `SetDIBitsToDevice`; a scaled result uses `StretchDIBits`. The source is a
  top-down 32-bit `BI_BITFIELDS` DIB whose masks match CNA's in-memory RGBA byte order.
- The shared SpriteBatch rasterizer reports clipped candidate-pixel bounds after origin, rotation,
  transform, viewport, and scissor. An SDL event watch independently advances a native-client
  invalidation generation for expose/restore/resize/display lifecycle events.
- HDC acquisition, DIB submission, `GdiFlush`, and explicit `ReleaseDC` form a checked transaction.
  Damage and the captured native invalidation generation are acknowledged only after all those
  operations succeed. The non-throwing RAII destructor is fallback cleanup for exceptional exits
  and does not repeat an explicit release; GDI-077's failure/retry oracle covers this boundary.
- Full presentation currently fills the entire client black before every blit, including Stretch
  and Overscan where the following blit already covers the client.
- Process environment options select `halftone` scaling, dirty presentation, and optional
  `DwmFlush`. Each backend captures all three once into typed configuration during construction;
  `Present()` never re-reads mutable process state. Invalid values retain safe defaults and are
  aggregated into one construction-time diagnostic.

### Existing tests

[`cmake/Tests/GdiTests.cmake`](../cmake/Tests/GdiTests.cmake) builds smoke, CPU 2D regression,
ColorMatrix integration, public-stencil, complete public-API, applied-state, unsupported-feature,
dirty-damage, repaint/failure, presentation-oracle, presentation-configuration, window-metrics,
framebuffer-allocation, MSAA-contract, presentation-mode transaction, DC-release transaction,
texture-allocation, and benchmark executables. That is seventeen correctness executables plus one
non-CTest benchmark. It registers nineteen cases (sixteen one-to-one cases plus three environment
configurations) as CTests only for a native Windows build. The manual `gdi-windows-ci.yml` workflow
configures the backend with MSVC, builds only CNA plus all seventeen focused tests, and runs the
`GDI` CTest label.

---

## 3. Reproduced baseline

The audit configured and built the four focused targets with MinGW-w64/Ninja:

```bash
cmake -S . -B build-gdi-audit -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64.cmake \
  -DCNA_GRAPHICS_BACKEND=GDI \
  -DCNA_ENABLE_NET=OFF \
  -DCNA_BUILD_EXAMPLES=OFF \
  -DCNA_BUILD_TESTS=ON \
  -DCMAKE_BUILD_TYPE=Release \
  -DCNA_MAX_VENDORED_BUILD_JOBS=2

cmake --build build-gdi-audit \
  --target cna_test_gdi_smoke cna_test_gdi_2d_regression \
           cna_test_gdi_colormatrix_effect cna_bench_gdi_2d -j2
```

Results at the audited commit:

- configure and all four links: **pass**;
- `cna_test_gdi_smoke.exe` under Wine: **pass**;
- `cna_test_gdi_2d_regression.exe` under Wine: **pass**;
- `cna_test_gdi_colormatrix_effect.exe` under Wine: **pass**;
- `ctest -N -L GDI` in the cross-build: **0 tests**, intentionally, because cross-built PE files
  are not registered; and
- the shared Software compilation emits an allocation-size warning in its cube-texture code. GDI
  rejects cube textures at runtime, but it compiles the whole Software translation unit and thus
  still carries its build risk.

These passes prove CPU raster/readback behavior and that GDI calls do not fail in the tested hidden
Wine window. They do **not** prove channel order, orientation, scaling, clipping, damage, black bars,
or repaint behavior in a visible native Windows client.

### Historical Phase G5/G6 validation

At the 2026-08-01 milestone, the feature working tree built all fourteen then-current correctness
executables together with MinGW-w64/Ninja at `-j2`.
Under Wine, smoke, 2D regression, ColorMatrix, public stencil, the complete public API matrix, dirty
damage, unsupported-feature rejection, framebuffer allocation, the 19-check MSAA contract,
repaint/failure, and the memory-DC presentation oracle all pass. The window-metrics integration
covers external ownership, odd resize, every mode, fullscreen, and minimize retention, while
presentation configuration also passes in its default, dirty, and halftone environments with
`CNA_GDI_DWM_FLUSH=0`. Unlike the audited baseline, the memory-DC test proves the exact GDI-produced
pixels on a deterministic selected bitmap. It still does not replace the visible native Windows
lifecycle/DPI gate.

The first 2026-08-03 follow-up independently repeated the focused Release configure/build and the
then-complete sixteen-case Wine matrix under `xvfb-run`, again at `-j2`; all cases passed. This is
strong evidence for
the paths they exercise, but the suite has no fault-injected valid-mode allocation rollback, no
ordinary `Texture2D` allocation-boundary matrix, and no observable `ReleaseDC` result. Those omissions
correspond directly to F9 through F11 below rather than weakening unrelated passing assertions.

### 2026-08-08 adapted-branch final validation

The production/test tree through `4fc0a0d0f4d8f2f4a18e839cf41e918f733ae1a2` received the
following current validation. These results supersede only earlier statements that adapted
execution was pending; the dated feature-branch milestones above remain provenance.

- **GDI x64:** the current MinGW Release configuration built all seventeen focused correctness
  executables. CTest registered nineteen correctness cases, including the three presentation
  configuration variants, and all **19/19** passed serially through Wine 10 on Linux Xvfb display
  `:104`. `cna_demo_2d` was compile-covered only. The benchmark ran four frames per case and passed;
  its quota-constrained timings are intentionally not compared or used as performance evidence.
- **32-bit allocation arithmetic:** both standalone binaries were genuine PE32 Intel i386 images
  and passed under Wine: framebuffer planner **5/5**, texture planner **7/7**.
- **Pixel and presentation oracles:** asymmetric RGBA channels, the negative-height top-down DIB,
  corner/orientation probes, nonzero-Y dirty-band clipping, odd-width padded upload, and
  transactional short-stride rejection all passed. CPU alpha/blend coverage, including corrected
  `SourceAlphaSaturation`, passed. Final GDI presentation remains an opaque `SRCCOPY`; CNA alpha is
  consumed by CPU blending and is not desktop-composited by GDI.
- **Win32 lifetime oracle:** repeated memory-surface create/destroy and injected
  `GetDC`/`CreateDIBSection`/`SelectObject` failures passed, including selected-object restoration
  and authoritative operation counters. The `GetGuiResources` live-delta subcheck skipped because
  Wine did not provide a stable/available counter. This is strong cleanup-path evidence, not proof
  that a physical Windows process leaks no kernel objects.
- **Native SOFTWARE sanitizers:** `ldd` proved `libasan.so.8` and `libubsan.so.1` were active for the
  Debug focused harness. Of 151 selected tests, **149 passed and 2 intentionally skipped**, with
  zero CNA ASan/UBSan reports. Six standalone controls also passed: effects **7/7**, Additive
  **29/29**, scissor **44/44**, render-target readback **102/102**, SpriteBatch viewport **19/19**,
  and Texture2D GetData **40/40**. LeakSanitizer with `detect_leaks=1` is unusable under the
  supervising ptrace environment, so the valid sanitizer run used `detect_leaks=0`. The full
  native `CnaTests` target cannot compile because the accepted Glide
  `FakeGlide3xDll` fixture includes `windows.h`; this does not reopen Glide. The focused set also
  excluded the unrelated integration-baseline `Software SetRenderTargets_FourTargets` expectation
  mismatch and Pulse-sensitive capability matrix; neither is a GDI finding.
- **REMED-GFX-223 control:** the principal current OPENGLES/EasyGL runtime control passed **8/8**
  focused pixel/state executables (`TexturedQuad`, Additive BlendState, RT2D readback, render-target
  viewport/scissor reset, `InstancedModel`, MRT, Additive contract, and Texture2D GetData), plus
  the actual `CnjCacheIsolationTest` **2/2**, on Mesa OpenGL ES 3.2 llvmpipe/Xvfb display `:105`.
  Shared Texture2D cache code is unchanged, so REMED-GFX-223 remains preserved; REMED-GFX-224
  remains open.
- **Other backend controls:** DX3 built for x64 MinGW and `Dx3_GraphicsCapability` passed **1/1**
  through Wine/Xvfb with the DirectDraw-engagement wrapper, closing REMED-GFX-232 validation.
  Sokol at pinned `27b4960` built from current sources for native GLCORE and passed Smoke,
  Instanced3D, and WireFrame **3/3** on llvmpipe/Xvfb. Diligent at pinned v2.5.6 `b036337` passed
  generated compile probes for the current `DiligentGraphicsBackend.cpp` and shared
  `GraphicsDevice.cpp` under `CNA_BACKEND_DILIGENT`; this was compile-only, without a current
  runtime or full DiligentCore rebuild. Skia at pinned `ebf5052` with matching local raster archives
  passed the corresponding current-source compile probes under `CNA_BACKEND_SKIA`; it was
  compile-only and emitted only external Skia `clang::reinitializes` warnings under GCC. Current
  32-bit i686 MinGW Glide backend/shared-device compile probes passed under `CNA_BACKEND_GLIDE`;
  Glide was compile-only because no `glide3x.dll` runtime was available.

REMED-GFX-229/230/231/232/233, REMED-BUILD-017/018, and GDI-054's handle-oracle hardening are
validated for their automated scope. REMED-GFX-233's mechanism pre-existed at integration base
`677f4c59` and was exposed and closed here. No unresolved GDI supported-path finding remains.
These are Linux cross/Wine and native Linux sanitizer/control results, not native MSVC, physical
Windows lifecycle/DPI, or physical-Windows kernel-object-leak proof.

---

## 4. Audit findings

### F1 — public stencil clears are dropped (P0, resolved by GDI-050)

GDI reports `SupportsDepthStencil() == false`, which is correct for depth. However,
[`GraphicsDevice::Clear`](../src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp) uses that one
combined result to mask **both** `DepthBuffer` and `Stencil` down to `Target`. Render targets use the
same conflated `HasRealDepthBuffer()` decision. Therefore public
`GraphicsDevice::Clear(ClearOptions::Stencil, ...)` never reaches GDI's real `ClearStencil()`, on
either the backbuffer or a render target.

The regression missed this because it calls `IGraphicsBackend::ClearStencil()` directly. GDI-026
is consequently not complete at the public API boundary, and `DepthStencilBuffer=false` gives users
no separate way to discover the stencil-only extension.

**Resolution:** backend and render-target interfaces now report real depth and real stencil
independently, `GraphicsDevice::Clear` masks the two flags independently, and the appended
`GraphicsCapability::StencilBuffer` advertises GDI's always-present standalone plane without
changing existing capability ordinals. The public test covers stencil masking and reset on both
the backbuffer and a depthless `RenderTarget2D`.

### F2 — dirty bounds can be smaller than the pixels actually written (P0, resolved by GDI-051)

[`GdiSpriteBatchBackend::MarkDraw`](../src/CNA/Internal/Backends/Gdi/GdiGraphicsBackend.cpp) records
the raw destination rectangle when rotation is zero and the batch transform is identity. The shared
rasterizer subsequently:

- moves the quad by a non-zero `origin`; and
- adds `Viewport.X/Y` after transforming viewport-local sprite coordinates.

Those effects are visible in the actual quad construction in
[`SoftwareGraphicsBackend.cpp`](../src/CNA/Internal/Backends/Software/SoftwareGraphicsBackend.cpp),
but absent from the damage rectangle. With `CNA_GDI_DIRTY_PRESENTATION=1`, valid pixels can remain
stale. Rectangle addition also uses signed `int` without overflow-safe widening.

The existing dirty test only reads the updated CPU pixel after `Present()`. It neither enables the
option in CTest nor observes which rectangle GDI submitted, so it cannot detect this defect.

**Resolution:** the pre-raster GDI estimate was removed. The shared Software SpriteBatch path now
emits conservative clipped raster bounds after origin, rotation, batch transform, viewport origin,
viewport clipping, and scissor. Float-to-int conversion is range-checked before conversion and GDI
unions the reported inclusive bounds with widened arithmetic. The focused test covers every case
listed in GDI-051, including huge and fully off-screen coordinates.

### F3 — expose repair relies on an update region SDL may already have validated (P0, resolved by GDI-052)

Dirty presentation treats a non-empty `GetUpdateRect()` as the signal to repaint the full frame.
Microsoft documents that `BeginPaint` validates the update region, after which `GetUpdateRect()` is
empty. More directly, the [vendored SDL Win32 `WM_PAINT`
handler](../third_party/SDL/src/video/windows/SDL_windowsevents.c) explicitly calls `ValidateRect()`
before sending `SDL_EVENT_WINDOW_EXPOSED`. CNA polls that event before rendering, but its game loop
does not turn exposed or restored into graphics invalidation. GDI's later `GetUpdateRect()` can
therefore be empty even though the client needs its retained frame redrawn.

The backend must not use `GetUpdateRect()` alone as proof that the retained client pixels are valid.
Occlude/expose and minimize/restore need an explicit full-damage signal or a proper paint strategy.

**Resolution:** a per-window SDL event watch advances an atomic native-invalidation generation for
expose, show, restore, resize/pixel-size, focus, display/scale, safe-area, and fullscreen events.
`Present()` acknowledges only the generation snapshot it successfully copied, so an event arriving
during a present cannot be lost. `GetUpdateRect()` is only a secondary hint, and a temporarily
zero-size/minimized client preserves the last valid logical framebuffer size. Deterministic event
integration plus a real `InvalidateRect`/`UpdateWindow` → SDL `WM_PAINT` cycle verifies that repair
still occurs after the Win32 update region is empty. The test also performs a real minimize/restore
round-trip without drawing and verifies retained logical storage. Broader visible occlusion/DPI
inspection remains GDI-061.

### F4 — Win32 result handling is incomplete (P0, resolved by GDI-053)

The scaled branch treats only `GDI_ERROR` as `StretchDIBits` failure. The documented uncompressed
DIB contract returns **zero** on failure or when no scan line is copied, so ordinary failures can be
accepted as successful and damage is then reset. `FillRect`, `SetStretchBltMode`, and `GdiFlush`
results are also ignored, diagnostics contain no Win32 error context, and HDC release is manually
duplicated across returns and exception handling.

The exact `SetDIBitsToDevice` success rule also needs a deterministic test for a clipped client and
for a non-zero dirty `y`/`StartScan`; comparing against the requested height may reject legal
clipping, while the current parameter combination has never been pixel-verified.

**Resolution:** window DC ownership is scoped; setup, fill, DIB, stretch-mode restoration, and
`GdiFlush` results are checked; and failures include their operation plus formatted Win32 context.
Zero/`GDI_ERROR` DIB results retain all pending damage. A dirty band now passes a pointer to the
band's first source row and a band-local top-down DIB. Tests inject zero-line failure, verify
retention/retry, and accept a positive clipped `SetDIBitsToDevice` result rather than requiring the
requested height.

### F5 — presentation has no deterministic oracle (P0, resolved by GDI-054)

All three tests create hidden windows. The smoke test clears/readbacks the CPU buffer and merely
calls `Present()`. The large regression performs almost every assertion through
`ReadBackbuffer()`, which bypasses GDI. The ColorMatrix test is a useful public-API test, but it also
ends at CPU readback. There is no memory-DC/DIB output test, no presentation telemetry, and no
visible lifecycle automation.

**Resolution:** presentation geometry and path selection are pure functions, with a small checked
DIB-to-HDC helper and backend telemetry. A top-down 32-bit DIBSection selected into a memory DC now
validates channel order, row orientation, both blit paths and filters, every presentation mode,
odd geometry, bars/cropping, coordinate edges, dirty clipping, a non-zero-Y dirty band, and injected
failure without relying on CPU backbuffer readback.

### F6 — native Windows compilation and CTest were not exercised in CI (P1, resolved by GDI-057)

The CMake registration is suitable for native Windows, but no workflow selects GDI. The existing
manual Windows workflow is explicitly scoped to D3D11/D3D12. This leaves MSVC compilation, native
GDI behavior, and the registered `GDI` label unobserved.

**Resolution:** a separate owner-approved, `workflow_dispatch`-only workflow uses one
`windows-latest` MSVC/Ninja job. It checks out only the required sibling dependency, caches the
vendored SDL build, builds CNA plus all seventeen focused correctness executables at two-way
parallelism, runs the nineteen native `GDI` CTest cases, and uploads CTest/CMake diagnostics on
failure. REMED-BUILD-017 supplied the three follow-up targets omitted from the original explicit
list. It remains a compiler/hidden-window gate and explicitly does not close the visible GDI-061
gate.

### F7 — public configuration and rejection semantics are not consistently honest (P1,
resolved by GDI-058 and GDI-059)

- The GDI factory applies a requested MSAA count, but direct `GraphicsDevice` construction does not
  write the clamped result back to its stored `PresentationParameters`; `Reset()` does.
- Backbuffer/depth formats are passed to the factory but GDI ignores them. Render-target depth,
  MSAA, and preservation requests are also silently normalized by the backend while public objects
  may continue to report the request.
- Texture3D rejects at construction via a capability check, TextureCube constructs with a null
  backend and fails on later data access, ShaderEffect constructs invalid and fails when used by
  SpriteBatch, and OcclusionQuery throws a generic runtime error immediately.
- `SetPresentationMode(int)` accepts any ordinal and casts it to the enum.

**GDI-058 resolution:** GDI validates all presentation-mode ordinals before mutation. Backbuffer
format/depth requests normalize to the fixed RGBA8/no-depth storage on construction, store-only
updates, and reset; initial and reset MSAA both write back the backend's actual 0x/4x result. A
render target rejects non-`Color` format, reports its actual `DepthFormat::None` and 0x MSAA while
retaining the independent always-present stencil plane, and its three usage policies are tested
against storage after rebind.

**GDI-059 resolution:** every excluded GDI resource factory and 3D backend entry now throws
`System::NotSupportedException`. Cube textures, cube render targets, shader effects, occlusion
queries, static/dynamic vertex and 16/32-bit index buffers fail during public construction;
`Texture3D` retains its still-earlier public capability guard. Public user-draw and depth-state
calls therefore fail before private Software 3D state or storage can be used. A focused public
test covers each excluded family and confirms `ThreeD=false` plus the resource-specific capability
answers.

### F8 — lifecycle, cost, and architecture claims are ahead of evidence (P1/P2)

- Presentation used Win32 client pixels while input callbacks supplied SDL window coordinates,
  with no explicit density conversion. Some minimized clients also expose a misleading non-zero
  cached SDL pixel size that can change the dynamic-width aspect and clear retained pixels.
- `ResolveColor()` walks the complete framebuffer at the start of every MSAA `Present()`, even when
  dirty mode has no damage or only a small rectangle.
- The hidden-Wine benchmark does not measure a visible compositor path; it cannot close the
  DIBSection, flush, dirty-blit, or pacing decisions.
- At the audited commit, four-sample colour was real for solid triangle coverage, but wireframe
  wrote all samples and depth/stencil remained per pixel; that limited contract was not fully
  documented or tested.
- Before GDI-067, GDI allocated an unused float-depth plane for every framebuffer. At 4x it retained
  roughly 25 bytes/pixel across resolved colour, depth, stencil, and sample colour before container
  overhead.
- Inheriting the entire Software 3D backend and globbing its whole source makes the 2D boundary
  fragile: a newly added virtual path can become reachable unless GDI remembers to override it.
- Presentation policy was decoded from mutable process environment state inside every `Present()`,
  so behavior could change during a device's lifetime without an API state change.

**GDI-060 resolution for the lifecycle/coordinate portion:** SDL drawable pixels are now the one
presentation-size authority, SDL event/warp coordinates are converted through the explicit
window-coordinate/drawable ratio, and minimized state is treated as non-drawable before consulting
cached metrics. A live integration test found the retained-pixel failure under Wine and now proves
external-window ownership, odd resize, all modes, fullscreen, and minimize/restore.

**GDI-067 resolution for the memory portion:** framebuffer attachment storage is independently
selectable; GDI now commits RGBA8 plus its real stencil plane (5 bytes/pixel), adding the 4x colour
plane only when applied (21 bytes/pixel total). A side-effect-free planner checks positive and
16,384-axis dimensions, every `size_t` operation, generated mip storage, the Win32 `DWORD` boundary,
and a 512 MiB per-resource pixel-storage budget before allocation. Allocation failures are translated
to `System::OutOfMemoryException`, rejected resizes retain the previous framebuffer, and a genuine
32-bit MinGW harness covers overflow separately from budget rejection. The remaining cost and
architecture bullets map to GDI-062 through GDI-066, GDI-071, and GDI-074.

**GDI-070 resolution for the runtime boundary:** `GdiGraphicsBackend` now derives directly from
`IGraphicsBackend` and privately owns a `GdiSoftware2DCore` composition adapter. No pointer to the
complete Software backend escapes; reviewed 2D operations are forwarded explicitly, while cube/
volume resources, cube/MRT/array-slice bindings, effects, queries, buffers, depth-only operations,
and every draw entry remain explicit `NotSupportedException` paths. A compile-time assertion
forbids reintroducing Software inheritance. The expanded focused test exercises the public paths
plus every direct resource/3D virtual boundary in 42 assertions.

**GDI-071 resolution for the build boundary:** the GDI build no longer globs the Software
directory or creates a third static archive. Its single backend archive names only the reviewed
`SoftwareFramebufferAllocation.cpp`, `SoftwareFramebuffer.cpp`, `SoftwareTexture2D.cpp`,
`SoftwareRenderTarget2D.cpp`, `SoftwareGraphicsBackend2DState.cpp`, `SoftwareSpriteBatch.cpp`,
and `SoftwareGraphicsBackend2D.cpp` units, so future Software files cannot enter GDI without
review, and the link graph is reduced to the honest `CNA` ↔ GDI cycle.
An independent full GCC SOFTWARE build exposed the same undeclared reverse edge there
(`ColorMatrixEffect::FillSpriteDrawParams`); declaring `CNA` ↔ SOFTWARE fixed ordinary executable
links and removed the tests' GNU-only archive group. The complete focused MinGW build and all
sixteen Wine/Xvfb cases pass, as does the full native GCC link. The remaining source-text monolith
is tracked by GDI-074, and the native-MSVC result remains the only GDI-071 verification gap.

**GDI-072 resolution for the configuration portion:** each backend now owns one const
`GdiConfiguration` snapshot. A pure parser accepts only `nearest`/`halftone` and `0`/`1`, preserves
the corresponding safe default for every invalid value, sanitizes values, and combines all errors
into one diagnostic emitted during construction. The focused configuration executable mutates all
three environment variables after construction and also injects a contrary typed override; filter,
dirty policy, and stored DWM choice remain deterministic.

**GDI-073 resolution for the multisample-contract portion:** the advertised 4x mode is now
explicitly limited to the GDI backbuffer's filled SpriteBatch triangles. Their four 2x2-grid colour
samples intersect geometric coverage with `MultiSampleMask` bits 0 through 3 before fragment tests;
higher bits are ignored. Wireframe remains a crisp one-pixel DDA path that writes every enabled
sample in a visited pixel, not subpixel-antialiased lines. Stencil remains one byte per pixel: one
comparison/operation per covered triangle fragment gates all active colour samples, with no
per-sample depth/stencil attachment. Render targets stay single-sampled. A focused 19-check pixel
test covers fractional masks, edge coverage, wireframe, stencil/mask ordering, and MSAA disable.

**GDI-074 partial resolution for the physical build boundary:** GDI now compiles
`SoftwareGraphicsBackend2D.cpp`, which defines `CNA_SOFTWARE_2D_ONLY` before compiling the shared
CPU raster/SpriteBatch implementation. `SoftwareFramebuffer.cpp`, `SoftwareTexture2D.cpp`, and
`SoftwareRenderTarget2D.cpp` now own the corresponding reusable resource definitions and share a
small allocation-error helper. `SoftwareGraphicsBackend2DState.cpp` owns the reusable backend
lifecycle, target binding, readback, and 2D state application. `SoftwareSpriteBatch.cpp` owns the
public SpriteBatch geometry/transform/effect preparation and submits four prepared corners through
the private `RasterizeSpriteQuad` bridge. The wrapper excludes Software vertex/index buffers, cube
resources and sampling, programmable effects, and all general-3D draw bodies. Small explicit
`NotSupportedException` stubs retain the complete virtual table required by the private 2D adapter.
Archive/symbol inspection confirms the GDI archive contains no Software cube implementation or
general-3D rasterizer; consequently it no longer produces the cube allocation warning. The full
SOFTWARE build compiles the unguarded implementation and retains its feature set. The shared
triangle raster helpers and the `RasterizeSpriteQuad` bridge remain centralized in
`SoftwareGraphicsBackend.cpp` through the wrapper, so their extraction remains part of GDI-074.

### F9 — a valid presentation-mode change can leave the backend in a failed half-state (P1,
resolved by GDI-075)

[`GdiGraphicsBackend::SetPresentationMode`](../src/CNA/Internal/Backends/Gdi/GdiGraphicsBackend.cpp)
assigns `presentationMode_` before calling the fallible `SynchronizeBackbufferSize()`. Validation of
ordinals is transactional, and `SoftwareFramebuffer::Resize` itself is transactional, but the
surrounding mode change is not. A valid switch to `FixedHeightDynamicWidth` can derive a width above
the 16,384-axis limit or 512 MiB budget from the current drawable aspect and requested virtual
height. The resize then throws after the new mode has already been stored. The old framebuffer and
pixels survive, but subsequent `GetViewportSize()`, `Present()`, `Clear()`, or SpriteBatch begin can
repeat the same failing synchronization instead of continuing with the prior presentation policy.

The current regression proves only that invalid ordinals preserve Letterbox. It does not inject a
failure for a valid ordinal whose implicit resize is rejected. GDI-075 must make the whole
mode/size/damage transition commit or roll back as one operation and add that missing test.

**Resolution:** `SetPresentationMode` now saves `presentationMode_` before assigning the requested
ordinal, and restores it in a `catch (...)` around `SynchronizeBackbufferSize()` before rethrowing
-- the identical pattern `SetVirtualResolution` already used, and safe for the same reason:
`SoftwareFramebuffer::Resize` is itself transactional, so restoring only the mode field fully rolls
back the whole request. `cna_test_gdi_presentation_mode_transaction` starts from a valid retained
Letterbox framebuffer, resizes the real window to an extreme aspect, switches to
`FixedHeightDynamicWidth` (rejected with `System::ArgumentOutOfRangeException`), and verifies the
old mode's dimensions, pixels, and damage are unchanged and that a subsequent `Present()` succeeds
-- proving the backend does not get stuck repeating the same failing synchronization. Public
`GraphicsDevice::SetPresentationMode(int)` is a private, single-line, stateless forward reachable
only through `GraphicsDeviceManager` (which requires a live `Game` in this codebase), so it adds no
state of its own to duplicate at that layer; the test file documents this explicitly.

### F10 — CPU `Texture2D` allocation bypasses the GDI framebuffer safety contract (P1,
resolved by GDI-076)

GDI reports a 16,384 maximum texture dimension, while the ordinary public `Texture2D` constructors
check only that upper per-axis value and then allocate `width * height * 4` bytes. A square RGBA8
level at the advertised limit is 1 GiB before transient copies or mip storage. The composed
[`SoftwareTextureBackend`](../src/CNA/Internal/Backends/Software/SoftwareTexture2D.cpp) copies the
provided vector without using GDI-067's checked layout/budget planner, without translating
`std::bad_alloc`/`std::length_error`, and without validating that direct `ImageData` dimensions and
pixel count agree. Decoded/asset paths also need the same backend-boundary guard rather than relying
only on the integer-dimension check in selected public constructors.

GDI-067 is correctly scoped to framebuffers, so its completed status does not cover this gap. The
existing unsupported-feature test merely compares the reported integer ceiling; it never attempts
the boundary allocation. GDI-076 must define a truthful CPU-texture byte contract and enforce it
before the first large public allocation as well as at the direct backend entry.

**Resolution:** new `SoftwareTextureAllocation.hpp`/`.cpp` mirrors GDI-067's framebuffer planner --
positive dimensions, the shared 16,384-axis ceiling, every declared mip level's bytes (not just
level 0), and a 512 MiB per-resource budget (`SoftwareTextureMaxBytes`) -- computed before any
vector allocation. `SoftwareTextureBackend`'s two constructors and `UpdatePixels`/
`UpdatePixelsLevel` now validate this layout, reject a supplied pixel buffer smaller than its own
level (`System::ArgumentException`, closing the out-of-bounds-read risk), truncate an oversized one
to exactly `width*height*4` instead of retaining the excess, and translate
`std::bad_alloc`/`std::length_error` to `System::OutOfMemoryException`. `GdiGraphicsBackend::
CreateTexture` forwards directly into this checked constructor, so the direct backend entry and the
GDI/Software CPU-texture boundary are the same call. This deliberately stops at that boundary: the
shared `Texture2D.cpp` public constructors (identical across all configured CNA backends) still
allocate their own first `width*height*4` vector before ever reaching it, so an over-budget public
request still pays for one wasted transient allocation before GDI/Software's own rejection is
reached -- eliminating that is a cross-backend change this GDI-only task deliberately leaves open
rather than touching a shared file this session cannot verify against every other backend.
`cna_test_gdi_texture_allocation` covers the pure planner (byte-per-pixel/mip-chain/overflow/budget
math) and the live GDI boundary (ordinary creation, buffer truncation, undersized-buffer rejection,
over-budget rejection, non-positive-dimension rejection). A genuine i686-w64-mingw32 32-bit
`size_t` harness (`tools/graphics/texture_allocation_32bit_check.cpp`, wired into the standalone
`tools/media/arithmetic32bit` project and `32bit-arithmetic-ci.yml` alongside GDI-067's own
framebuffer harness) additionally proves the mip-chain summation and budget checks on a real
32-bit build, not a 64-bit simulation.

### F11 — `ReleaseDC` occurs after the presentation commit and its failure is invisible (P2,
resolved by GDI-077)

`WindowDeviceContext` guarantees a scoped `GetDC`/`ReleaseDC` pair, but its destructor discards the
`ReleaseDC` result. `Present()` resets backbuffer damage and acknowledges the invalidation generation
before that destructor runs. Consequently the otherwise checked native transaction cannot report a
failed DC release, retain damage for a conservative retry, or expose the operation in telemetry.
The normal valid-HWND path is unlikely to fail, but the code and GDI-053 wording currently provide no
proof of the complete acquisition/release contract.

GDI-077 should add an explicit, checked close before the commit point, retain a destructor fallback
for exception safety, and use injection or a small DC-provider seam to verify failure telemetry and
damage retention without depending on a naturally failing Win32 desktop.

**Resolution:** `WindowDeviceContext` gained an explicit `Release(bool forceFailure = false)` that
performs the real (or, under test injection, simulated) `ReleaseDC` exactly once and nulls its
handle regardless of outcome, so the destructor becomes a no-op after it runs and is otherwise only
a non-throwing fallback for exception paths that never reached the explicit call. `Present()` now
calls `Release()` after `GdiFlush`/`DwmFlush` but before `ResetBackbufferDamage()` and the
invalidation-generation acknowledgement; a failed release surfaces through the same
`ThrowWin32Failure` diagnostic path, records `operation == "ReleaseDC"` in presentation telemetry,
and leaves damage/generation exactly as they were. A new `DebugForceNextReleaseDcFailure()` test
seam skips the real Win32 call and reports a simulated failure, matching the existing
`DebugForceNextDibBlitFailure()` convention. `cna_test_gdi_dc_release_transaction` proves telemetry
identification, damage/generation retention across repeated forced failures, and that a normal
`Present()` afterward still succeeds and commits.

### F12 — handoff evidence has drifted from the implemented build boundary (P2 documentation,
resolved by GDI-078)

The current CMake list names seven shared CPU-2D source files and the backend archive also contains
the three GDI translation units. `NEXT_gdi.md` still says the archive names "exactly two" CPU-2D
translation units, later calls it a "five-object" archive, and records `-j8` validation even though
this plan requires at most two parallel jobs. Other nearby paragraphs already describe the newer
split, so the handoff is internally contradictory rather than merely old.

GDI-078 must reconcile source/object/test counts with CMake-generated evidence, remove obsolete
parallelism claims, and record the 2026-08-03 clean `-j2` plus Xvfb/Wine rerun. Counts that are likely
to change again should be derived or asserted by a small build-boundary check instead of copied into
several prose sections.

**Resolution:** `NEXT_gdi.md`'s "exactly the two required CPU-2D translation units", "five-object
GDI archive", and three `-j8` claims are corrected in place to match the actual, already-current
seven/eight-file split and this plan's `-j2` ceiling. `cmake/BackendLibraries.cmake` now
`message(STATUS ...)`s the real `CNA_GDI_SOFTWARE_SOURCES` length (plus GDI's own three units) at
every configure, so this count is generated evidence rather than a number copied into prose that
can drift again. `docs/gdi-backend.md` and `NEXT_gdi.md` are updated to the current eight
shared/eleven-total file count and nineteen-case CTest registration (GDI-076's
`SoftwareTextureAllocation.cpp` and GDI-075/076/077's three new focused executables), and both
record the 2026-08-03 clean MinGW-w64 Release `-j2` reconfigure/build plus the full nineteen-case
Wine/Xvfb rerun with `CNA_GDI_DWM_FLUSH=0`.

**REMED-BUILD-017 adaptation correction:** registration was complete, but the manual native-MSVC
workflow and the native/Wine command inventory still stopped at the pre-follow-up fourteen-target
list. They now include all seventeen correctness executables, specifically adding the
presentation-mode transaction, DC-release transaction, and texture-allocation targets before the
nineteen-case CTest run. The adapted x64 MinGW build and serial Wine/Xvfb run subsequently built
all seventeen targets and passed all nineteen cases; the separate manual native-MSVC dispatch is
still pending.

---

## 5. Reclassification of the existing roadmap

The IDs remain stable because source comments and documentation refer to them. A check mark below
means only the narrowed statement in this table, not overall release readiness.

| IDs | Revised status | Audited interpretation |
|---|---:|---|
| GDI-001 | ✅ | Windows selection, factory, SDL3/`gdi32` linkage, and MinGW build integration work. |
| GDI-002 | ✅ | A real GDI display path exists, using `SetDIBitsToDevice` at 1:1 and `StretchDIBits` when scaled. |
| GDI-003 | ✅ | Historical feature-branch and current adapted-branch Release evidence each cover all seventeen GDI correctness executables/nineteen Wine runs, including all three configuration variants. Native MSVC and visible physical-Windows gates remain separate. |
| GDI-004 | 🟨 | Native visible demo/lifecycle inspection is still required and its checklist must be expanded. |
| GDI-005 | ✅ | CPU 2D raster/readback regression is substantial; it is not a GDI-output regression. |
| GDI-006 | 🟨 | Build/run and corrected stencil/dirty/test documentation exist; release and native-performance claims still require GDI-061/062. |
| GDI-010 | 🟨 | Hidden Wine raster numbers are useful; visible native presentation has no valid baseline. |
| GDI-011 | 🟨 | “No DIBSection” is provisional; a DIBSection could become authoritative storage rather than require a copy. |
| GDI-012–013 | ✅ | Oracle pixels verify 1:1 plus COLORONCOLOR/HALFTONE output, and native CTest registers the environment-driven variants separately. |
| GDI-014 | ✅ | Raster-derived damage, event-generation invalidation, checked commit, failure retention, and deterministic dirty-band pixels are covered. |
| GDI-015–016 | 🟨 | `GdiFlush` is checked and optional `DwmFlush` policy exists; native latency/pacing evidence is missing. |
| GDI-020–023 | ✅ | Blend, RT mips, fixed ColorMatrix, and SpriteBatch custom-effect rejection work in CPU tests. |
| GDI-024 | 🚫 | A general CPU shader interpreter remains outside the 2D compatibility scope. |
| GDI-025 | ✅ | The backbuffer has real 4x colour coverage/resolve; public count, partial resolves, sample-mask bits, wireframe limits, and per-pixel stencil interaction are tested and documented. |
| GDI-026 | ✅ | Public backbuffer and depthless-RT stencil use/clear now work without internal-backend calls. |
| GDI-027–028 | ✅ | No-depth and Anisotropic-to-Linear decisions are implemented for the current contract. |
| GDI-029 | ✅ | `StencilBuffer=true`, `DepthStencilBuffer=false`, and per-target attachment queries expose the standalone plane honestly. |
| GDI-030–034 | 🚫 | MRT, cube/volume resources, other surface formats, and occlusion remain excluded. |
| GDI-040–043 | 🚫 | General 3D expansion remains excluded. |

---

## 6. New corrective roadmap

### Phase G5 — correctness blockers

| # | Task | Status | Acceptance criteria |
|---|---|---:|---|
| GDI-050 | Split depth and stencil attachment/capability decisions. | ✅ | Add separate backend and render-target predicates for real depth and real stencil, with conservative defaults for existing backends. `GraphicsDevice::Clear` masks each flag independently. Add `GraphicsCapability::StencilBuffer` (or an equally explicit public query); GDI reports stencil=true and combined depth/stencil=false. Public `GraphicsDevice` tests clear/use stencil on both backbuffer and `RenderTarget2D`, including the single-colour `Clear(Color)` reset, without calling the internal backend. Document whether GDI's stencil-only plane is always present or opt-in; the public property and allocation policy must agree. |
| GDI-051 | Make dirty damage derive from the actual rasterized quad. | ✅ | Prefer a damage callback from the shared SpriteBatch raster path after origin, rotation, transform matrix, viewport origin, viewport clip, and scissor are known, avoiding a second geometry implementation in the GDI wrapper. A conservative full-frame fallback is acceptable for cases not proven exact. Use widened/saturating arithmetic before clipping. Tests cover non-zero origin, non-zero viewport, scissor, negative/offscreen/very large rectangles, unions, rotation, and transform. |
| GDI-052 | Add reliable expose/restore invalidation. | ✅ | Route the relevant SDL window events (at least exposed, restored, resized/pixel-size changed, and display-scale changed where emitted) to the registered backend, or adopt an equivalent correct Win32 paint design. Mark the GDI client fully dirty before a no-damage present can be skipped; do not rely solely on `GetUpdateRect`. Preserve the last valid logical size while minimized. A native test retains a frame, occludes/exposes and minimizes/restores without a new draw, and verifies repaint. |
| GDI-053 | Harden the Win32 presentation transaction. | ✅ | Use scoped HDC ownership. Treat documented zero `StretchDIBits` output as failure; define and test legal `SetDIBitsToDevice` returns under clipping and partial scan ranges. Check all correctness-relevant setup/draw calls, retain damage after any failed present, and include operation plus formatted Win32 error context where meaningful. A failed or zero-line blit must never call `ResetBackbufferDamage()`. |
| GDI-054 | Build a deterministic presentation oracle. | ✅ | Extract pure presentation geometry/planning and a small DIB-to-HDC function. Test all modes, odd sizes, clipping, forward/inverse coordinate boundaries, RGB channel order, top-down row order, 1:1, scaled output, letter/overscan bars, and a non-zero dirty subrectangle against a memory DC/DIBSection or another deterministic Win32 surface. Add test-only telemetry/injection for selected path, destination, damage, filter, and failures; do not infer them from CPU readback. |

### Phase G6 — public contract, automation, and lifecycle

| # | Task | Status | Acceptance criteria |
|---|---|---:|---|
| GDI-055 | Add high-level GDI API coverage. | ✅ | `gdi_public_api_test` drives `GraphicsDevice`, `PresentationParameters`, `Texture2D` upload/readback, `SpriteBatch`, `RenderTarget2D` binding/preservation/sampling, viewport/scissor, 4x and rejected 2x MSAA reset, backbuffer readback, resize, and the complete capability matrix. `gdi_public_stencil_test` covers stencil clear/use on the backbuffer and a depthless target. Low-level raster details remain in `gdi_2d_regression_test`; every advertised feature now has a public-path assertion. |
| GDI-056 | Register meaningful configuration variants. | ✅ | Native CTest has distinct default, `CNA_GDI_DIRTY_PRESENTATION=1`, and scaled `CNA_GDI_PRESENT_FILTER=halftone` cases. The variants assert NativeFull/None/Stretch and filter selection through GDI-054 telemetry, while the oracle verifies corresponding pixels. Every case explicitly disables `DwmFlush` so CI cannot block on a compositor. |
| GDI-057 | Add a manual native-Windows GDI workflow. | ✅ | `.github/workflows/gdi-windows-ci.yml` is a one-job, `workflow_dispatch`-only MSVC/Ninja gate. With REMED-BUILD-017 it builds only CNA plus all seventeen focused GDI correctness executables with `--parallel 2`, runs the nineteen-case `GDI` CTest label, and uploads CTest/CMake diagnostics on failure. The equivalent adapted MinGW/Wine 19-case matrix passes; the manual native-MSVC dispatch and separate visible GDI-061 lifecycle/DPI gate remain pending. |
| GDI-058 | Make applied presentation/resource state honest. | ✅ | Presentation-mode ordinals outside 0–4 throw before state mutation. Backbuffer format/depth normalize to `Color`/`None`, and both construction and reset expose only actual 0x/4x MSAA. `RenderTarget2D` rejects non-`Color`, reports actual `None` depth and 0x MSAA while retaining the separately advertised stencil plane, and preserves `PreserveContents`/`PlatformContents` while deterministically clearing `DiscardContents`. `gdi_applied_state_test` compares every property with color readback/mip/rebind storage; the public stencil test verifies the same three rebind policies for stencil. |
| GDI-059 | Normalize unsupported-feature failures. | ✅ | Every excluded GDI factory/backend entry throws `System::NotSupportedException`. Public construction rejects TextureCube, Texture3D, RenderTargetCube, ShaderEffect, occlusion queries, vertex buffers, and 16/32-bit index buffers (including dynamic wrappers) before a null or private-core Software resource can escape. The focused public test also covers depth state and indexed/non-indexed user draws, and confirms `SupportsCapability(ThreeD)==false` plus the resource-specific false answers. |
| GDI-060 | Audit DPI, fullscreen, resize, and input transforms. | ✅ | `SDL_GetWindowSizeInPixels()` is authoritative for backbuffer/presentation pixels; SDL input/warp coordinates are converted via `SDL_GetWindowSize()`. Pure tests cover 100/150/200% ratios and edge/bar rejection. A live SDL/Win32 test covers caller-owned windows and input-handle cleanup, all modes, three repeated odd resizes, drawable/client agreement, fullscreen round-trip, and exact logical/pixel retention across minimize/restore. Minimized state is explicitly non-drawable even when a platform reports a misleading cached pixel size. Physical multi-DPI observation remains GDI-061. |
| GDI-061 | Correct documentation and complete the visible gate. | 🟨 | `docs/gdi-backend.md` is synchronized through GDI-060 and GDI-067 and no longer claims release readiness or treats hidden-Wine timings as a release decision. The remaining human gate is to inspect Windows 10/11 animation, RGB/orientation, all presentation modes, both filters, dirty retained UI, resize, occlude/expose, minimize/restore, DPI move, fullscreen, and clean close; record OS, DPI, renderer settings, and result. This closes the original GDI-004/GDI-006 gate. |

### Phase G7 — measurement-led performance and memory

| # | Task | Status | Acceptance criteria |
|---|---|---:|---|
| GDI-062 | Replace the hidden-window presentation benchmark with native visible data. | ⬜ | Report p50/p95/p99 for CPU raster, MSAA resolve, GetDC/release, bar clear, DIB blit, `GdiFlush`, optional `DwmFlush`, and whole frame. Cover 800×600, 1280×720, 1920×1080; 1:1/scaled; full/dirty UI; 0x/4x MSAA. Record Windows version, CPU, DPI, presentation mode, visibility, and compositor state. Make optimization decisions only from this result. |
| GDI-063 | Stop clearing covered client pixels before a full blit. | ⬜ | Compute uncovered bars/regions and fill only those. Skip black fill when Stretch/Overscan fully covers the client. Preserve deterministic black after resize/mode changes and verify it with GDI-054 pixel tests before accepting any timing gain. |
| GDI-064 | Resolve only changed MSAA pixels. | ⬜ | Track unresolved sample damage. A no-damage dirty `Present()` performs no full-frame resolve; a partial update resolves only its safe clipped region; readback resolves the requested region or a documented conservative superset. Results remain byte-identical to a full resolve, including clear and overlapping blends. |
| GDI-065 | Add measured CPU SpriteBatch fast paths. | ⬜ | Profile first. Implement only high-value cases such as axis-aligned point-sampled opaque copy and common alpha spans, falling back to the generic triangle path for all other state. Differential randomized tests require byte identity; native benchmarks must show a material p95 frame improvement before retaining each path. |
| GDI-066 | Re-evaluate an authoritative DIBSection surface. | ⬜ | After GDI-062, compare the current vector+DIB calls with a persistent DIBSection whose mapped pixels are the rasterizer's authoritative colour storage, avoiding an obligatory extra copy. Record complexity, resize/readback/MSAA interaction, and measured benefit. Adopt only if native data beats the simpler path materially. This supersedes GDI-011's hidden-Wine conclusion. |
| GDI-067 | Make framebuffer allocation optional and overflow-safe. | ✅ | GDI backbuffers and targets allocate RGBA8 plus stencil but no float depth; only an applied 4x backbuffer adds sample colour. A pure planner validates positive dimensions, a 16,384-axis ceiling, every `size_t` multiply/add, complete mip storage, and a 512 MiB per-resource pixel-storage budget before allocation/`DWORD` conversion. Vector failures become `System::OutOfMemoryException`; resize/MSAA changes are transactional. Pure/live GDI tests verify exact 5/21-byte layouts and rejection, while a genuine i686 MinGW harness verifies 32-bit overflow and budget paths. |

### Phase G8 — maintainability

| # | Task | Status | Acceptance criteria |
|---|---|---:|---|
| GDI-070 | Replace inheritance from the whole Software 3D backend with an explicit CPU-2D component, or prove a guarded equivalent. | ✅ | GDI now derives directly from `IGraphicsBackend` and privately composes `GdiSoftware2DCore`; only reviewed 2D services are forwarded. A compile-time non-inheritance assertion plus a 42-check public/direct boundary test cover all resource factories, bindings, depth operations, and draw entries, so new Software virtuals cannot enter GDI implicitly. |
| GDI-071 | Make the shared-core build boundary explicit. | 🟨 | The GDI Software glob and intermediate archive are gone: one GDI archive uses the reviewed eight-file list (including GDI-076's texture-allocation planner), and future Software files cannot enter implicitly. The GNU/MinGW cycle is now only `CNA` ↔ GDI; the independently discovered `CNA` ↔ SOFTWARE edge is declared centrally and the per-test GNU linker group is gone. Historical and current focused MinGW GDI linking/nineteen-case matrices pass, and the current native SOFTWARE sanitizer harness plus standalone controls pass. The list includes allocation planners, independently built framebuffer/texture/RT resources, 2D state, SpriteBatch frontend, and the 2D-only raster wrapper (GDI-074); the manual native-MSVC workflow must still pass before this task becomes ✅. |
| GDI-072 | Capture typed GDI configuration once. | ✅ | `GdiConfiguration` captures filter/dirty/DWM once at construction; its pure strict parser aggregates sanitized invalid values into one warning and preserves safe defaults. Typed constructor-override and post-construction environment-mutation tests prove `Present()` uses only the immutable snapshot. |
| GDI-073 | Finish the advertised 4x MSAA semantics. | ✅ | The capability is explicitly narrowed and tested: filled backbuffer triangles have four 2x2-grid colour samples; mask bits 0–3 intersect geometric coverage and high bits are ignored; wireframe is a crisp full-enabled-sample DDA path without line AA; stencil is one value and one operation per covered pixel fragment that gates active colour samples. A 19-check pixel test covers every rule, and render targets remain single-sampled. |
| GDI-074 | Physically split the reusable Software 2D implementation. | 🟨 | GDI now independently builds `SoftwareFramebuffer.cpp`, `SoftwareTexture2D.cpp`, `SoftwareRenderTarget2D.cpp`, `SoftwareGraphicsBackend2DState.cpp`, and `SoftwareSpriteBatch.cpp`, then uses `SoftwareGraphicsBackend2D.cpp` for the shared triangle-raster bridge. Cube, vertex/index-buffer, generic-effect, and general-3D draw bodies are compiled out and retained only by the full SOFTWARE build. Archive/symbol inspection proves GDI has no cube implementation or its allocation warning; focused GDI MinGW/Wine plus native SOFTWARE build/smoke/rasterizer/SpriteBatch gates pass. The shared raster helpers and bridge remain in `SoftwareGraphicsBackend.cpp` through the wrapper, so extracting them and validating native MSVC are the remaining completion criteria. |

### Phase G9 — 2026-08-03 follow-up corrections

| # | Task | Status | Acceptance criteria |
|---|---|---:|---|
| GDI-075 | Make valid presentation-mode changes transactional. | ✅ | Validate the requested ordinal, calculate the prospective logical size, and complete any fallible framebuffer replacement before committing `presentationMode_`, damage, or coordinate policy; alternatively restore every prior field on failure. Add a deterministic test that starts from a valid retained framebuffer, switches to `FixedHeightDynamicWidth` with an aspect/virtual-height combination that exceeds the axis or byte budget, and verifies the exception family/diagnostic, old mode transforms, dimensions, pixels, damage, and a subsequent successful `Present()`. Cover both direct backend and public `GraphicsDevice::SetPresentationMode` paths. |
| GDI-076 | Give CPU `Texture2D` the same allocation discipline as GDI framebuffers. | 🟨 | Define a checked CPU-texture layout contract for positive dimensions, base RGBA8 bytes, declared/supplied mip levels, every `size_t` operation, and a documented per-resource byte budget. Enforce it before public constructors allocate their first large vector and again in `GdiGraphicsBackend::CreateTexture`/the shared CPU texture boundary so decoded and direct `ImageData` cannot bypass it. Make `GetMaxTextureDimension()` and the byte budget jointly honest, validate source pixel lengths before sampling, eliminate avoidable full-size transient copies where practical, and translate `bad_alloc`/`length_error` to `System::OutOfMemoryException`. Add public/direct, malformed-data, over-budget, mip-budget, and genuine 32-bit planner tests; rejected creation must not alter existing resources. The GDI/Software CPU-texture boundary itself is fully checked, including a genuine i686-w64-mingw32 32-bit `size_t` harness (`tools/graphics/texture_allocation_32bit_check.cpp`, mirroring GDI-067's framebuffer one); the shared `Texture2D.cpp` public constructors' own first `width*height*4` allocation remains open, deliberately cross-backend, scope before this task becomes ✅. |
| GDI-077 | Include DC release in the checked presentation transaction. | ✅ | Refactor scoped DC ownership to support an explicit checked close before `ResetBackbufferDamage()` and invalidation acknowledgement, with a non-throwing destructor fallback for exceptional paths. A zero `ReleaseDC` result must identify `ReleaseDC` in telemetry/diagnostics and conservatively retain pending damage/generation. Add deterministic failure injection or a DC-provider seam plus success/failure count assertions proving exactly one release attempt and no leak/double release. Do not make a destructor throw. |
| GDI-078 | Reconcile handoff/build evidence with the actual archive boundary. | ✅ | `NEXT_gdi.md`, `docs/gdi-backend.md`, and this plan follow the CMake source of truth: eight reviewed shared CPU-2D sources plus three GDI-owned units, seventeen correctness executables, nineteen registered cases, and at most `-j2`. Obsolete "two translation units", "five-object archive", and `-j8` claims are removed; configure reports the archive count. REMED-BUILD-017 separately corrected the workflow/manual target inventory that the original reconciliation missed. |

### Phase G10 — 2026-08-08 integration-adaptation corrections

| # | Task | Status | Acceptance criteria |
|---|---|---:|---|
| REMED-GFX-229 | Reject undersized positive Software texture upload pitch. | ✅ | `stride >= width * 4` is validated before mutation for a positive stride, while non-positive values retain the tight-row default. Current GDI/Software coverage passed odd-width padded asymmetric RGBA rows, exact readback, stride-11 rejection for a 12-byte row, and retained prior pixels. |
| REMED-GFX-230 | Honor Software render-target upload pitch. | ✅ | `SoftwareRenderTargetBackend::UpdatePixels` copies each valid row's RGBA bytes using the supplied pitch and rejects a positive short pitch before color/MSAA/mip mutation. Current 3×2 odd-width/asymmetric/padded/readback/rejection coverage passed. |
| REMED-GFX-231 | Restore `SourceAlphaSaturation` destination-alpha semantics. | ✅ | The shared CPU factor uses `min(sourceAlpha, 1-destinationAlpha)` for RGB and one for alpha. Current asymmetric source-channel and distinct source/destination-alpha coverage passed in the GDI 2D regression and Software blend controls. |
| REMED-GFX-232 | Align DX3's standalone stencil hook with its capability answer. | ✅ | DX3's real depth-only/no-stencil production path now reports `SupportsStencilBuffer()==false`, consistent with `SupportsCapability(StencilBuffer)`. The x64 MinGW DX3 build and focused capability runtime passed 1/1 through Wine/Xvfb with DirectDraw engagement. |
| REMED-GFX-233 | Preserve legacy empty-declaration single-buffer submission. | ✅ | `VertexBuffer(device, count)` intentionally exposes an empty declaration while typed `SetData` gives its backend a real packed stride. REMED-GFX-201's zero-stride snapshot made Software reuse record zero; the exact ordinary one-buffer legacy shape now retains the named-`vb`/backend-stride fallback while declared, multistream, and instanced paths remain authoritative. The mechanism pre-existed at integration base `677f4c59`. Current Software effects 7/7, Additive 29/29, and scissor 44/44 controls close the defect. |
| REMED-BUILD-017 | Reconcile native GDI build target inventory. | ✅ | The workflow/docs name all seventeen correctness executables, including the three formerly omitted GDI-075/076/077 targets. The current MinGW build produced all seventeen and CTest registered and passed all nineteen Wine/Xvfb cases. Manual native MSVC remains an external workflow gate, not an inventory defect. |
| REMED-BUILD-018 | Include the complete graphics-backend type in the capability test. | ✅ | `GraphicsDeviceCapabilityTests.cpp` directly includes `IGraphicsBackend.hpp` before calling `GetBackend().SupportsStencilBuffer()`. The current native ASan/UBSan focused harness compiled and ran 151 selected tests (149 pass, 2 intentional skips) with no CNA sanitizer report, closing the incomplete-type build failure. |

Test-only adaptation hardening extends the existing GDI-054 presentation oracle without adding an
executable or registered case: its memory surface restores the prior selected object and deletes
the bitmap/DC on normal destruction and every injected post-allocation failure path, repeats the
cycle 64 times, and asserts `GetGuiResources` return-to-baseline only when a stable live DC/DIB
delta proves that API reliable in the current Windows/Wine environment. Repeated normal/failure
cleanup and authoritative operation counters passed; the `GetGuiResources` subcheck skipped under
Wine because no stable live delta was available, so physical-Windows leak absence is not claimed.

REMED-GFX-224 remains open exactly as recorded in `plan_skia.md`: it concerns EasyGL render-target
`SetData`, not GDI's private Software-2D upload path, and this adaptation does not alter its status.

---

## 7. Recommended execution order

1. ✅ **Repair the public contract:** GDI-050.
2. ✅ **Make retained presentation correct:** GDI-051, GDI-052, and GDI-053.
3. ✅ **Create the deterministic oracle:** GDI-054.
4. ✅ **Finish public coverage:** GDI-055.
5. ✅ **Register configuration coverage:** GDI-056.
6. **Establish native repeatability:** ✅ GDI-057 through GDI-060; GDI-061 remains a visible
   native-Windows gate.
7. **Close follow-up correctness gaps:** ✅ GDI-075 and GDI-077 are fully closed; GDI-076 is closed
   at the GDI/Software CPU-texture boundary but leaves the shared `Texture2D.cpp` allocation-order
   improvement as deliberate cross-backend scope (🟨). These are implementation tasks, not
   substitutes for the visible native gate.
8. **Reconcile evidence:** ✅ GDI-078; keep generated archive/test facts separate from human
   lifecycle observations.
9. **Measure a visible client:** GDI-062. Only then choose GDI-063 through GDI-066.
10. **Reduce remaining architectural risk:** ✅ GDI-067, GDI-070, GDI-072, and GDI-073; GDI-071 is
   locally implemented and awaits native MSVC, while only GDI-074 remains in this group.

The GDI-014 and GDI-026 prerequisites are now satisfied by GDI-050 through GDI-054 and their
focused automated tests. Dirty presentation remains opt-in until the visible native lifecycle gate
in GDI-061 confirms the retained-client behavior outside focused automated lifecycle coverage.

---

## 8. Release definition of done

The GDI backend may be called a release baseline only when:

- every G5 task and GDI-075 through GDI-078 are complete, and no public capability depends on
  direct internal-backend calls;
- focused MinGW `-j2` build plus Wine smoke/regression and native MSVC `ctest -L GDI` pass;
- deterministic presentation tests validate the pixels sent through both DIB paths and all
  registered environment variants;
- the native visible GDI-061 lifecycle matrix passes on at least Windows 10 or 11, with a second
  DPI setting tested;
- public presentation/resource properties report applied reality;
- native visible benchmark data, not a hidden Wine DC, supports all performance claims; and
- `NEXT_gdi.md`, `docs/gdi-backend.md`, and this status table match the implementation and generated
  build-boundary evidence at the same commit.

---

## 9. External API references used by this audit

- Microsoft: [`StretchDIBits` return value and top-down DIB behavior](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-stretchdibits)
- Microsoft: [`SetDIBitsToDevice` scan-range contract](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-setdibitstodevice)
- Microsoft: [`ReleaseDC` return contract](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-releasedc)
- Microsoft: [`BeginPaint` validates the update region](https://learn.microsoft.com/en-us/windows/win32/gdi/retrieving-the-update-region)
- SDL3: [window coordinates, pixel size, density, and pixel-size/display-scale events](https://wiki.libsdl.org/SDL3/README-highdpi)
