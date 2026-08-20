# NEXT_gdi.md — GDI backend handoff

> Adaptation branch: `adapt/gdi` (integration base `677f4c59e066fc9a7ed79430d0fee5ffd69b531c`)
> Historical source: `feature/gdi` at `adc9cc2a2e496d162202733b05ab659199a857b8`
> Active plan: `plans/plan_gdi.md`
> Started: 2026-08-01
> This file is the GDI-specific continuity log. Per project-owner instruction, `NEXT.md` must not
> be changed during this work.

## 2026-08-08 integration adaptation

- The meaningful 34-commit `feature/gdi` sequence has been recreated chronologically on the current
  integration architecture. Historical hashes and dated validation entries below remain
  provenance for the source branch; the separate final-validation entry in this section records
  the current adapted tree.
- Current shared interfaces remain authoritative: `GpuDrawParams::vertexStreams` plus per-binding
  offsets/strides are preserved, removed instance-buffer fields were not restored, and
  REMED-GFX-223 keeps shared cache reconstruction authoritative. REMED-GFX-224 remains an open,
  separate EasyGL render-target `SetData` finding and is not changed by the GDI work.
- REMED-GFX-229 now rejects a positive Software `Texture2D` upload stride below `width * 4` before
  mutation; the existing texture-allocation executable covers padded odd-width asymmetric RGBA
  rows, rejection, and retained pixels.
- REMED-GFX-230 makes Software `RenderTarget2D::UpdatePixels` consume its stride row by row and
  apply the same pre-mutation lower bound; the existing 2D regression covers exact padded-row
  readback and retained pixels after rejection.
- REMED-GFX-231 corrects the supported `SourceAlphaSaturation` CPU blend factor to use
  `min(sourceAlpha, 1-destinationAlpha)` for RGB and one for alpha; an asymmetric/nontrivial-alpha
  2D regression and the current Software blend controls pass.
- REMED-GFX-232 makes DX3's standalone stencil hook report false, consistent with its depth-only
  production path and `GraphicsCapability::StencilBuffer` answer; the focused DX3 capability
  regression compares both paths directly and passes 1/1 through Wine/Xvfb after the x64 MinGW
  build, with the DirectDraw engagement wrapper active.
- REMED-GFX-233 fixes the shared Software persistent-buffer failure exposed by adaptation
  validation. `VertexBuffer(device, count)` intentionally has an empty, zero-stride declaration;
  REMED-GFX-201 had copied that zero into the one-stream snapshot, so every vertex fetch reused
  record zero and produced a degenerate primitive. Only that exact legacy single-buffer shape now
  uses the named-`vb` backend-stride fallback. Nonzero declared streams and multistream semantics
  remain unchanged. The Additive contract pins the empty-declaration precondition plus exact
  non-indexed/indexed pixels. Current Software effects 7/7, Additive 29/29, and scissor 44/44
  controls pass. The mechanism is unchanged from integration base `677f4c59`, so this is an
  integration-lane closure of a newly exercised pre-existing defect, not a defect introduced by
  the GDI replay.
- The GDI memory-DC oracle now exercises selection restoration and bitmap/DC deletion across 64
  create/destroy cycles plus three injected constructor-failure checkpoints. It also checks
  `GetGuiResources` return-to-baseline only after proving that API's live DC/DIB count is stable;
  repeated normal/failure cleanup and operation-counter checks pass. The `GetGuiResources`
  live-delta subcheck skips under Wine because the API is not stable/available there, so this does
  not claim physical-Windows kernel-object leak proof. The executable/case inventory is unchanged.
- REMED-BUILD-017 corrects the manual native-MSVC workflow and command inventory to build all
  **seventeen** correctness executables before running the **nineteen** registered GDI cases. The
  omitted targets were presentation-mode transaction, DC-release transaction, and texture
  allocation.
- REMED-BUILD-018 adds the direct `IGraphicsBackend.hpp` include required by the shared
  `SupportsStencilBuffer` consistency test. The current native ASan/UBSan focused harness compiles
  and runs, closing the incomplete-type error.
- Final adapted MinGW/Wine and native Software sanitizer/control validation is complete as recorded
  below. The manual native-MSVC workflow, visible native-Windows lifecycle/DPI checks, and physical
  Windows handle observation remain external gates.

### Final adapted validation

