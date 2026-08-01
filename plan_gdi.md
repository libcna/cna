# Win32 GDI Graphics Backend — Plan

> **Current state: Release baseline compiled and smoke/regression-tested; the real-display 2D
> demo is ready for manual inspection with its GDI compatibility profile.**
> `CNA_GRAPHICS_BACKEND=GDI` selects a Windows-only backend that uses the shared CPU
> rasterizer for 2D content and displays its RGBA8 backbuffer through Win32 GDI
> `StretchDIBits`.  A MinGW **Release** GDI build has completed with at most two jobs, and its
> hidden-window smoke and 2D regression executables completed successfully under Wine.  The
> visible `cna_demo_2d` window remains the manual visual gate and is deliberately not kept open
> while unattended.
>
> **Status legend:** ✅ implemented; 🟨 code exists but the stated end-to-end verification is
> still missing; ⬜ not started; ⏸ blocked by an external prerequisite; 🚫 intentionally outside
> the current 2D-only scope.  GDI-001 through GDI-003, GDI-005, GDI-006, GDI-010, GDI-020 through GDI-023 are complete;
> GDI-004 awaits manual confirmation using the Release GDI compatibility profile.

---

## Contract and boundaries

GDI is intended as a Windows compatibility and modest-workload 2D backend, not a replacement for
EasyGL.  It must keep using a real Win32 GDI presentation path; adding an SDL renderer, an OpenGL
context, or a Direct3D device would be a different backend rather than an improvement to this one.

The current usable 2D slice already includes RGBA `Texture2D`, CPU `SpriteBatch` (source
rectangles, transforms, rotation and flips), full XNA `BlendState` factors/equations, one colour
`RenderTarget2D` including CPU-generated mips when requested, readback, viewport/scissor state,
and the fixed CPU `ColorMatrixEffect` for SpriteBatch. It deliberately
rejects 3D entry points, depth/stencil, MSAA, cube/volume textures, occlusion queries and custom effects.

All builds and test commands for this plan must use at most two parallel jobs (`-j2` or
`CMAKE_BUILD_PARALLEL_LEVEL=2`).

---

## Phase G0 — establish the real baseline

| # | Task | Status | Acceptance criteria |
|---|---|---|---|
| GDI-001 | Keep `GDI` selectable through `CNA_GRAPHICS_BACKEND`, Windows-gated, and linked with `gdi32`; retain one unambiguous GDI factory. | ✅ | Source integration exists in `cmake/BackendSelection.cmake`, `cmake/BackendLibraries.cmake` and `GdiGraphicsBackend.cpp`. |
| GDI-002 | Keep the CPU 2D framebuffer and the real `StretchDIBits` presentation path. | ✅ | The completed GDI smoke executable creates an SDL `HWND`, clears/readbacks a pixel and calls `Present()` through GDI. |
| GDI-003 | Build the GDI smoke target and run its hidden-window checks. | ✅ | Vendored SDL is initialized; the MinGW GDI build completed with at most two jobs and `cna_test_gdi_smoke.exe` returned success under Wine. |
| GDI-004 | Build and launch `cna_demo_2d` with `CNA_GRAPHICS_BACKEND=GDI` on a real display for manual inspection. | 🟨 | The first full-load run used an unoptimised build and was stopped. Re-inspect the built **Release** GDI compatibility profile (12–20 sprites, 30 FPS), confirming animation, resize behaviour and normal closing without `--smoke`. |
| GDI-005 | Add deterministic 2D regression coverage for GDI: texture upload, source rectangle, tint/alpha, rotation, flip, sampler address modes, render-target sampling, resize and presentation transforms. | ✅ | `cna_test_gdi_2d_regression` passed under Wine from the Release build with byte-exact readback checks for all listed operations; it also caught and prevented double alpha blending on a SpriteBatch split diagonal. |
| GDI-006 | Record a reproducible Windows-native and MinGW+Wine test procedure in `docs/gdi-backend.md`. | ✅ | Documentation gives exact configure/build/run commands, display prerequisite, expected result, staged DLL policy and the two-job limit. |

---

## Phase G1 — presentation performance and image quality

These tasks target the main difference from EasyGL: GDI currently rasterizes on the CPU and submits
the whole backbuffer to GDI each frame.  They must be driven by measurements, not assumptions.

