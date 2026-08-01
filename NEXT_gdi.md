# NEXT_gdi.md — GDI backend handoff

> Branch: `feature/gdi`
> Active plan: `plan_gdi.md`
> Started: 2026-08-01
> This file is the GDI-specific continuity log. Per project-owner instruction, `NEXT.md` must not
> be changed during this work.

## Current focus

- GDI-050 through GDI-060 and GDI-067 are complete. The approved catch-up baseline is commit
  `48826e0b`; later completed tasks are `4c512245`, `01873ca9`, `c8fd70d6`, `47fe3f1e`, and
  `3096ab0c`, followed by the current validated GDI-067 change set.
- GDI-061 and GDI-062 require native visible Windows work. The next safe autonomous candidate is
  GDI-072's typed, construction-time presentation configuration.

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
- GDI-057: an owner-approved one-job, manual-only MSVC/Ninja workflow builds CNA plus the thirteen
  focused GDI executables at `--parallel 2`, runs all fifteen `GDI` CTest cases, and uploads native
  diagnostics on failure. It intentionally does not claim the visible GDI-061 gate.
- GDI-058: applied backbuffer format/depth/MSAA are normalized on construction, reset, and the
  store-only update path; invalid presentation modes throw transactionally. Render targets expose
  actual RGBA8/depthless/single-sample storage, reject other color formats, and have verified
  Preserve/Platform/Discard rebind behavior. The always-present stencil remains a separate
  capability and focused public stencil contract.
- GDI-059: all excluded GDI resource factories and 3D entries now throw
  `System::NotSupportedException`. Public construction fails immediately for cube/3D textures,
  cube render targets, shader effects, occlusion queries and static/dynamic buffers. The focused
  public test also covers depth state and indexed/non-indexed user draws without allowing inherited
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

GDI-050 through GDI-054 and GDI-056 were committed together as the explicitly approved catch-up
baseline. All later tasks use one task per commit.

## Known limitations and external gates

- Native visible Windows lifecycle/DPI validation (GDI-061) cannot be completed in this Linux/Wine
  environment and must remain `needs_human` until recorded on Windows 10/11.
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
- Do not edit `NEXT.md`.

## Decisions

- 2026-08-01: project owner approved one catch-up commit for GDI-050–054/056.
- 2026-08-01: project owner approved a new GDI-specific, manual `workflow_dispatch` MSVC workflow.
- 2026-08-01: `SDL_GetWindowSizeInPixels()` is the GDI presentation/backbuffer pixel authority;
  backend transforms bridge SDL window coordinates explicitly rather than assuming density 1.
- 2026-08-01: CPU framebuffer pixel storage is limited to 16,384 on either axis and 512 MiB per
  resource after including all selected attachment/sample planes and generated mips.
- Preserve XNA/FNA public API compatibility; backend-specific unsupported behavior must fail
  clearly without broadening the GDI 2D contract.

## Validation status

- Fresh MinGW-w64 Release configure in `cmake-build-gdi/`: pass.
- `CNA`, all thirteen focused GDI correctness executables, the presentation benchmark and 2D demo:
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
- Shared SOFTWARE gate: CNA plus eight focused executables build at `-j2`; smoke, rasterizer,
  depth-contract, depth-state, and depth/stencil-usage CTests pass. Under Xvfb the complete
  31-leg MSAA depth contract and 34-leg first-readback supervisor pass after resolving the real
  sample plane on target unbind. See the known stale mip-supervisor limitation above.
- `GraphicsDeviceCapabilityTest.SupportsStencilBuffer`: pass under HEADLESS. The complete
  `GraphicsDeviceCapabilityTest.*` filter is 9 pass / 1 pre-existing configuration mismatch:
  `DoesNotSupportWireFrame` assumes EasyGL, while HEADLESS truthfully reports wireframe support.
- `git diff --check`: pass for the complete GDI-067 change set.

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

Begin GDI-072 by replacing repeated environment reads with one validated, typed configuration
captured at backend construction, including deterministic test overrides and diagnostics. Keep
physical multi-DPI/visible observations in GDI-061 and native performance decisions in GDI-062.
