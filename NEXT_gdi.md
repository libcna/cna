# NEXT_gdi.md — GDI backend handoff

> Branch: `feature/gdi`
> Active plan: `plan_gdi.md`
> Started: 2026-08-01
> This file is the GDI-specific continuity log. Per project-owner instruction, `NEXT.md` must not
> be changed during this work.

## Current focus

- GDI-050 through GDI-060, GDI-067, GDI-070, and GDI-072 are committed. The approved catch-up
  baseline is commit `48826e0b`;
  later completed tasks are `4c512245`, `01873ca9`, `c8fd70d6`, `47fe3f1e`, and
  `3096ab0c`, GDI-067 in `de79659d`, GDI-072 in `517a0776`, and GDI-070 in `35c047a2`.
- GDI-071's explicit shared-core source/archive boundary is committed in `47268263`. Its
  native-MSVC workflow result remains pending, so the plan status is 🟨.
- GDI-073's narrowed 4x-MSAA contract is committed in `91d8cf38`.
- GDI-074's framebuffer/`Texture2D`/`RenderTarget2D` extraction is pushed in `b2fa93b0`; backend
  state is the current verified follow-up. Those CPU sources now compile independently, while the
  remaining 2D-only wrapper retains SpriteBatch/raster implementation. The native-MSVC workflow
  remains an external final validation gate.

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
- GDI-057: an owner-approved one-job, manual-only MSVC/Ninja workflow builds CNA plus the fourteen
  focused GDI executables at `--parallel 2`, runs all sixteen `GDI` CTest cases, and uploads native
  diagnostics on failure. It intentionally does not claim the visible GDI-061 gate.
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
  `cna_backend_graphics_software_core` archive. Its one backend archive names exactly the two
  required CPU-2D translation units, so future files require deliberate review. The link graph is
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
  lifecycle, target binding, readback, and 2D state application. `SoftwareGraphicsBackend2D.cpp`
  defines `CNA_SOFTWARE_2D_ONLY` for the remaining 2D raster/SpriteBatch core. It excludes Software
  vertex/index buffers, cubes and their sampling, programmable effects, and general-3D draw bodies;
  necessary virtual-table entries throw clear `System::NotSupportedException` diagnostics. The GDI
  archive consequently has no Software cube implementation or cube-allocation warning. SOFTWARE
  continues to compile the unguarded source plus the shared resource units.

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
  target mip levels. Current production code and `plan_software.md` say those levels are generated
  on unbind; the representative 4x level-zero oracle passes, but the full supervisor fails its
  obsolete refusal assertions. This is outside GDI-067 and should be reconciled in Software scope.
- `SoftwareGraphicsBackend.cpp` still contains the 2D raster/SpriteBatch and all 3D/cube source
  text. Framebuffer, `Texture2D`, `RenderTarget2D`, and backend state definitions have moved to
  owned files; GDI no longer compiles unrelated cube/general-3D bodies or the GCC
  `-Wstringop-overflow` warning.
  GDI-074 remains 🟨 until SpriteBatch/raster helper extraction and native-MSVC validation complete.
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
- 2026-08-01: GDI uses one backend archive with an explicit six-file CPU-2D source list.
- 2026-08-01: `SoftwareGraphicsBackend2D.cpp` is a deliberately narrow transitional build unit:
  it compiles the remaining shared raster/SpriteBatch source with `CNA_SOFTWARE_2D_ONLY`, retaining
  virtual stubs only because `GdiSoftware2DCore` needs the base class's complete virtual table.
  Full SOFTWARE compiles the same source without that macro. The actual SpriteBatch/raster-helper
  ownership split stays within GDI-074.
- 2026-08-01: GDI's 4x claim is limited to filled backbuffer triangles with four colour samples;
  wireframe has no subpixel line AA and stencil/depth are not per sample.
- Preserve XNA/FNA public API compatibility; backend-specific unsupported behavior must fail
  clearly without broadening the GDI 2D contract.

## Validation status

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
  demo link from the single five-object GDI archive at `-j2`; Ninja exposes no `software_core`
  target. The final executable link line repeats only `libCNA.a` and
  `libcna_backend_graphics_gdi.a` for the declared cycle. All thirteen ordinary executables and all
  three configuration variants pass in one Wine/Xvfb session.
- GDI-073 `cna_test_gdi_msaa_contract`: all 19 mask, coverage, wireframe, stencil-ordering,
  single-sampled-target, and disable assertions pass under Wine/Xvfb.
- GDI-074 focused MinGW build: CNA, all fourteen GDI correctness executables, benchmark, and demo
  build at `-j8` from a GDI archive containing independently compiled framebuffer/`Texture2D`/
  `RenderTarget2D`/backend-state units plus `SoftwareGraphicsBackend2D.cpp`, rather than the full
  Software implementation. `x86_64-w64-mingw32-ar`/`nm -C` inspection finds no
  `SoftwareTextureCubeBackend`, Software vertex/index-buffer implementation, or normal 3D
  rasterizer bodies; only small throwing virtual stubs remain. All thirteen ordinary executables
  plus the default, dirty, and halftone presentation configurations pass in one Wine/Xvfb session.
  The full native GCC SOFTWARE build also succeeds at `-j8`; it retains the known cube allocation
  warning, while `Software_Smoke` and `Software_Rasterizer` pass.
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

Cross-built GDI tests are intentionally not registered as Linux-host CTests. Run their `.exe`
files under Wine with an available display and `CNA_GDI_DWM_FLUSH=0`; the exact focused commands
are maintained in `docs/gdi-backend.md`.

## Immediate next step

Finish GDI-074 by extracting `SpriteBatch` and the reusable 2D raster helpers into owned source
files without changing either backend's behavior. Keep the current GDI 2D-only compilation
boundary intact throughout. GDI-071 remains provisional until its manual native-MSVC workflow
passes; GDI-061 and GDI-062 remain native visible-Windows gates.
