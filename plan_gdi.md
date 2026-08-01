# Win32 GDI Graphics Backend — Plan

> **Current state: baseline implemented, not yet demonstrated on a real display.**
> `CNA_GRAPHICS_BACKEND=GDI` selects a Windows-only backend that uses the shared CPU
> rasterizer for 2D content and displays its RGBA8 backbuffer through Win32 GDI
> `StretchDIBits`.  The source code and a smoke-test target exist, but the full CMake build and
> the visible `cna_demo_2d` run are pending because this checkout's SDL submodules were not
> initialized when the first build was attempted.
>
> **Status legend:** ✅ implemented; 🟨 code exists but the stated end-to-end verification is
> still missing; ⬜ not started; ⏸ blocked by an external prerequisite; 🚫 intentionally outside
> the current 2D-only scope.  No ⬜ task in this plan has been started.

---

## Contract and boundaries

GDI is intended as a Windows compatibility and modest-workload 2D backend, not a replacement for
EasyGL.  It must keep using a real Win32 GDI presentation path; adding an SDL renderer, an OpenGL
context, or a Direct3D device would be a different backend rather than an improvement to this one.

The current usable 2D slice already includes RGBA `Texture2D`, CPU `SpriteBatch` (source
rectangles, transforms, rotation, flips and basic alpha compositing), one colour `RenderTarget2D`,
readback, viewport/scissor state, and all CNA presentation modes.  It deliberately rejects 3D
entry points, depth/stencil, MSAA, cube/volume textures, occlusion queries and custom effects.

All builds and test commands for this plan must use at most two parallel jobs (`-j2` or
`CMAKE_BUILD_PARALLEL_LEVEL=2`).

---

## Phase G0 — establish the real baseline

| # | Task | Status | Acceptance criteria |
|---|---|---|---|
| GDI-001 | Keep `GDI` selectable through `CNA_GRAPHICS_BACKEND`, Windows-gated, and linked with `gdi32`; retain one unambiguous GDI factory. | ✅ | Source integration exists in `cmake/BackendSelection.cmake`, `cmake/BackendLibraries.cmake` and `GdiGraphicsBackend.cpp`. |
| GDI-002 | Keep the CPU 2D framebuffer and the real `StretchDIBits` presentation path. | 🟨 | Code exists in `GdiGraphicsBackend`; prove it by a built executable presenting into an actual `HWND`. |
| GDI-003 | Build the GDI smoke target and run its hidden-window checks. | ⏸ | Initialize the required vendored SDL submodules, configure a MinGW GDI build, build with at most two jobs, then run `cna_test_gdi_smoke.exe` under a display-capable Wine setup or natively on Windows. |
| GDI-004 | Build and launch `cna_demo_2d` with `CNA_GRAPHICS_BACKEND=GDI` on a real display for manual inspection. | ⏸ | The visible window shows animated sprites, reacts to resize, and closes normally.  This is the first manual visual gate; it must not use `--smoke` if a human is inspecting it. |
| GDI-005 | Add deterministic 2D regression coverage for GDI: texture upload, source rectangle, tint/alpha, rotation, flip, sampler address modes, render-target sampling, resize and presentation transforms. | ⬜ | A GDI-labelled test executable covers each listed operation and checks pixels/readback where possible; a separate visible run covers the final GDI display step. |
| GDI-006 | Record a reproducible Windows-native and MinGW+Wine test procedure in `docs/gdi-backend.md`. | ⬜ | Documentation names exact configure/build/run commands, display prerequisite, expected result and the two-job limit. |

---

## Phase G1 — presentation performance and image quality

These tasks target the main difference from EasyGL: GDI currently rasterizes on the CPU and submits
the whole backbuffer to GDI each frame.  They must be driven by measurements, not assumptions.

