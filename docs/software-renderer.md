# Software (CPU rasterizer) graphics renderer

Status: current as of **2026-10-31**. `SOFTWARE` is an **Experimental** renderer identity and a
real, deterministic CPU rasterizer. It owns color, depth and stencil storage and produces actual
pixels without a GPU context or native window. It is not the same as `HEADLESS`, which validates
and traces graphics calls without rasterizing, or `STUB`, which is a minimal no-op implementation.

Use [`graphics-renderer-feature-matrix.md`](graphics-renderer-feature-matrix.md) for comparisons
with other renderer families. For a particular device and graphics profile, the live
`GraphicsDevice::GetRendererCapabilityProfileEXT()`, `SupportsCapability()` and per-format
queries remain authoritative. Current defects live only in
[KNOWN_ISSUES.md](../KNOWN_ISSUES.md).

## Build and test

Software has no renderer-specific system dependency and does not need a display server:

```bash
cmake -S . -B cmake-build-software \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCNA_GRAPHICS_RENDERER=SOFTWARE \
  -DCNA_BUILD_TESTS=ON \
  -DCNA_BUILD_EXAMPLES=ON
cmake --build cmake-build-software --parallel 8
ctest --test-dir cmake-build-software -L Software --output-on-failure -j6
```

The label intentionally includes capability/profile skips. A skip is not evidence that Software
implements the skipped feature; inspect the reason and the live capability profile. The focused
smoke, rasterizer, stock-effect and capability executables are
`cna_test_software_smoke`, `cna_test_software_rasterizer`, `cna_test_software_effects` and
`cna_test_software_capability_contract`.

Classic compiled XNA effects are a separate, off-by-default build mode:

```bash
cmake -S . -B cmake-build-software-effects \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCNA_GRAPHICS_RENDERER=SOFTWARE \
  -DCNA_SOFTWARE_COMPILED_EFFECTS=ON \
  -DCNA_BUILD_TESTS=ON \
  -DCNA_BUILD_EXAMPLES=ON
cmake --build cmake-build-software-effects --parallel 8
ctest --test-dir cmake-build-software-effects -L Software --output-on-failure -j6
```

That configuration adds the pinned MojoShader dependency. The default configuration remains
dependency-free at the renderer layer and reports `GraphicsCapability::CompiledEffects=false`.

## Intended use

Software is designed for:

- deterministic pixel and resource tests without a GPU or display server;
- server and CI workloads that need real rasterization and readback;
- an independent implementation for diagnosing renderer-specific output differences; and
- an explicitly selected fallback in a multi-renderer build.

It is not a throughput renderer. The implementation deliberately favors scalar, inspectable and
repeatable behavior over SIMD, threading, tile binning or GPU-like performance. Merely compiling
Software into a multi-renderer binary does not make it an automatic fallback; configure the
runtime fallback chain described in
[`runtime-renderer-selection.md`](runtime-renderer-selection.md).

## Architecture contracts

Source comments cite these decisions by number, so keep the numbering stable.

1. **Correctness and determinism over throughput.** Optimization is justified by a measured need
   and must preserve the renderer's exact-output role.
2. **A declared vertex layout wins.** `VertexDeclaration`, stream offsets and instance frequency
   define input whenever supplied. Recognized legacy strides are only a compatibility fallback
   for older paths that do not provide a declaration.
3. **CPU storage is authoritative.** Draws, clears, resolves, generated mips and readbacks operate
   on renderer-owned typed planes. The backbuffer is RGBA8 `SurfaceFormat::Color`; textures and
   render targets retain the supported declared format rather than pretending every resource is
   RGBA8.
4. **The renderer is windowless and owns no GPU swap chain.** It requests neither a native window
   nor the platform video subsystem. A host may attach an `IPlatformSurfacePresenter`; this does
   not change the renderer's offscreen construction contract.
5. **SpriteBatch uses the same rasterizer as 3D.** Sprites become textured triangles and share
   clipping, sampling, blending, target and multisample behavior with ordinary draws.