| # | Task | Status | Acceptance criteria |
|---|---|---|---|
| GDI-010 | Measure a baseline: CPU raster time, `Present()` time, frame time and memory at representative resolutions and sprite counts. | ✅ | `cna_bench_gdi_2d` records separate phases. Release MinGW+Wine baseline (4 frames, Ryzen 7 PRO 7840U): 800×600/12 sprites 9.169 ms raster + 0.026 ms present; 800×600/50 32.013 + 0.028 ms; 1280×720/20 13.687 + 0.027 ms. Repeat visibly on native Windows before treating compositor timings as a shipping budget. |
| GDI-011 | Investigate a persistent `DIBSection`/memory-DC presentation surface instead of recreating bitmap metadata and passing the CPU vector through `StretchDIBits` every frame. | ✅ | Decided not to adopt: the CPU rasterizer already owns the authoritative RGBA8 vector, while a DIBSection would require copying the whole backbuffer before every blit. `Present()` is only about 0.02 ms in the Wine baseline versus 8–35 ms rasterization; the 1:1 `SetDIBitsToDevice` path avoids scaling without the extra copy. Reconsider only after a native profile demonstrates a material gain. |
| GDI-012 | Add a 1:1 presentation fast path (`SetDIBitsToDevice` or an equivalent DIB blit) so native-size output does not go through scaling. | 🟨 | `Present()` now takes `SetDIBitsToDevice` when its destination is 1:1 and retains `StretchDIBits` only for scaling. The Release MinGW+Wine smoke/regression tests passed; its 20-frame measurements were 0.018 ms (800×600/12), 0.028 ms (800×600/50) and 0.020 ms (1280×720/20) for `Present()`. Confirm pixel identity and no regression on native Windows before marking complete. |
| GDI-013 | Make final-window scaling intentional: preserve nearest-neighbour/pixel-art scaling by default and evaluate an explicit higher-quality scaling option for non-pixel-art applications. | 🟨 | The default final blit is `COLORONCOLOR` (nearest); `CNA_GDI_PRESENT_FILTER=halftone` opts into GDI `HALFTONE` only when final-window scaling is required. It is documented and the scaled regression path runs in both modes; confirm appearance on native Windows before marking complete. Source-texture sampler semantics remain in the CPU rasterizer and are unchanged. |
| GDI-014 | Add optional dirty-rectangle presentation for UI-like workloads, with a safe full-frame fallback for clears, scrolling, scaling, rotation or unknown damage. | 🟨 | `CNA_GDI_DIRTY_PRESENTATION=1` tracks the union of simple untransformed SpriteBatch destinations and uses a partial 1:1 `SetDIBitsToDevice` blit. Clear, resize, scaling, rotation, non-identity transforms and Win32 invalidation force a full frame. Default remains full-frame; the hidden regression exercises the post-full-present UI update. Benchmark a visible native UI before marking complete. |
| GDI-015 | Profile whether unconditional `GdiFlush()` is necessary.  Avoid it only when presentation ordering and window teardown remain reliable. | 🟨 | Retained for now: the current Release Wine baseline includes `GdiFlush()` yet spends only about 0.02 ms in `Present()` while CPU rasterization costs 8–35 ms. The smoke/regression repeatedly present and tear down cleanly. Do not remove the ordering guarantee until a visible native-Windows stress profile demonstrates a material benefit. |
| GDI-016 | Evaluate best-effort frame pacing through DWM (`DwmFlush`) as an opt-in policy. | 🟨 | `CNA_GDI_DWM_FLUSH=1` calls a dynamically loaded `DwmFlush()` after `GdiFlush`; absent/disabled DWM falls back safely, and default presentation remains non-blocking. Documentation explicitly distinguishes this compositor pacing hint from VSync. Confirm native-Windows pacing/latency before marking complete. |

---

## Phase G2 — higher-fidelity 2D rendering

These are features EasyGL can express through GPU state.  The GDI version would remain CPU-based,
mostly by extending the shared Software rasterizer; therefore every task must also check whether it
changes the `SOFTWARE` backend and run its relevant regression tests.