| # | Task | Status | Acceptance criteria |
|---|---|---|---|
| GDI-010 | Measure a baseline: CPU raster time, `Present()` time, frame time and memory at representative resolutions and sprite counts. | ⬜ | A repeatable benchmark records 800×600 and at least one higher-resolution case; results distinguish rasterization time from GDI presentation time. |
| GDI-011 | Investigate a persistent `DIBSection`/memory-DC presentation surface instead of recreating bitmap metadata and passing the CPU vector through `StretchDIBits` every frame. | ⬜ | Adopt it only if profiling shows a material benefit, preserve exact RGBA channel order/top-down orientation, and retain correct resize/lifetime handling. |
| GDI-012 | Add a 1:1 presentation fast path (`SetDIBitsToDevice` or an equivalent DIB blit) so native-size output does not go through scaling. | ⬜ | NativeBackBuffer output is pixel-identical and measurably no slower than the current generic stretch path. |
| GDI-013 | Make final-window scaling intentional: preserve nearest-neighbour/pixel-art scaling by default and evaluate an explicit higher-quality scaling option for non-pixel-art applications. | ⬜ | Both modes are documented, testable and do not alter source-texture sampler semantics. |
| GDI-014 | Add optional dirty-rectangle presentation for UI-like workloads, with a safe full-frame fallback for clears, scrolling, scaling, rotation or unknown damage. | ⬜ | Partial updates never leave stale pixels; benchmarks show a benefit for a small changed region and no regression for full-frame animation. |
| GDI-015 | Profile whether unconditional `GdiFlush()` is necessary.  Avoid it only when presentation ordering and window teardown remain reliable. | ⬜ | A documented decision is based on timing and stress testing; no lost or reordered visible frames. |
| GDI-016 | Evaluate best-effort frame pacing through DWM (`DwmFlush`) as an opt-in policy. | ⬜ | It is explicitly documented as pacing, not a true swap interval; unsupported systems fall back safely and default behaviour stays non-blocking. |

---

## Phase G2 — higher-fidelity 2D rendering

These are features EasyGL can express through GPU state.  The GDI version would remain CPU-based,
mostly by extending the shared Software rasterizer; therefore every task must also check whether it
changes the `SOFTWARE` backend and run its relevant regression tests.

| # | Task | Status | Acceptance criteria |
|---|---|---|---|
| GDI-020 | Implement exact XNA blend factors and blend equations instead of the current `Opaque` versus simplified alpha-blend choice. | ⬜ | Pixel tests cover at least Opaque, AlphaBlend, NonPremultiplied and Additive plus independently discriminating blend-factor/equation cases; channel-write masks remain correct. |
| GDI-021 | Generate and retain mip levels for a `RenderTarget2D` created with `mipMap=true`, after rendering is complete. | ⬜ | Every generated level has correct dimensions and downsampled pixels, can be sampled/read back, and regeneration respects target unbind/pass boundaries. |
| GDI-022 | Define a small, explicit set of fixed CPU 2D effects (for example colour matrix, greyscale or a simple blur). | ⬜ | Each effect has defined parameters, pixel tests and a documented cost.  This is not a claim of general shader support. |
| GDI-023 | Decide whether custom `Effect` support should remain an explicit exception or use a restricted CPU-effect API. | ⬜ | The backend never silently accepts a custom shader and ignores it.  Any accepted effect has real, documented rendering semantics. |
| GDI-024 | Investigate a general CPU shader interpreter for arbitrary custom GLSL/HLSL-like effects. | 🚫 | Outside the intended pure-GDI/2D scope: it is only reconsidered after a concrete compatibility requirement and a performance budget are approved. |
| GDI-025 | Add optional CPU multi-sample anti-aliasing/supersampling. | ⬜ | Sample count is honestly reported, resolves are pixel-tested, and benchmarks show the memory/CPU cost at target resolutions.  It must remain opt-in. |
| GDI-026 | Add a CPU stencil buffer for 2D clipping/masking. | ⬜ | Stencil clear, comparison, operations and masked sprite rendering are tested; capability reporting changes only when a real buffer exists. |
| GDI-027 | Decide separately whether a real depth buffer is useful for 2D layering. | ⬜ | Either implement depth clear/test/write with dedicated pixel tests and honest `DepthStencilBuffer` reporting, or formally keep it unsupported. |
| GDI-028 | Implement anisotropic texture filtering in the CPU sampler, or continue to map it to linear filtering. | ⬜ | Make a measured decision; if implemented, tests demonstrate a different, correct minified/rotated result and document its CPU cost. |
| GDI-029 | Audit and update `SupportsCapability()` as each optional feature becomes real. | ⬜ | Every `true` result is backed by an executable test; unsupported features stay `false` and fail explicitly rather than silently degrading. |