6. **The two custom-effect contracts stay separate.** A legacy `ShaderEffect` resource may retain
   source as a compatibility placeholder, but Software advertises no shader dialect, reports
   `CustomEffects=false` and never lets that source determine pixels. Opt-in compiled XNA Effect
   bytecode is instead parsed and executed by the CPU shader runtime.

## Current rendering contract

The following is the maintained renderer-level summary. Format combinations and numeric limits
must still be queried from the selected device.

| Area | Software contract |
|---|---|
| Geometry | Triangle, line and strip topologies plus `PointListEXT`; indexed 16/32-bit draws, multiple vertex streams, instancing, homogeneous clipping, viewport/scissor, culling, depth bias and wireframe. |
| Raster and output state | Top-left triangle coverage, line/point coverage, color and separate-alpha blending, per-target write masks, depth/stencil including two-sided stencil, and the full four-sample mask. |
| Sampling | 2D, cube and volume sampling; point/linear/anisotropic filtering, mip selection, LOD bias, maximum mip level and U/V/W addressing. |
| Effects | All five stock effects, `PbrEffect`, `SkinnedPbrEffect`, fog, lighting, shadows, image-based lighting and stock-effect `Model.Draw()`. |
| 2D | `SpriteBatch`, sorting modes, source rectangles, transforms, `SpriteEffects`, `SpriteFont` and render-target composition. |
| Resources | `Texture2D`, `TextureCube` and HiDef `Texture3D`, mip chains, typed transfers and the supported classic compressed, packed, normalized, float and half-float formats. |
| Targets | `RenderTarget2D`, `RenderTargetCube`, ordered two-to-four-target MRT under HiDef, target sampling, readback, mip generation, supported float/half-float formats and `Alpha8` (one-byte declared texels, sampled as (0, 0, 0, A); L22). |
| MSAA | One real CPU mode: a four-sample 2x2 grid. Requests of four or more are clamped to 4; requests below four become 0 rather than another sample count. |
| Queries | Exact CPU occlusion sample counts with XNA-style lifecycle validation. |

The public XNA profile ceilings still apply: for example, Reach does not gain volume textures or
MRT merely because the CPU could allocate them. Surface support is intentionally per resource
kind; a format accepted for a texture is not thereby promised for a cube, volume or render
target. See [`surface-format-support.md`](surface-format-support.md),
[`texture3d-texturecube-support.md`](texture3d-texturecube-support.md) and
[`rendertarget-support.md`](rendertarget-support.md).

Every CPU framebuffer allocation is checked before committing storage. A single axis is bounded
to 16,384 texels and one framebuffer, including requested mip and multisample planes, is bounded
to 512 MiB. Profile limits are normally stricter. Overflow, unsupported sample counts and budget
exhaustion fail explicitly instead of relying on allocator failure or truncated arithmetic.

Floating-point contraction is disabled for the renderer's translation units so the same scalar
expressions do not silently become fused operations on one architecture but not another. This is
part of its cross-architecture exactness contract, not a general CNA compiler policy.

## Shader contracts

### `ShaderEffect` source

Software does not implement a source-shader language. Its exact public answers are:

- `GraphicsCapability::CustomEffects == false`;
- `GetShaderDialectEXT() == CNA::ShaderDialectEXT::Unknown`;
- `SupportsShaderLanguageEXT(...) == false`; and
- `ExecutesShaderEffectSourceEXT() == false`.

The internal compatibility object can accept and retain a nonempty legacy payload, but drawing
uses the fixed CPU rendering path; the payload is never compiled or executed. Code must not infer
support from successful object construction. The cross-renderer acceptance, dialect and execution
distinctions are in
[`shader-effect-vs-fx-bytecode.md`](shader-effect-vs-fx-bytecode.md).

### Compiled XNA Effect bytecode

