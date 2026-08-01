# Win32 GDI Graphics Backend — Audit and Remediation Plan

> **Audit snapshot:** 2026-08-01, commit `56bd8961` (`feature/gdi`).
>
> **Assessment at the audited commit:** the backend was a sound Windows-only CPU-2D compatibility
> prototype, but **not a release baseline**. The audit found three correctness blockers: the public
> stencil-clear path was disconnected, dirty damage could omit pixels, and Win32 expose/paint repair
> was not reliable. Its tests predominantly inspected the CPU framebuffer and did not prove what GDI
> put in the window.
>
> **Implementation update (2026-08-01 working tree):** GDI-050 through GDI-058 are implemented.
> The focused MinGW Release build, all ten GDI correctness executables, and all three configuration
> variants pass under Wine. The suite includes a real memory-DC/DIBSection pixel oracle, complete
> public-path coverage for advertised features, exact dirty-damage coverage,
> event/failure-retention integration, and distinct default/dirty/halftone cases. The first manual
> native-MSVC workflow result and the visible Windows lifecycle/DPI gate remain open, so the backend
> is still not a release baseline.
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

All build and test commands for this plan use at most two parallel jobs (`-j2` or
`CMAKE_BUILD_PARALLEL_LEVEL=2`).

---

## 2. How the backend is implemented after Phase G5

```text
GraphicsDevice / SpriteBatch public API
                 |
                 v
GdiGraphicsBackend
  - HWND acquisition and presentation policy
  - window/logical coordinate transforms
  - SDL window-event invalidation generation
  - raster-derived damage and 2D-only capability boundary
                 |
                 v
SoftwareGraphicsBackend + SoftwareFramebuffer
  - CPU texture/SpriteBatch/render-target rasterization
  - RGBA8 + float depth + 8-bit stencil + optional 4x colour samples
                 |
                 v
ResolveColor -> GdiPresentation planner -> scoped GetDC
             -> SetDIBitsToDevice (1:1 full/dirty)
                or StretchDIBits (scaled) -> GdiFlush -> optional DwmFlush
```

### Build integration

- [`cmake/BackendSelection.cmake`](cmake/BackendSelection.cmake) hard-gates `GDI` to a Windows
  target and selects `cna_backend_graphics_gdi`.
- [`cmake/BackendLibraries.cmake`](cmake/BackendLibraries.cmake) links SDL3, `gdi32`, and a
  separately archived Software core. The core currently globs every Software `.cpp` file.
- [`cmake/CnaLibrary.cmake`](cmake/CnaLibrary.cmake) declares the resulting static-library cycle
  needed by GNU/MinGW archive scanning.

### Raster and resource path

- [`GdiGraphicsBackend`](include/CNA/Internal/Backends/Gdi/GdiGraphicsBackend.hpp) derives from the
  full Software backend. It delegates 2D work and overrides the main 3D entry points to reject them.
- [`SoftwareFramebuffer`](include/CNA/Internal/Backends/Software/SoftwareGraphicsBackend.hpp) owns
  the resolved RGBA8 image, a float depth plane, an 8-bit stencil plane, and an optional four-sample
  RGBA plane. GDI pays for the depth allocation even though it forcibly disables depth.
- GDI render targets deliberately force depth and MSAA to zero and explicitly expose their
  always-present standalone Software stencil allocation. `GraphicsDevice` now asks depth and
  stencil attachment questions independently.

### Presentation path

- `Present()` synchronizes the logical backbuffer, resolves MSAA, reads the Win32 client size, and
  delegates Native/Stretch/Letterbox/Overscan/FixedHeightDynamicWidth geometry and path selection
  to a deterministic presentation planner.
- A 1:1 result uses `SetDIBitsToDevice`; a scaled result uses `StretchDIBits`. The source is a
  top-down 32-bit `BI_BITFIELDS` DIB whose masks match CNA's in-memory RGBA byte order.
- The shared SpriteBatch rasterizer reports clipped candidate-pixel bounds after origin, rotation,
  transform, viewport, and scissor. An SDL event watch independently advances a native-client
  invalidation generation for expose/restore/resize/display lifecycle events.
- HDC ownership and DIB submission form a checked transaction. Damage and the captured native
  invalidation generation are acknowledged only after the complete blit and `GdiFlush` succeed.
- Full presentation currently fills the entire client black before every blit, including Stretch
  and Overscan where the following blit already covers the client.
