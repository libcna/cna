# FNA3D renderer

**Status: reconciled with the implementation on 2026-10-31.** `FNA3D` is an
**Experimental** real-pixel renderer family. It presents XNA's graphics API through the FNA3D C
library, which selects one of its OpenGL, Direct3D 11 or SDL_GPU drivers at runtime. It is not a
second name for any of those CNA renderer identities.

This page is the current renderer contract. The dated XNA image comparison is in
[`fna3d-parity-report.md`](fna3d-parity-report.md), retired `FNA3D-0`..`63` task evidence is in
Git history, and the authoritative list of current defects is [`handoff.md` §5](../handoff.md#5-known-bugs-and-limitations).

## Build and test

The ordinary single-renderer configuration is:

```bash
cmake -S . -B cmake-build-fna3d \
  -DCNA_GRAPHICS_RENDERER=FNA3D \
  -DCNA_BUILD_TESTS=ON \
  -DCNA_BUILD_EXAMPLES=ON \
  -DCNA_FNA3D_TEST_DRIVER=OpenGL
cmake --build cmake-build-fna3d --parallel 8
tools/platform/run_gpu_tests_private.sh cmake-build-fna3d \
  -L Fna3d --output-on-failure -j6
```

FNA3D needs a real SDL video window. GPU and window tests must therefore use the private runner;
do not run them on the owner's desktop or substitute Xvfb for the Vulkan-capable private Xwayland.
`CNA_FNA3D_TEST_DRIVER` makes the registered suite deterministic. It accepts `OpenGL`, `Vulkan`,
`D3D12`, `Metal`, `D3D11` and `SDLGPU`; `Vulkan`, `D3D12` and `Metal` select FNA3D's SDL_GPU driver
and the corresponding SDL GPU backend. The default is `Metal` on Apple and `OpenGL` elsewhere.

For an offline or candidate dependency checkout, add:

```bash
-DFETCHCONTENT_SOURCE_DIR_FNA3D=/path/to/FNA3D
```

That checkout must include its initialized `MojoShader` submodule. Configure reports a supplied
checkout that differs from CNA's pin or has local changes. FetchContent-populated sources are held
to the pin unless `CNA_FNA3D_ALLOW_SOURCE_DRIFT=ON` deliberately relaxes the check.

## Identity and runtime route

| Field | Value |
|---|---|
| Public identity | `FNA3D` (`GraphicsRendererType::Fna3d`) |
| Maturity | `GraphicsRendererMaturity::Experimental` |
| Compile definition | `CNA_RENDERER_FNA3D` |
| Module / target | `modules/renderers/fna3d` / `cna_renderer_fna3d` |
| Implementation | `CNA::Internal::Renderers::Fna3d::Fna3dRenderer` |
| Window contract | SDL video window; 24-bit depth, 8-bit stencil, double-buffered GL-compatible visual |
| Native driver | Chosen by FNA3D at runtime: SDL_GPU, Direct3D 11 or OpenGL |

```text
CNA game
  -> Microsoft::Xna::Framework::Graphics
  -> CNA Fna3dRenderer (IGraphicsRenderer)
  -> FNA3D
  -> SDL_GPU | Direct3D 11 | OpenGL
```

FNA3D tries SDL_GPU, Direct3D 11 and OpenGL in that order. Set the SDL hint
`FNA3D_FORCE_DRIVER` to pin `SDLGPU`, `D3D11` or `OpenGL`; the `Vulkan`, `D3D12` and `Metal`
aliases pin SDL_GPU and its native backend.

`FNA3D_PrepareWindowAttributes()` is part of device construction, not an optional query. It
selects the driver and primes the window attributes before the SDL window exists. CNA calls the
pre-window route while assembling `WindowDescription`, and the renderer establishes the same
precondition before `FNA3D_CreateDevice`. Moving this responsibility must preserve both timing and
driver selection.

## Dependency and source pin

| Component | CNA source |
|---|---|
| FNA3D | [`libcna/FNA3D`](https://github.com/libcna/FNA3D), commit `10f650b9d0f6bb0e6f3f9378833fbd807dfe8cde` |
| FNA3D upstream base | FNA3D release 26.10, followed by CNA's dependency-audit fixes |
| MojoShader | [`libcna/mojoshader`](https://github.com/libcna/mojoshader), submodule commit `03b686fe1a33863a61fa153d6d33c3b6eafa514d` |
| System contract | SDL 3.2 or newer, supplied by CNA's existing SDL target |
| Integration | `cmake/ThirdPartyFNA3D.cmake`; FNA3D and MojoShader are linked as static archives |
| License | zlib |

Configure does not patch either checkout. `cmake/Fna3dSourceCheck.cmake` verifies the FNA3D
revision, the recorded and checked-out MojoShader revisions, and local modifications. A pin is
always a full commit SHA, never a moving branch.

## The two forks

The `cna` branch of each fork carries CNA-required behavior as ordinary reviewable commits while
the default branch tracks upstream. They are not drop-in ABI replacements for upstream shared
libraries: CNA adds extension entry points, render-state values and public-structure fields, and
links both libraries statically.

- The FNA3D fork owns the XNA pixel-centre and legacy bump-map controls, format-exact bounded
  transfers, multi-device GL context handling, corrected OpenGL and SDL_GPU resolve/readback
  behavior, tracing, package/install fixes and the `tests/cna` regression suite.
- The MojoShader fork owns hardened Effect and preshader parsing, Direct3D 9 shader validation,
  GLSL/GLSL ES, SPIR-V, Metal and D3D11 fixes, SDL_GPU adapter support and its own `tests/cna`
  suite. Its Effect Framework ABI is also used by other CNA compiled-effect renderers.

To follow upstream, merge upstream into each `cna` branch; do not rebase pinned history. Update
MojoShader first, move FNA3D's submodule to that commit, validate both fork suites and CNA's
compiled-effect suites through the affected drivers, and only then move
`CNA_FNA3D_GIT_TAG`. The detailed compiled-effect dependency boundary is in
[`fx-compiled-effects.md`](fx-compiled-effects.md).

Changes to the forks are left to human maintainers under those projects' AI policies. Defects CNA
has found in them, with reproducers and CNA's interim handling, are collected in
[`fna3d-dependency-defects.md`](fna3d-dependency-defects.md).

## Shader and effect contract

FNA3D's shader entry point consumes a compiled Direct3D 9 Effect Framework binary. It does not
compile a GLSL or HLSL source string. The distinction is reflected in the live capabilities:

| Contract | Value |
|---|---|
| `GraphicsCapability::CompiledEffects` | true |
| `GraphicsCapability::CustomEffects` | false |
| Compiled-effect dialect | XNA/FNA Effect bytecode through FNA3D and MojoShader |
| Source `ShaderEffect` dialect / execution | `Unknown` / false |

`BasicEffect`, `AlphaTestEffect`, `DualTextureEffect`, `EnvironmentMapEffect` and
`SkinnedEffect` execute their real compiled stock artifacts and preserve XNA's shader-index
selection. CNA's `PbrEffect` and `SkinnedPbrEffect` also use CNA-owned compiled Effect binaries,
including metallic-roughness material maps, image-based lighting, punctual and shadow lights, fog,
vertex color, skinning and double-sided tangent-space handling.

Caller-supplied `Effect(GraphicsDevice&, byte[])` uses the same native runtime. Hardware
instancing additionally requires a compiled vertex shader that declares the instance stream; the
stock shaders do not, so stock-effect instancing is refused. See
[`shader-effect-vs-fx-bytecode.md`](shader-effect-vs-fx-bytecode.md) for the source-versus-bytecode
API boundary.

## Current rendering contract

| Area | Implemented contract |
|---|---|
| Geometry | Indexed and non-indexed lists, strips, lines and points; 16- and 32-bit indices; user and GPU buffers; up to 16 vertex streams |
| 2D | `SpriteBatch`, every sort mode, transforms, flips, source rectangles, large-batch splitting and compiled custom-effect passes |
| States | Blend, depth/stencil, rasterizer, sampler, viewport, scissor, color-write masks, blend factor, multisample mask and reference stencil |
| Textures | `Texture2D`, `TextureCube` and HiDef `Texture3D`; mip and partial transfers; vertex textures; live format classification |
| Targets | `RenderTarget2D`, `RenderTargetCube`, MRT, per-target depth/stencil, MSAA resolve, mip generation and readback |
| Effects | Five stock effects, compiled caller effects, PBR and skinned PBR; no source `ShaderEffect` |
| Presentation | Backbuffer reset/readback, vsync and fullscreen values, plus Letterbox, Overscan, Stretch, NativeBackBuffer and FixedHeightDynamicWidth destination layouts |
| Queries | Occlusion queries only when the selected driver passes the device probe |

CNA-FIX-D24 preserves measured XNA One/Zero blend equations despite FNA3D's factor-only blending
enable rule. The adapter supplies an effective unit constant when a non-Add equation needs
execution, while retaining the user's BlendFactor and restoring it for weighted states. The
ordinary RGBA/HiDef matrix includes independent colour/alpha functions, all sixteen Min/Max
refusals, dynamic factor changes and the subsequent weighted draw; see the
[reference provenance](../tests/reference/xna40/blend-function-one-zero/PROVENANCE.json).


FNA3D's enumerations intentionally match XNA's numeric values. `Fna3dEnumMapping.hpp` pins that
assumption with compile-time assertions so a future mismatch fails the build rather than silently
changing state.

## Driver-dependent capabilities

Capabilities and formats must be queried from the live device; choosing the `FNA3D` family alone
does not determine all of them.

| Feature | Rule |
|---|---|
| MSAA | Adapter requests round down to 4x, 2x or 0; the selected driver may lower that through `FNA3D_GetMaxMultiSampleCount` |
| Stencil | Reported from the live backbuffer/device contract |
| Occlusion queries | Probed on the live device; available on qualifying OpenGL/D3D11 routes and absent from FNA3D's SDL_GPU route |
| Instancing | Requires `FNA3D_SupportsHardwareInstancing` and a compatible compiled vertex shader |
| Multiple streams | Native FNA3D binding array, limited to XNA's 16 streams |
| DXT/BC7 and sRGB | Conditional on the selected driver and resource kind |
| Float and half-float targets | Exposed through the renderer capability profile; actual format use remains driver-dependent |
| Missing stock inputs | OpenGL can provide neutral defaults; SDL_GPU and D3D11 refuse unsupported missing inputs by name |

FNA3D exposes no maximum-texture-size query, so CNA reports the shared HiDef guarantee of 4096.
Per-format support and usage masks, not that number alone, decide whether a resource can be
created.

## Resource and lifecycle rules

- Native resources carry a device identity/liveness token. A resource from another device, or one
  used after its device is disposed, is rejected deterministically; destruction after device
  shutdown remains safe.
- Public `Texture2D` promotion includes `Color`, `NormalizedByte2`, `NormalizedByte4`, `Single`
  and `Vector4`, with the remaining formats decided by the shared and live driver gates.
  Texture3D accepts the uncompressed transfer formats; CNA does not invent a block layout for
  compressed cube or volume transfers.
- When a driver cannot read a volume texture, CNA keeps a bounded exact mirror of initialized
  uploads. A read covering uninitialized voxels is refused without modifying the destination.
- Backbuffer readback obtains the full FNA3D frame and then crops it, preserving CNA/XNA top-left
  rectangle semantics across driver differences. Compressed Texture2D readback is separately
  probed and refused when the driver cannot return the blocks.
- Buffer growth allocates the replacement before retiring the old allocation. Failed growth keeps
  the existing buffer, contents and index width intact.
- Reset updates backbuffer format, dimensions, depth/stencil, fullscreen, interval and MSAA
  through FNA3D, then recomputes presentation layout. Device-independent resources remain owned by
  their original device.

## Explicit refusals and current boundaries

The renderer intentionally refuses unsupported operations instead of advertising a placeholder:

- source-string `ShaderEffect` creation or execution;
- stock-effect hardware instancing;
- render-target array slices;
- compressed cube and Texture3D transfers;
- a vertex stream whose stride cannot be resolved from a public declaration;
- `GraphicsDevice::Present` source/destination-region and foreign-window overloads beyond CNA's
  renderer-owned presentation layout;
- simulated context loss/restoration, which FNA3D does not expose as a portable device contract.

Important current boundaries are deliberately not expanded into a second defect ledger:

- `RasterizerState.MultiSampleAntiAlias` is forwarded in `FNA3D_RasterizerState` (CNA-FIX-L20):
  the OpenGL driver toggles `GL_MULTISAMPLE` (`Fna3d_MsaaFragmentContract`), D3D11 sets
  `MultisampleEnable`, and the SDL_GPU-based drivers have no such switch. This is separate from
  creating and resolving multisampled backbuffers and render targets.
- PBR and SkinnedPBR input without `TANGENT0` reads the neutral (0, 0, 0, 1) tangent on every
  driver (CNA-FIX-L23): the OpenGL driver supplies it as GL's default attribute, and on the
  SDL_GPU and D3D11 drivers, which cannot build a pipeline with a missing input, the renderer binds
  it as a one-element per-instance stream. `Fna3d_PbrVertexContract` (configured driver) and
  `Fna3d_PbrVertexContract_SdlGpuDriver` pin that a mesh with and without tangents draws alike.
- Same-frame update visibility for cube, volume and mipped 2D textures follows FNA3D's SDL_GPU
  driver ordering, which differs from XNA in the cases listed in `handoff.md`.
- Integer pixel-centre correction is established for ordinary triangles. Lines and points remain
  at the host driver's centers, and native Direct3D 9 behavior for those primitives remains an
  external measurement boundary.

Consult [`handoff.md` §5](../handoff.md#5-known-bugs-and-limitations) for the
current MojoShader profile gaps, readback costs and restrictions, trace/device limitation,
D3D11-driver findings and every other open item.

## Validation and qualification

- Linux is exercised through FNA3D's OpenGL driver and its SDL_GPU/Vulkan driver, including stock
  and compiled effects, resources, targets, formats and presentation.
- The Apple M4 campaign exercised the full macOS tree through FNA3D's Metal-selected SDL_GPU
  route. Its current known format/query limits are recorded in
  [`apple-platforms.md`](apple-platforms.md), not generalized to iOS.
- Native Direct3D 11 execution remains an external qualification gate. Cross-compiled and
  Wine-visible coverage does not establish native-Windows driver correctness.
- The 2026-08-11 39-scene comparison against checked-in real-XNA images is retained as a historical
  measurement in [`fna3d-parity-report.md`](fna3d-parity-report.md). It is evidence, not a current
  pass-count dashboard or release gate.

When rerunning the image corpus, keep it off the live desktop:

```bash
tools/platform/run_gpu_tests_private.sh --exec \
  scripts/run-oracle-corpus-diff-fna3d.sh \
  ./cmake-build-fna3d/cna_oracle_render_fna3d
```

Registered suite totals are intentionally not copied here: `ctest -N -L Fna3d` in the actual
build tree is the authoritative inventory.
