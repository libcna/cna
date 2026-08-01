# NEXT_gdi.md — GDI backend handoff

> Branch: `feature/gdi`
> Active plan: `plan_gdi.md`
> Started: 2026-08-01
> This file is the GDI-specific continuity log. Per project-owner instruction, `NEXT.md` must not
> be changed during this work.

## Current focus

- GDI-050 through GDI-059 are complete. The approved catch-up baseline is commit `48826e0b`,
  GDI-055 is `4c512245`, and the manual workflow is `01873ca9`.
- Next audit the automatable DPI/resize/input-transform portion of GDI-060, leaving real multi-DPI
  and visible lifecycle observations explicitly human-gated.

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
- GDI-057: an owner-approved one-job, manual-only MSVC/Ninja workflow builds CNA plus the eleven
  focused GDI executables at `--parallel 2`, runs all thirteen `GDI` CTest cases, and uploads native
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
  this is a pre-existing build-system inconsistency, not a GDI regression.
- Do not edit `NEXT.md`.

## Decisions

- 2026-08-01: project owner approved one catch-up commit for GDI-050–054/056.
- 2026-08-01: project owner approved a new GDI-specific, manual `workflow_dispatch` MSVC workflow.
- Preserve XNA/FNA public API compatibility; backend-specific unsupported behavior must fail
  clearly without broadening the GDI 2D contract.

## Validation status

- Fresh MinGW-w64 Release configure in `cmake-build-gdi/`: pass.
- `CNA`, all eleven focused GDI correctness executables, the presentation benchmark and 2D demo:
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
- Native HEADLESS/system-SDL build: `CNA` and `CnaTests` link successfully at `-j2`.
- `GraphicsDeviceCapabilityTest.SupportsStencilBuffer`: pass under HEADLESS. The complete
  `GraphicsDeviceCapabilityTest.*` filter is 9 pass / 1 pre-existing configuration mismatch:
  `DoesNotSupportWireFrame` assumes EasyGL, while HEADLESS truthfully reports wireframe support.
- `git diff --check`: pass for the GDI-059 change set.

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

Start GDI-060 with a source-of-truth audit of Win32 client pixels versus SDL pixel coordinates,
then extend deterministic resize/coordinate tests. Keep real 100/150/200% DPI and visible
fullscreen observations for GDI-061's native-Windows gate.