- Process environment options select `halftone` scaling, dirty presentation, and optional
  `DwmFlush`; they are re-read during presentation rather than captured as backend configuration.

### Existing tests

[`cmake/Tests/GdiTests.cmake`](cmake/Tests/GdiTests.cmake) builds smoke, CPU 2D regression,
ColorMatrix integration, public-stencil, complete public-API, applied-state, dirty-damage,
repaint/failure, presentation-oracle, and presentation-configuration and benchmark executables. It
registers twelve cases (nine original/focused cases plus three environment configurations) as
CTests only for a native Windows build. The manual `gdi-windows-ci.yml` workflow now configures the
backend with MSVC, builds only CNA plus those focused tests, and runs the `GDI` CTest label.

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
  still inherits its build risk.

These passes prove CPU raster/readback behavior and that GDI calls do not fail in the tested hidden
Wine window. They do **not** prove channel order, orientation, scaling, clipping, damage, black bars,
or repaint behavior in a visible native Windows client.

### Current Phase G5/G6 validation

The working tree builds all ten correctness executables together with MinGW-w64/Ninja at `-j2`.
Under Wine, smoke, 2D regression, ColorMatrix, public stencil, the complete public API matrix, dirty
damage, repaint/failure and the memory-DC presentation oracle all pass; the
presentation-configuration executable also passes in its default, dirty and halftone environments
with `CNA_GDI_DWM_FLUSH=0`. Unlike the audited baseline, the memory-DC test proves the exact
GDI-produced pixels on a deterministic selected bitmap. It still does not replace the visible
native Windows lifecycle/DPI gate.

---

## 4. Audit findings

### F1 — public stencil clears are dropped (P0, resolved by GDI-050)

GDI reports `SupportsDepthStencil() == false`, which is correct for depth. However,
[`GraphicsDevice::Clear`](src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp) uses that one
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

[`GdiSpriteBatchBackend::MarkDraw`](src/CNA/Internal/Backends/Gdi/GdiGraphicsBackend.cpp) records
the raw destination rectangle when rotation is zero and the batch transform is identity. The shared
rasterizer subsequently:

- moves the quad by a non-zero `origin`; and
- adds `Viewport.X/Y` after transforming viewport-local sprite coordinates.

Those effects are visible in the actual quad construction in
[`SoftwareGraphicsBackend.cpp`](src/CNA/Internal/Backends/Software/SoftwareGraphicsBackend.cpp),
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
handler](third_party/SDL/src/video/windows/SDL_windowsevents.c) explicitly calls `ValidateRect()`
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
vendored SDL build, builds CNA plus the ten focused executables at two-way parallelism, runs all
twelve native `GDI` CTest cases, and uploads CTest/CMake diagnostics on failure. It remains a
compiler/hidden-window gate and explicitly does not close the visible GDI-061 gate.

### F7 — public configuration and rejection semantics are not consistently honest (P1,
configuration resolved by GDI-058)

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
against storage after rebind. The remaining inconsistent unsupported-feature exceptions are
isolated to GDI-059.

### F8 — lifecycle, cost, and architecture claims are ahead of evidence (P1/P2)

- `ResolveColor()` walks the complete framebuffer at the start of every MSAA `Present()`, even when
  dirty mode has no damage or only a small rectangle.
- The hidden-Wine benchmark does not measure a visible compositor path; it cannot close the
  DIBSection, flush, dirty-blit, or pacing decisions.
- Four-sample colour is real for solid triangle coverage, but wireframe writes all samples and
  depth/stencil remain per pixel. This limited MSAA contract is not fully documented or tested.
- GDI allocates unused float depth for every framebuffer. At 4x it retains roughly 25 bytes/pixel
  across resolved colour, depth, stencil, and sample colour before container overhead.
- Inheriting the entire Software 3D backend and globbing its whole source makes the 2D boundary
  fragile: a newly added virtual path can become reachable unless GDI remembers to override it.

---

## 5. Reclassification of the existing roadmap

The IDs remain stable because source comments and documentation refer to them. A check mark below
means only the narrowed statement in this table, not overall release readiness.