- Current x64 MinGW Release GDI built all **seventeen** focused correctness executables. CTest
  registered **nineteen** cases, including the three presentation variants, and all **19/19**
  passed serially through Wine 10 on Linux Xvfb display `:104`. `cna_demo_2d` was compile-covered
  only. The benchmark completed four frames per case and passed, but quota-constrained timings are
  not comparable and are not retained as performance evidence.
- The PE32 Intel i386 allocation planners both passed under Wine: framebuffer **5/5**, texture
  **7/7**. Pixel/oracle coverage passed asymmetric RGBA channels, a top-down negative-height DIB,
  corner/orientation probes, nonzero-Y dirty-band clipping, odd-width row padding, and short-stride
  rejection with retained pixels. CPU alpha/blend checks, including corrected
  `SourceAlphaSaturation`, passed. Window presentation remains opaque GDI `SRCCOPY`.
- Repeated handle create/destroy and injected `GetDC`, `CreateDIBSection`, and `SelectObject`
  failures passed with selected-object restoration and authoritative operation counters. The
  Wine-skipped `GetGuiResources` live-delta check is not physical-Windows leak proof.
- Native SOFTWARE Debug ASan+UBSan was proven active by `ldd` (`libasan.so.8`, `libubsan.so.1`).
  The focused harness selected 151 tests: **149 pass, 2 intentional skips, zero CNA ASan/UBSan**.
  Standalone controls pass: effects **7/7**, Additive **29/29**, scissor **44/44**, render-target
  readback **102/102**, SpriteBatch viewport **19/19**, and Texture2D GetData **40/40**.
  LeakSanitizer `detect_leaks=1` is unusable under ptrace, so the valid run used
  `detect_leaks=0`. Full native `CnaTests` compilation remains blocked by the accepted Glide
  `FakeGlide3xDll` fixture's `windows.h`; no Glide finding was opened. The unrelated baseline
  Software `SetRenderTargets_FourTargets` expectation mismatch and Pulse-sensitive capability
  matrix were excluded from the focused set and are not GDI findings.
- REMED-GFX-223's principal current OPENGLES/EasyGL control passed **8/8** runtime pixel/state
  tests (TexturedQuad, Additive BlendState, RT2D readback, render-target viewport/scissor reset,
  InstancedModel, MRT, Additive contract, Texture2D GetData) plus the actual
  `CnjCacheIsolationTest` **2/2** on Mesa OpenGL ES 3.2 llvmpipe/Xvfb `:105`. Shared Texture2D
  cache code is unchanged: REMED-GFX-223 is preserved and REMED-GFX-224 remains open.
- DX3 x64 MinGW plus the capability runtime passed as described above. Sokol at pinned `27b4960`
  built from current sources for native GLCORE and passed Smoke, Instanced3D, and WireFrame **3/3**
  on llvmpipe/Xvfb. Diligent pinned v2.5.6 `b036337` passed generated compile probes for current
  `DiligentGraphicsBackend.cpp` and shared `GraphicsDevice.cpp` under
  `CNA_BACKEND_DILIGENT`; this was compile-only, with no current runtime or full DiligentCore
  rebuild. Skia pinned `ebf5052` with matching local raster archives passed equivalent current-source
  compile probes under `CNA_BACKEND_SKIA`; only external Skia `clang::reinitializes` warnings were
  emitted under GCC. Current i686 MinGW Glide backend/shared-device compile probes passed under
  `CNA_BACKEND_GLIDE`; Glide remained compile-only because `glide3x.dll` was unavailable.
- All compilation used explicit numeric parallelism at or below two across the session. The final
  current-tree runs used `j1` under `CPUQuota=50%`; no helper was unbounded, `j8` was never reached,
  and compilation never exceeded two jobs.

REMED-GFX-229 through REMED-GFX-233, REMED-BUILD-017/018, and GDI-054's handle-oracle hardening are
resolved for their automated scope. No unresolved GDI supported-path finding remains. The evidence
is Linux cross/Wine plus native Linux sanitizer/control coverage, not native MSVC, physical-Windows
lifecycle/DPI, or physical-Windows kernel-object-leak proof.

## Current focus

- GDI-050 through GDI-060, GDI-067, GDI-070, and GDI-072 are committed. The approved catch-up
  baseline is commit `48826e0b`;
  later completed tasks are `4c512245`, `01873ca9`, `c8fd70d6`, `47fe3f1e`, and
  `3096ab0c`, GDI-067 in `de79659d`, GDI-072 in `517a0776`, and GDI-070 in `35c047a2`.