| # | Task | Status | Acceptance criteria |
|---|---|---|---|
| GDI-020 | Implement exact XNA blend factors and blend equations instead of the former `Opaque` versus simplified alpha-blend choice. | ✅ | The shared CPU 2D raster path stores all four factors, both equations and the dynamic `BlendFactor`. The Release GDI regression passed Opaque, premultiplied AlphaBlend, straight-alpha NonPremultiplied, Additive saturation, independent RGB/A equation and constant-factor readback cases; existing channel-write masking remains post-blend. |
| GDI-021 | Generate and retain mip levels for a `RenderTarget2D` created with `mipMap=true`, after rendering is complete. | ✅ | The shared CPU target builds its RGBA8 chain with a clamped 2×2 box filter on unbind. Lower levels are unavailable during an active pass, then may be sampled or read back. The Release GDI regression passed uniform, regenerated-at-boundary and minified-sampling pixel checks. |
| GDI-022 | Define a small, explicit set of fixed CPU 2D effects (for example colour matrix, greyscale or a simple blur). | ✅ | `ColorMatrixEffect` is a non-shader CNA SpriteBatch extension: a row-major 4×4 RGBA matrix, RGBA offset, identity reset and Rec.709 grayscale preset. It runs after texture/tint and before normal `BlendState`; the hidden GDI integration test verifies grayscale, arbitrary channel remapping/offset and alpha preservation. Cost is one clamped 4×4 transform per covered sprite pixel (16 multiplies, 16 additions, 4 clamps), with no intermediate surface or allocation. |
| GDI-023 | Decide whether custom `Effect` support should remain an explicit exception or use a restricted CPU-effect API. | ✅ | Custom `ShaderEffect` remains an explicit unsupported exception: `CreateEffectBackend()` returns null, so no user source or uniform can be accepted and silently ignored. `SpriteBatch` admits only GDI-022's documented `ColorMatrixEffect`; it rejects every other custom effect. The hidden regression/integration tests assert both sides of this boundary. |
| GDI-024 | Investigate a general CPU shader interpreter for arbitrary custom GLSL/HLSL-like effects. | 🚫 | Outside the intended pure-GDI/2D scope: it is only reconsidered after a concrete compatibility requirement and a performance budget are approved. |
| GDI-025 | Add optional CPU multi-sample anti-aliasing/supersampling. | ⬜ | Sample count is honestly reported, resolves are pixel-tested, and benchmarks show the memory/CPU cost at target resolutions.  It must remain opt-in. |
| GDI-026 | Add a CPU stencil buffer for 2D clipping/masking. | ⬜ | Stencil clear, comparison, operations and masked sprite rendering are tested; capability reporting changes only when a real buffer exists. |
| GDI-027 | Decide separately whether a real depth buffer is useful for 2D layering. | ✅ | Formally unsupported: 2D layering uses SpriteBatch submission/sort order, not a depth resource. GDI forcibly installs a disabled depth/stencil state even though the shared Software core owns one for its separate 3D backend; `DepthStencilBuffer` stays false, clears still reject, and the GDI regression proves a later SpriteBatch draw is not depth-occluded. |
| GDI-028 | Implement anisotropic texture filtering in the CPU sampler, or continue to map it to linear filtering. | ⬜ | Make a measured decision; if implemented, tests demonstrate a different, correct minified/rotated result and document its CPU cost. |
| GDI-029 | Audit and update `SupportsCapability()` as each optional feature becomes real. | ✅ | GDI advertises only `WireFrame`, because the shared CPU SpriteBatch rasterizer draws actual wireframe quad edges; the GDI regression pixel-tests it. It explicitly reports false for 3D, depth/stencil, MSAA, MRT, anisotropy, occlusion, arbitrary custom effects and Texture3D; their existing construction/draw paths reject rather than pretend to provide those features. |

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

1. Finish the manual visual confirmation for GDI-004 using the Release build.  No display claim is
   trustworthy until the actual 2D demo is observed on a real Windows/Wine display.
2. GDI-010 shows CPU rasterization dominates the current hidden-Wine baseline, not `Present()`.
   Repeat visibly on native Windows before choosing GDI-011, GDI-012 or GDI-015; do not optimize
   the GDI blit merely from the current numbers.
3. Next choose an opt-in 2D+ feature from GDI-025 through GDI-028 based on the consuming
   application's needs and explicit cost/benefit evidence.
4. Do not begin GDI-030 onward without a new scope decision; they are recorded as exclusions, not
   an implied roadmap commitment.