| IDs | Revised status | Audited interpretation |
|---|---:|---|
| GDI-001 | ✅ | Windows selection, factory, SDL3/`gdi32` linkage, and MinGW build integration work. |
| GDI-002 | ✅ | A real GDI display path exists, using `SetDIBitsToDevice` at 1:1 and `StretchDIBits` when scaled. |
| GDI-003 | ✅ | Focused Release cross-build passed; all ten current GDI correctness executables and three configuration variants pass under Wine. |
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
| GDI-025 | 🟨 | Backbuffer 4x colour coverage/resolve exists; public count, partial resolve, and sample semantics remain. |
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
| GDI-057 | Add a manual native-Windows GDI workflow. | ✅ | `.github/workflows/gdi-windows-ci.yml` is a one-job, `workflow_dispatch`-only MSVC/Ninja gate. It builds only CNA plus the ten focused GDI executables with `--parallel 2`, runs `ctest -L GDI --output-on-failure`, and uploads CTest/CMake diagnostics on failure. Its header and documentation explicitly retain GDI-061 as the separate visible lifecycle/DPI gate. |
| GDI-058 | Make applied presentation/resource state honest. | ✅ | Presentation-mode ordinals outside 0–4 throw before state mutation. Backbuffer format/depth normalize to `Color`/`None`, and both construction and reset expose only actual 0x/4x MSAA. `RenderTarget2D` rejects non-`Color`, reports actual `None` depth and 0x MSAA while retaining the separately advertised stencil plane, and preserves `PreserveContents`/`PlatformContents` while deterministically clearing `DiscardContents`. `gdi_applied_state_test` compares every property with color readback/mip/rebind storage; the public stencil test verifies the same three rebind policies for stencil. |
| GDI-059 | Normalize unsupported-feature failures. | ⬜ | Choose one documented early-failure policy and exception family for GDI's TextureCube, Texture3D, RenderTargetCube, ShaderEffect, occlusion, buffers, and 3D draw APIs. Construction must not appear usable only to fail unpredictably later. Add one public test per excluded feature and retain `SupportsCapability(ThreeD)==false`. |
| GDI-060 | Audit DPI, fullscreen, resize, and input transforms. | ⬜ | Establish whether Win32 client pixels or `SDL_GetWindowSizeInPixels()` is the single source of truth and make presentation/input use it consistently. Test 100%, 150%, and 200% Windows scale where available, external SDL windows, every presentation mode, repeated odd-size resize, fullscreen round-trip, and coordinate round-trips at edges/bars. No zero-size minimize transition may reallocate or erase a retained backbuffer. |
| GDI-061 | Correct documentation and complete the visible gate. | ⬜ | Update `docs/gdi-backend.md` after GDI-050–060: remove the release claim and hidden-Wine performance inference, describe actual stencil/MSAA/resource semantics and test variants, and state unsupported exceptions exactly. On Windows 10/11, inspect animation, RGB/orientation, all presentation modes, both filters, dirty retained UI, resize, occlude/expose, minimize/restore, DPI move, fullscreen, and clean close; record OS, DPI, renderer settings, and result. This closes the original GDI-004/GDI-006 gate. |

### Phase G7 — measurement-led performance and memory

| # | Task | Status | Acceptance criteria |
|---|---|---:|---|
| GDI-062 | Replace the hidden-window presentation benchmark with native visible data. | ⬜ | Report p50/p95/p99 for CPU raster, MSAA resolve, GetDC/release, bar clear, DIB blit, `GdiFlush`, optional `DwmFlush`, and whole frame. Cover 800×600, 1280×720, 1920×1080; 1:1/scaled; full/dirty UI; 0x/4x MSAA. Record Windows version, CPU, DPI, presentation mode, visibility, and compositor state. Make optimization decisions only from this result. |
| GDI-063 | Stop clearing covered client pixels before a full blit. | ⬜ | Compute uncovered bars/regions and fill only those. Skip black fill when Stretch/Overscan fully covers the client. Preserve deterministic black after resize/mode changes and verify it with GDI-054 pixel tests before accepting any timing gain. |
| GDI-064 | Resolve only changed MSAA pixels. | ⬜ | Track unresolved sample damage. A no-damage dirty `Present()` performs no full-frame resolve; a partial update resolves only its safe clipped region; readback resolves the requested region or a documented conservative superset. Results remain byte-identical to a full resolve, including clear and overlapping blends. |
| GDI-065 | Add measured CPU SpriteBatch fast paths. | ⬜ | Profile first. Implement only high-value cases such as axis-aligned point-sampled opaque copy and common alpha spans, falling back to the generic triangle path for all other state. Differential randomized tests require byte identity; native benchmarks must show a material p95 frame improvement before retaining each path. |
| GDI-066 | Re-evaluate an authoritative DIBSection surface. | ⬜ | After GDI-062, compare the current vector+DIB calls with a persistent DIBSection whose mapped pixels are the rasterizer's authoritative colour storage, avoiding an obligatory extra copy. Record complexity, resize/readback/MSAA interaction, and measured benefit. Adopt only if native data beats the simpler path materially. This supersedes GDI-011's hidden-Wine conclusion. |
| GDI-067 | Make framebuffer allocation optional and overflow-safe. | ⬜ | Do not allocate float depth for a no-depth GDI surface; allocate stencil/MSAA only according to the settled contract. Validate positive dimensions and every multiplication before allocation/`DWORD` conversion, impose a documented practical dimension/byte budget, translate allocation failure clearly, and cover 32-bit MinGW arithmetic. Report real bytes/pixel in docs. |

