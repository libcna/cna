# NEXT_gdi.md — GDI backend handoff

> Branch: `feature/gdi`
> Active plan: `plan_gdi.md`
> Started: 2026-08-01
> This file is the GDI-specific continuity log. Per project-owner instruction, `NEXT.md` must not
> be changed during this work.

## Current focus

- Checkpoint the freshly revalidated GDI-050 through GDI-054 and GDI-056 working tree as one
  explicitly approved catch-up commit.
- Then finish GDI-055, add the approved manual MSVC workflow from GDI-057, and continue through the
  safe, automatable GDI roadmap in dependency order.

## Completed in the current working tree

- GDI-050: independent depth/stencil attachment queries and public stencil capability/coverage.
- GDI-051: SpriteBatch damage comes from the final clipped raster bounds.
- GDI-052: SDL/Win32 expose, restore and resize invalidation is retained until a successful present.
- GDI-053: checked/scoped Win32 presentation transaction with failure retention.
- GDI-054: deterministic presentation planning plus a memory-DC/DIBSection pixel oracle.
- GDI-056: distinct native CTest cases for default, dirty and halftone presentation policies.

These changes are not yet committed. The project owner approved one catch-up commit for this
already interwoven baseline; all later tasks return to one task per commit.

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
  this is a pre-existing build-system inconsistency, not a GDI regression.
- Do not edit `NEXT.md`.

## Decisions

- 2026-08-01: project owner approved one catch-up commit for GDI-050–054/056.
- 2026-08-01: project owner approved a new GDI-specific, manual `workflow_dispatch` MSVC workflow.
- Preserve XNA/FNA public API compatibility; backend-specific unsupported behavior must fail
  clearly without broadening the GDI 2D contract.

## Validation status

- Fresh MinGW-w64 Release configure in `cmake-build-gdi/`: pass.
- `CNA`, all eight focused GDI correctness executables, the presentation benchmark and 2D demo:
  build pass at `-j2`.
- Wine/Xvfb: smoke, 2D regression, ColorMatrix, public stencil, dirty damage, repaint/failure and
  presentation-oracle executables pass.
- Wine/Xvfb presentation configuration: default, dirty and halftone variants all pass with
  `CNA_GDI_DWM_FLUSH=0`.
- Native HEADLESS/system-SDL build: `CNA` and `CnaTests` link successfully at `-j2`.
- `GraphicsDeviceCapabilityTest.SupportsStencilBuffer`: pass under HEADLESS. The complete
  `GraphicsDeviceCapabilityTest.*` filter is 9 pass / 1 pre-existing configuration mismatch:
  `DoesNotSupportWireFrame` assumes EasyGL, while HEADLESS truthfully reports wireframe support.
- `git diff --check`: pending immediately before commit.

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

Run `git diff --check`, commit the approved catch-up baseline, then complete GDI-055's remaining
public-API matrix.