- GDI-071's explicit shared-core source/archive boundary is committed in `47268263`. Its
  native-MSVC workflow result remains pending, so the plan status is 🟨.
- GDI-073's narrowed 4x-MSAA contract is committed in `91d8cf38`.
- GDI-074's framebuffer/`Texture2D`/`RenderTarget2D` extraction is pushed in `b2fa93b0`, its
  backend-state follow-up in `216ca5ef`, and the SpriteBatch frontend split in `726c6f7d`. Those
  CPU sources now compile independently, while the remaining 2D-only wrapper retains only the
  shared triangle raster bridge/helpers. The native-MSVC workflow remains an external final
  validation gate.
- 2026-08-03 follow-up: GDI-075, GDI-076, and GDI-077 (the three implementation gaps the
  2026-08-03 re-audit found) are now closed; see the dated entry below. GDI-078 (this
  documentation/evidence reconciliation) is closed by this same pass.

## 2026-08-03 follow-up: GDI-075/076/077/078

- GDI-075: `GdiGraphicsBackend::SetPresentationMode` now saves the previous mode, attempts
  `SynchronizeBackbufferSize()`, and restores the previous mode on any exception before
  rethrowing -- mirroring the transactional pattern `SetVirtualResolution` already used. A valid
  ordinal whose derived logical size exceeds the axis/byte budget (e.g. `FixedHeightDynamicWidth`
  against an extreme drawable aspect) now leaves the backend fully on its prior mode, dimensions,
  pixels, and damage, and a subsequent `Present()`/`GetViewportSize()` succeeds instead of
  repeating the same failing synchronization. New focused executable
  `cna_test_gdi_presentation_mode_transaction` (`GDI_PresentationModeTransaction`).
- GDI-077: `WindowDeviceContext` gained an explicit, checked `Release()` (returning success plus
  any Win32 error); `Present()` now calls it before `ResetBackbufferDamage()`/generation
  acknowledgement, so a failed release is visible in telemetry (`operation == "ReleaseDC"`) and
  conservatively retains pending damage and the invalidation generation. The destructor is now a
  fallback-only path for exceptional exits and is a no-op after `Release()` has run, so exactly one
  release attempt (real or, under `DebugForceNextReleaseDcFailure()`, simulated) is ever made. New
  focused executable `cna_test_gdi_dc_release_transaction` (`GDI_DcReleaseTransaction`).