### Phase G8 — maintainability

| # | Task | Status | Acceptance criteria |
|---|---|---:|---|
| GDI-070 | Replace inheritance from the whole Software 3D backend with an explicit CPU-2D component, or prove a guarded equivalent. | ⬜ | Preferred design is composition around reusable framebuffer/texture/SpriteBatch/RT services. If inheritance remains, add a complete contract test over every virtual 3D/resource entry and a review guard so new methods default to unsupported on GDI. No shared Software behavior may silently broaden GDI. |
| GDI-071 | Make the shared-core build boundary explicit. | ⬜ | Replace the GDI Software source glob with an explicit 2D-core target/source list, remove unrelated 3D/cube compilation where practical, and simplify the static archive cycle without reintroducing MinGW link failures. Build GDI and SOFTWARE independently with GCC/MinGW and MSVC. |
| GDI-072 | Capture typed GDI configuration once. | ⬜ | Parse filter/dirty/DWM settings at backend construction into a validated config object with test overrides. Unknown values produce one clear diagnostic. `Present()` does not repeatedly consult mutable process environment state. |
| GDI-073 | Finish the advertised 4x MSAA semantics. | ⬜ | Decide and test wireframe coverage, `MultiSampleMask`, and stencil interaction. Either implement sample-correct behavior for every advertised 2D path or explicitly narrow the capability/documentation so users cannot infer per-sample depth/stencil or anti-aliased wire edges. Keep RT MSAA false unless real sample storage is added. |

---

## 7. Recommended execution order

1. ✅ **Repair the public contract:** GDI-050.
2. ✅ **Make retained presentation correct:** GDI-051, GDI-052, and GDI-053.
3. ✅ **Create the deterministic oracle:** GDI-054.
4. ✅ **Finish public coverage:** GDI-055.
5. ✅ **Register configuration coverage:** GDI-056.
6. **Establish native repeatability:** ✅ GDI-057 and GDI-058; next GDI-059 through GDI-061.
7. **Measure a visible client:** GDI-062. Only then choose GDI-063 through GDI-066.
8. **Reduce memory and architectural risk:** GDI-067 and GDI-070 through GDI-073.

The GDI-014 and GDI-026 prerequisites are now satisfied by GDI-050 through GDI-054 and their
focused automated tests. Dirty presentation remains opt-in until the visible native lifecycle gate
in GDI-061 confirms the retained-client behavior outside focused automated lifecycle coverage.

---

## 8. Release definition of done

The GDI backend may be called a release baseline only when:

- every G5 task is complete and no public capability depends on direct internal-backend calls;
- focused MinGW `-j2` build plus Wine smoke/regression and native MSVC `ctest -L GDI` pass;
- deterministic presentation tests validate the pixels sent through both DIB paths and all
  registered environment variants;
- the native visible GDI-061 lifecycle matrix passes on at least Windows 10 or 11, with a second
  DPI setting tested;
- public presentation/resource properties report applied reality;
- native visible benchmark data, not a hidden Wine DC, supports all performance claims; and
- `docs/gdi-backend.md` and this status table match the implementation at the same commit.

---

## 9. External API references used by this audit

- Microsoft: [`StretchDIBits` return value and top-down DIB behavior](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-stretchdibits)
- Microsoft: [`SetDIBitsToDevice` scan-range contract](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-setdibitstodevice)
- Microsoft: [`BeginPaint` validates the update region](https://learn.microsoft.com/en-us/windows/win32/gdi/retrieving-the-update-region)
- SDL3: [window coordinates, pixel size, density, and pixel-size/display-scale events](https://wiki.libsdl.org/SDL3/README-highdpi)