With `CNA_SOFTWARE_COMPILED_EFFECTS=ON`, Software reports
`GraphicsCapability::CompiledEffects=true` and executes Direct3D 9 Effect Framework bytecode. The
runtime uses MojoShader for the container/reflection model and retains validated Shader Model 1-3
programs for its CPU vertex and pixel interpreters. Pass state, preshaders, control flow,
derivatives, clipping, perspective interpolation, vertex texture sampling, 2D/cube/volume pixel
sampling, MRT, discard/depth output and SpriteBatch custom passes use the same resource and output
pipeline as stock rendering.

This path does not substitute a stock shader and is not a way to run HLSL or another source string
at runtime. [`fx-compiled-effects.md`](fx-compiled-effects.md) is the shared bytecode, reflection,
state and validation contract; it also owns current input restrictions.

## Presentation and device lifecycle

Software has no native swap chain, display mode or swap timing. Its backbuffer and render targets
remain usable when no display service exists. In that ordinary offscreen mode, `Present()`
finishes without copying pixels anywhere.

Where the platform supplies an `IPlatformSurfacePresenter`—the terminal platform is the current
consumer—`Present()` resolves four-sample storage when needed and passes a tightly packed,
top-down RGBA8 frame to that presenter. This optional route does not create a renderer-owned
window or GPU dependency.

The three-argument `GraphicsDevice::Present` supports a clipped source rectangle. A destination
rectangle or foreign window is refused rather than silently ignored. Reset applies the requested
backbuffer size, depth/stencil choice and the clamped zero-or-four sample count, recreates CPU
storage, resets target/view state and participates in the shared device reset events. Fullscreen
and presentation-interval properties remain API state without native swap-chain effects.

## Testing and differential diagnostics

A Software pixel test is a normal `Game` or `GraphicsDevice` test in a SOFTWARE-configured build.
The smoke, rasterizer and effect examples under `modules/renderers/software/examples/` demonstrate
the supported patterns. Prefer exact colors, counts and exception contracts. Use a tolerance only
when the test names a measured conversion or raster-rounding reason.

The small raw-frame diagnostic is useful when a result must be compared with EasyGL. It needs two
separate builds because each diagnostic executable links its configured renderer:

```bash
cmake --build cmake-build-software --parallel 8 \
  --target cna_diag_software cna_diag_compare
./cmake-build-software/cna_diag_software /tmp/software.rgba

cmake --build cmake-build-debug --parallel 8 --target cna_diag_easygl
tools/platform/run_gpu_tests_private.sh --exec \
  ./cmake-build-debug/cna_diag_easygl /tmp/easygl.rgba

./cmake-build-software/cna_diag_compare \
  /tmp/software.rgba /tmp/easygl.rgba 1
```

The EasyGL half must run through the private GPU runner. Never point it at the owner's live
desktop, and do not use Xvfb: the runner supplies an isolated real-GPU display. The final argument
is an explicit maximum per-channel difference; widen it only with evidence for the scene being
tested.

For a compiled-effect regression, reduce the issue to one profile/opcode/operand rule and pair it
with a nearby accepted control. Prove the CPU interpreter and the relevant native renderer before
changing shared validation. Add malformed-input and profile-boundary coverage when the defect is
in parsing rather than execution. The shared fixtures live in
`tests/support/CNA/TestSupport/CompiledEffectFixtures.hpp`.

## Qualification boundary and history

Software's `Experimental` maturity is deliberate. The active tests cover its public rendering,
resource, state, lifecycle and optional compiled-effect contracts, but a CPU reference renderer
is not a claim of production throughput or GPU-driver equivalence. Shared pixel-center and
hardware-observation uncertainties are recorded in
[KNOWN_ISSUES.md](../KNOWN_ISSUES.md), not duplicated here.

The completed `SOFTWARE-1` through `SOFTWARE-485` implementation campaign is historical. Its
EasyGL comparison evidence map is
[`software-easygl-parity-ledger.md`](software-easygl-parity-ledger.md); it points to the complete
historical tables, deleted plan and individual fixes in Git history. Those records explain how the current contract was
established; they are not a second status page and their old test totals are not current release
criteria.