- GDI-076: new `SoftwareTextureAllocation.hpp/.cpp` (mirroring GDI-067's
  `SoftwareFramebufferAllocation`) plans a checked CPU-texture layout -- positive dimensions, the
  shared 16,384-axis ceiling, every declared mip level's bytes, and a 512 MiB per-resource budget
  (`SoftwareTextureMaxBytes`) -- before any vector allocation. `SoftwareTextureBackend`'s two
  constructors and `UpdatePixels`/`UpdatePixelsLevel` now validate this layout, reject a
  caller-supplied pixel buffer smaller than its own level (`System::ArgumentException`), truncate
  an oversized one to exactly `width*height*4` rather than retaining the excess, and translate
  `std::bad_alloc`/`std::length_error` to `System::OutOfMemoryException`. This closes the gap at
  the GDI/Software CPU-texture boundary (`GdiGraphicsBackend::CreateTexture` forwards directly
  into it); it deliberately does **not** touch the shared `Texture2D.cpp` constructors or
  `IGraphicsBackend.hpp` (every other backend's own texture path), since those are used
  identically by all configured graphics backends and this task's scope, and this session's ability
  to rebuild/verify every other backend, is GDI/Software-only. A caller going through
  `Texture2D(GraphicsDevice&, w, h[, mipMap, format])` still pays for one wasted transient
  `w*h*4`-byte allocation in `Texture2D.cpp` itself before the GDI/Software boundary's own
  rejection is reached for an over-budget request; eliminating that remains open, cross-backend
  scope. New focused executable `cna_test_gdi_texture_allocation` (`GDI_TextureAllocation`).
- GDI-078: this reconciliation itself. `cmake/BackendLibraries.cmake`'s `CNA_GDI_SOFTWARE_SOURCES`
  is the actual source of truth: **eight** reviewed shared CPU-2D translation units as of this
  session (GDI-076 added `SoftwareTextureAllocation.cpp` to GDI-074's seven), plus GDI's own three
  (`GdiConfiguration.cpp`, `GdiGraphicsBackend.cpp`, `GdiPresentation.cpp`) -- eleven translation
  units in the one GDI backend archive. `cmake/BackendLibraries.cmake` now also `message(STATUS
  ...)`s this exact count at configure time so it is generated evidence, not a copied number (see
  "Useful commands" below for the exact configure-time line to check). `cmake/Tests/GdiTests.cmake`
  registers **seventeen** focused executables (fourteen from before GDI-075/076/077 plus the three
  new ones above) and **nineteen** native `GDI` CTest cases (sixteen single-registration cases plus
  the three `GDI_Presentation_*` environment variants of `cna_test_gdi_presentation_configuration`)
  -- up from fourteen executables/sixteen cases. The stale "two required CPU-2D translation units",
  "five-object GDI archive", and `-j8` claims elsewhere in this file are corrected in place above.
  A fresh MinGW-w64 Release reconfigure plus `-j2` build of all seventeen focused executables, and
  the complete nineteen-case run under Wine/Xvfb with `CNA_GDI_DWM_FLUSH=0`, all pass (see
  Validation status).

## Completed in the current working tree

- GDI-050: independent depth/stencil attachment queries and public stencil capability/coverage.
- GDI-051: SpriteBatch damage comes from the final clipped raster bounds.
- GDI-052: SDL/Win32 expose, restore and resize invalidation is retained until a successful present.
- GDI-053: checked/scoped Win32 presentation transaction with failure retention.
- GDI-054: deterministic presentation planning plus a memory-DC/DIBSection pixel oracle.
- GDI-055: public coverage now exercises presentation state, the complete capability matrix,
  texture upload/readback, SpriteBatch, viewport/scissor, render-target binding/preservation/
  sampling, 4x and rejected 2x MSAA resets, resize and backbuffer readback. Public stencil coverage
  remains in its focused companion test.
- GDI-056: distinct native CTest cases for default, dirty and halftone presentation policies.
- GDI-057/REMED-BUILD-017: an owner-approved one-job, manual-only MSVC/Ninja workflow builds CNA
  plus all seventeen focused GDI correctness executables at `--parallel 2`, runs all nineteen
  `GDI` CTest cases, and uploads native diagnostics on failure. It intentionally does not claim the
  visible GDI-061 gate. The equivalent adapted MinGW/Wine matrix passes 19/19; the first native-MSVC
  result remains pending.
- GDI-058: applied backbuffer format/depth/MSAA are normalized on construction, reset, and the
  store-only update path; invalid presentation modes throw transactionally. Render targets expose
  actual RGBA8/depthless/single-sample storage, reject other color formats, and have verified
  Preserve/Platform/Discard rebind behavior. The always-present stencil remains a separate
  capability and focused public stencil contract.
- GDI-059: all excluded GDI resource factories and 3D entries now throw
  `System::NotSupportedException`. Public construction fails immediately for cube/3D textures,
  cube render targets, shader effects, occlusion queries and static/dynamic buffers. The focused
  public test also covers depth state and indexed/non-indexed user draws without allowing private
  Software 3D behavior to escape.
- GDI-060: presentation and dynamic backbuffer sizing now use `SDL_GetWindowSizeInPixels()` as
  their one pixel-size authority. Input transforms explicitly bridge SDL window coordinates and
  drawable pixels. Caller-provided SDL windows are published to Mouse/TextInputEXT and detached
  without destroying caller ownership. Deterministic 100/150/200% ratio tests and a live
  SDL/Win32 integration cover all modes, odd resizes, fullscreen, edge/bar transforms and retained
  pixels across minimize/restore. The test exposed and fixed Wine's misleading non-zero minimized
  pixel size, which previously reallocated and erased the dynamic-width backbuffer.
- GDI-067: framebuffer storage is attachment-aware and planned before allocation. GDI backbuffers
  and targets now own RGBA8 plus stencil but no unused float depth (5 bytes/pixel); an applied 4x
  backbuffer adds exactly 16 bytes/pixel of sample colour (21 bytes/pixel total). The pure planner
  validates positive dimensions, a 16,384-axis ceiling, every `size_t` operation, mip storage, and
  a 512 MiB per-resource pixel-storage budget before Win32 conversion or allocation. Rejected
  changes preserve prior pixels; allocator failures become `System::OutOfMemoryException`.
  Focused live/pure tests and a genuine 32-bit i686 MinGW harness cover storage and overflow. The
  shared SOFTWARE target now resolves its real 4x plane before readback/mip generation.
- GDI-072: `GdiConfiguration` captures filter, dirty-presentation, and DWM policy exactly once at
  backend construction. The pure strict parser accepts `nearest`/`halftone` and `0`/`1`, keeps safe
  per-setting defaults for invalid values, sanitizes their text, and emits one aggregate warning.
  `Present()` uses only the backend's const snapshot. The existing configuration executable now
  mutates all three environment variables after construction and exercises a contrary typed
  constructor override.
- GDI-070: `GdiGraphicsBackend` now derives directly from `IGraphicsBackend` and privately owns a
  `GdiSoftware2DCore` composition adapter. Only reviewed CPU framebuffer, texture, SpriteBatch,
  2D-target, and state calls are forwarded, so future Software virtual methods cannot silently
  enter GDI. Every resource/3D entry remains explicit. The unsupported-feature executable now has
  42 public/direct boundary checks and a compile-time assertion forbidding Software inheritance.
- GDI-071: the GDI build no longer globs the Software directory or creates a separate
  `cna_backend_graphics_software_core` archive. Its one backend archive names an explicit, reviewed
  list of the required CPU-2D translation units (eight as of GDI-076; see GDI-078 below for the
  exact, CMake-derived count), so future files require deliberate review. The link graph is
  reduced to `CNA` ↔ GDI. A full independent SOFTWARE build exposed its own undeclared reverse
  dependency on CNA (`ColorMatrixEffect::FillSpriteDrawParams`); that cycle is now declared
  centrally, and Software tests no longer carry a GNU-only archive-group workaround.
- GDI-073: the advertised 4x mode is explicitly a filled-SpriteBatch backbuffer capability. Its
  2x2 colour samples use `MultiSampleMask` bits 0 through 3, wireframe remains a crisp full-sample
  DDA path without line antialiasing, and one per-pixel stencil comparison/operation gates every
  active colour sample. High mask bits are ignored, zero active samples cannot modify stencil,
  and render targets remain single-sampled. A focused test locks the contract down in 19 checks.
- GDI-074 (partial): `SoftwareFramebuffer.cpp`, `SoftwareTexture2D.cpp`, and
  `SoftwareRenderTarget2D.cpp` now own independently compiled reusable resource definitions,
  sharing a small allocation-error helper. `SoftwareGraphicsBackend2DState.cpp` owns backend
  lifecycle, target binding, readback, and 2D state application. `SoftwareSpriteBatch.cpp` owns
  the public SpriteBatch geometry/transform/effect path and calls the private shared triangle-raster
  bridge. `SoftwareGraphicsBackend2D.cpp` defines `CNA_SOFTWARE_2D_ONLY` for that remaining raster
  core. It excludes Software vertex/index buffers, cubes and their sampling, programmable effects,
  and general-3D draw bodies; necessary virtual-table entries throw clear
  `System::NotSupportedException` diagnostics. The GDI archive consequently has no Software cube
  implementation or cube-allocation warning. SOFTWARE continues to compile the unguarded source
  plus the shared 2D units.

GDI-050 through GDI-054 and GDI-056 were committed together as the explicitly approved catch-up
baseline. All later tasks use one task per commit.

## Known limitations and external gates

- Native visible Windows lifecycle/DPI validation (GDI-061) cannot be completed in this Linux/Wine
  environment and must remain `needs_human` until recorded on Windows 10/11.
- GDI-071 still needs the owner-approved manual `GDI Windows CI (MSVC)` workflow to pass before it
  can move from 🟨 to ✅. Local validation covers native GCC SOFTWARE and MinGW GDI, not MSVC.
- Native visible performance data (GDI-062) is likewise hardware/human gated. Do not use hidden
  Wine timings to authorize GDI-063 through GDI-066 performance changes.
- The pre-existing native `.sdl-prebuilt-Linux-x86_64` install contains a zero-byte
  `libSDL3.so.0.5.0`; the HEADLESS validation therefore uses the available system SDL packages
  (`CNA_USE_SYSTEM_SDL=ON`) without deleting or rewriting that unrelated cache.
- `CnaTests` still compiles Net tests when `CNA_ENABLE_NET=OFF`, but then omits ENet include paths.
  The HEADLESS validation was reconfigured with `CNA_ENABLE_NET=ON` to complete the test binary;
  this is a pre-existing build-system inconsistency, not a GDI regression. It was reconfirmed when
  an intentionally broad GDI all-target build reached `ENetBackendTests.cpp`; the exact GDI target
  build remains green.
- The broad HEADLESS `GraphicsDeviceValidationTest.*` filter has one pre-existing contract mismatch:
  `SetRenderTargets_FourTargets_DoesNotThrow` expects four MRTs while HEADLESS explicitly rejects
  simultaneous render targets. The 57 device-state/parameter tests relevant to this change pass.
- The local host lacks the Linux `-m32` C/C++ runtime (`Scrt1.o` and 32-bit libstdc++), so the exact
  Ubuntu multilib workflow cannot run locally. Its standalone project is covered here by an actual
  i686-w64-mingw32 executable under Wine; CI installs `gcc-multilib`/`g++-multilib` and runs the same
  planner source at genuine 32-bit `size_t` width.
- `Software_MsaaMipReadback` has a pre-existing stale expectation that SOFTWARE rejects nonzero
  target mip levels. Current production code and `plans/plan_software.md` say those levels are generated
  on unbind; the representative 4x level-zero oracle passes, but the full supervisor fails its
  obsolete refusal assertions. This is outside GDI-067 and should be reconciled in Software scope.
- `SoftwareGraphicsBackend.cpp` still contains the shared 2D triangle raster helpers/bridge and all
  3D/cube source text. Framebuffer, `Texture2D`, `RenderTarget2D`, backend state, and SpriteBatch
  definitions have moved to owned files; GDI no longer compiles unrelated cube/general-3D bodies or
  the GCC `-Wstringop-overflow` warning. GDI-074 remains 🟨 until raster-helper extraction and
  native-MSVC validation complete.
- The full SOFTWARE build still emits that cube-allocation warning. The audit traced it to
  `TextureCube` lacking the general pre-allocation dimension validation that `Texture2D` has; a
  real correction would define a cross-backend public cube-size contract and belongs to Software
  planning, not to the GDI-only archive boundary.
- Do not edit `NEXT.md`.

## Decisions

- 2026-08-01: project owner approved one catch-up commit for GDI-050–054/056.
- 2026-08-01: project owner approved a new GDI-specific, manual `workflow_dispatch` MSVC workflow.
- 2026-08-01: `SDL_GetWindowSizeInPixels()` is the GDI presentation/backbuffer pixel authority;
  backend transforms bridge SDL window coordinates explicitly rather than assuming density 1.
- 2026-08-01: CPU framebuffer pixel storage is limited to 16,384 on either axis and 512 MiB per
  resource after including all selected attachment/sample planes and generated mips.
- 2026-08-01: GDI presentation environment settings are immutable per backend instance. Invalid
  values warn once at construction and fall back individually to nearest/disabled policy.
- 2026-08-01: GDI's runtime contract uses composition, not inheritance from the full Software
  backend.
- 2026-08-01: GDI uses one backend archive with an explicit seven-file CPU-2D source list.
- 2026-08-01: `SoftwareGraphicsBackend2D.cpp` is a deliberately narrow transitional build unit:
  it compiles the remaining shared triangle-raster source with `CNA_SOFTWARE_2D_ONLY`, retaining
  virtual stubs only because `GdiSoftware2DCore` needs the base class's complete virtual table.
  Full SOFTWARE compiles the same source without that macro. The actual raster-helper ownership
  split stays within GDI-074.
- 2026-08-01: `SoftwareSpriteBatch.cpp` owns SpriteBatch's public quad geometry, transform, and
  fixed-effect preparation. Its private `RasterizeSpriteQuad` bridge deliberately retains one
  shared triangle/fragment implementation so GDI and SOFTWARE cannot drift at the pixel boundary.
- 2026-08-01: GDI's 4x claim is limited to filled backbuffer triangles with four colour samples;
  wireframe has no subpixel line AA and stencil/depth are not per sample.
- Preserve XNA/FNA public API compatibility; backend-specific unsupported behavior must fail
  clearly without broadening the GDI 2D contract.

## Historical validation status

The results in this section are preserved historical `feature/gdi` evidence from 2026-08-01 through
2026-08-03. Counts below fourteen/seventeen describe the suite at that historical milestone. See
**Final adapted validation** above for the current seventeen-executable/nineteen-case result.

- Fresh MinGW-w64 Release configure in `cmake-build-gdi/`: pass.
- `CNA`, all fourteen focused GDI correctness executables, the presentation benchmark and 2D demo:
  build pass at `-j2`.
- Wine/Xvfb: smoke, 2D regression, ColorMatrix, public stencil/API/applied-state,
  unsupported-feature, dirty-damage, repaint/failure and presentation-oracle executables pass.
- Wine/Xvfb presentation configuration: default, dirty and halftone variants all pass with
  `CNA_GDI_DWM_FLUSH=0`.
- GDI-055 `cna_test_gdi_public_api`: MinGW compile/link pass and all 33 public-path assertions pass
  under Wine/Xvfb, including exact scissor, RT, 4x resolve and resized-edge pixels.
- GDI-057 workflow: static YAML/action structure inspected locally; it cannot be executed until a
  human manually dispatches it on GitHub. The first native MSVC result therefore remains pending.
- GDI-058 `cna_test_gdi_applied_state`: MinGW compile/link and all applied-state/readback/mip/usage
  assertions pass under Wine/Xvfb. Updated 2D presentation-mode regression, public API matrix and
  public stencil tests also pass; stencil Preserve/Platform/Discard rebind behavior is explicit.
- GDI-058 shared-interface gate: native HEADLESS `CNA` and `CnaTests` rebuild pass at `-j2`; 35
  focused PresentationParameters/GraphicsDeviceInformation unit tests pass.
- GDI-059 focused MinGW build: CNA plus smoke, 2D regression, ColorMatrix and the new
  unsupported-feature executable pass at `-j2`. Wine/Xvfb passes all 15 new public assertions and
  the three updated regression executables; the exception family and diagnostics are verified.
- Post-GDI-059 full Wine/Xvfb milestone: all eleven correctness executables and all three
  presentation configurations pass in one shared display session.
- GDI-060 focused MinGW build: CNA, presentation oracle, repaint invalidation, and window-metrics
  executables compile/link at `-j2`. Wine/Xvfb passes deterministic 100/150/200% coordinate ratios,
  external-window ownership, three odd resizes, every presentation mode, fullscreen entry/exit,
  and exact minimize/restore storage retention.
- Post-GDI-060 full Wine/Xvfb milestone: all twelve correctness executables and all three
  presentation configurations pass in one shared display session. The exact focused build also
  includes the benchmark and 2D demo and passes at `-j2`.
- Native HEADLESS/system-SDL build: `CNA` and `CnaTests` link successfully at `-j2`.
- Native HEADLESS shared-interface validation: 57 GraphicsDevice backend/default/status,
  PresentationParameters, and GraphicsDeviceInformation tests pass.
- GDI-067 focused MinGW build: CNA, all thirteen focused executables, benchmark, and 2D demo compile
  and link at `-j2`. In one Wine/Xvfb session all twelve ordinary executables plus default, dirty,
  and halftone configuration runs pass; the new allocation executable passes all 22 assertions.
- GDI-067 32-bit gate: the standalone i686-w64-mingw32 executable is genuinely 32-bit and passes
  exact 4K layout, arithmetic-overflow, byte-budget, and mip-budget assertions under Wine.
- GDI-072 focused MinGW build: CNA and `cna_test_gdi_presentation_configuration` compile and link at
  `-j2`. Default, dirty, and halftone variants pass under Wine/Xvfb, including strict parsing,
  sanitized aggregate diagnostics, immutable post-construction behavior, and typed overrides. The
  complete fifteen-case Wine/Xvfb GDI correctness matrix also passes. A smoke run with all three
  settings invalid emits exactly one aggregate diagnostic and continues on safe defaults.
- GDI-070 focused MinGW build: CNA, all thirteen correctness executables, benchmark, and demo
  compile/link at `-j2`. All twelve ordinary executables and all three configuration variants pass
  in one Wine/Xvfb session after the composition change. The expanded unsupported-feature test
  passes all 42 public/direct boundary assertions.
- GDI-071/GDI-073 focused MinGW build: CNA, all fourteen correctness executables, benchmark, and
  demo link from the single explicit-source-list GDI archive at `-j2`; Ninja exposes no
  `software_core` target. The final executable link line repeats only `libCNA.a` and
  `libcna_backend_graphics_gdi.a` for the declared cycle. All thirteen ordinary executables and all
  three configuration variants pass in one Wine/Xvfb session.
- GDI-073 `cna_test_gdi_msaa_contract`: all 19 mask, coverage, wireframe, stencil-ordering,
  single-sampled-target, and disable assertions pass under Wine/Xvfb.
- GDI-074 focused MinGW build: CNA, all fourteen GDI correctness executables, benchmark, and demo
  build at `-j2` (this plan's own ceiling -- an earlier draft of this entry wrongly recorded
  `-j8`, corrected by GDI-078) from a GDI archive containing independently compiled
  framebuffer/`Texture2D`/`RenderTarget2D`/backend-state/SpriteBatch units plus
  `SoftwareGraphicsBackend2D.cpp`, rather than the full Software implementation.
  `x86_64-w64-mingw32-ar`/`nm -C` inspection finds no `SoftwareTextureCubeBackend`, Software
  vertex/index-buffer implementation, or normal 3D rasterizer bodies; only small throwing virtual
  stubs remain. All thirteen ordinary executables plus the default, dirty, and halftone
  presentation configurations pass in one Wine/Xvfb session. The current SpriteBatch split reran
  the complete sixteen-case Wine/Xvfb matrix successfully. The full native GCC SOFTWARE build also
  succeeds at `-j2` (also corrected from a wrongly recorded `-j8`); it retains the known cube
  allocation warning, while `Software_Smoke`, `Software_Rasterizer`,
  `Software_SpriteBatch_CustomViewport`, and `Software_SpriteBatch_RasterizerState` pass.
- GDI-071 independent SOFTWARE gate: the full native GCC build links after centrally declaring
  `CNA` ↔ SOFTWARE, including the formerly failing `cna_xnb_audio_metadata_dump`; its test link
  line is portable repeated archives with no `--start-group`. The 57-test `Software` label has
  45 passes, 4 skips, and 8 current functional failures (`RenderTargetReadback`,
  `ColorSpace_MidTone`, `PresentLifecycle`, `SpriteBatch3DOrder`, `FrontFaceWinding`,
  `Deferred_Viewport`, `Deferred_Scissor`, and `DescriptorCapacityContract`). This task changes
  only CMake link/source membership, not those runtime contracts.
- Shared SOFTWARE gate: CNA plus eight focused executables build at `-j2`; smoke, rasterizer,
  depth-contract, depth-state, and depth/stencil-usage CTests pass. Under Xvfb the complete
  31-leg MSAA depth contract and 34-leg first-readback supervisor pass after resolving the real
  sample plane on target unbind. See the known stale mip-supervisor limitation above.
- `GraphicsDeviceCapabilityTest.SupportsStencilBuffer`: pass under HEADLESS. The complete
  `GraphicsDeviceCapabilityTest.*` filter is 9 pass / 1 pre-existing configuration mismatch:
  `DoesNotSupportWireFrame` assumes EasyGL, while HEADLESS truthfully reports wireframe support.
- `git diff --check`: pass for the GDI-074 2D-only translation-unit change set.

## Useful commands

```bash
cmake -S . -B cmake-build-gdi -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64.cmake \
  -DCNA_GRAPHICS_BACKEND=GDI \
  -DCNA_ENABLE_NET=OFF \
  -DCNA_BUILD_TESTS=ON \
  -DCNA_BUILD_EXAMPLES=ON \
  -DCMAKE_BUILD_TYPE=Release \
  -DCNA_USE_CCACHE=ON \
  -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
  -DCNA_MAX_VENDORED_BUILD_JOBS=2

CMAKE_BUILD_PARALLEL_LEVEL=2 cmake --build cmake-build-gdi -j2
```

Ordinary cross configurations do not register PE executables as Linux-host CTests. The final
validation harness exposed the complete nineteen-case inventory and ran it serially through Wine;
for a normal cross build, run the `.exe` files manually with an available display and
`CNA_GDI_DWM_FLUSH=0`. Exact focused commands are maintained in `docs/gdi-backend.md`.

## Immediate next step

Dispatch the manual native-MSVC workflow and complete GDI-061's visible physical-Windows
lifecycle/DPI and handle-lifetime observation. GDI-062 still owns native visible performance
measurement, and GDI-074 still requires extracting the reusable 2D triangle-raster helpers without
changing either backend's behavior. REMED-GFX-224 remains open in its EasyGL render-target scope.