---

## Phase G3 — resources beyond the current one-colour-target 2D slice

| # | Task | Status | Acceptance criteria |
|---|---|---|---|
| GDI-030 | Evaluate multiple simultaneous render targets (MRT). | 🚫 | Outside normal SpriteBatch/GDI use.  It requires a multi-output CPU pixel pipeline, not just several GDI surfaces; reconsider only for a concrete consumer. |
| GDI-031 | Evaluate `TextureCube` and `RenderTargetCube`. | 🚫 | These are 3D/environment-map resources and are not part of the GDI 2D contract. |
| GDI-032 | Evaluate `Texture3D`. | 🚫 | Volume textures have no 2D GDI consumer; retain the explicit unsupported result. |
| GDI-033 | Add non-`Color`/non-RGBA8 `SurfaceFormat` storage. | 🚫 | Not a current EasyGL advantage either: EasyGL is also restricted to `SurfaceFormat::Color`.  Reconsider only with a shared public format/readback design. |
| GDI-034 | Evaluate hardware-style occlusion queries. | 🚫 | GDI has no corresponding GPU query primitive; a CPU pixel-count substitute would be a different contract and needs a separate design. |

---

## Phase G4 — deliberately excluded 3D expansion

The shared Software core already contains CPU 3D machinery, but GDI deliberately overrides those
entry points to throw.  Enabling them would turn GDI into a slow CPU 3D renderer with GDI display,
not improve its intended compatibility-2D role.  These items are recorded so the boundary is
explicit, not forgotten.

| # | Task | Status | Acceptance criteria if scope changes |
|---|---|---|---|
| GDI-040 | Enable vertex and index buffers plus non-indexed/indexed primitive draws. | 🚫 | Require a new project decision changing GDI from 2D-only; then run the Software 3D pixel suite through GDI presentation. |
| GDI-041 | Enable 3D depth/stencil clears and depth/rasterizer state. | 🚫 | Depends on GDI-040 and must provide real depth/stencil storage and state semantics. |
| GDI-042 | Enable stock 3D effects, models, cube sampling and 3D texture sampling. | 🚫 | Depends on GDI-040/041 and needs a complete, separately specified CPU shading contract. |
| GDI-043 | Enable 3D instancing, MRT and occlusion-query integration. | 🚫 | Depends on the earlier 3D tasks and a demonstrated use case; no implementation work is authorized by this plan alone. |

---

## Recommended execution order

1. Complete GDI-003 through GDI-006 first.  No performance or feature claim is trustworthy until
   the actual 2D demo is built and observed on a real Windows/Wine display.
2. Use GDI-010 before optimizing.  Then choose GDI-011, GDI-012 and GDI-015 only where the
   measurements show a bottleneck.
3. For compatibility value, implement GDI-020 (full blend semantics) before visual extras.
4. Next choose GDI-021 (render-target mipmaps) or GDI-022/023 (bounded CPU effects), based on the
   consuming application's needs.
5. Treat GDI-025 through GDI-028 as opt-in 2D+ work with explicit cost/benefit evidence.
6. Do not begin GDI-030 onward without a new scope decision; they are recorded as exclusions, not
   an implied roadmap commitment.
