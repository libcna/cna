# Diligent Engine graphics backend

## Status

The Diligent Engine backend is CNA's newest graphics backend and is **experimental**. Its
implemented surface is the 2D/3D baseline described in `plan_diligent.md` Phase `DILIGENT-1`; it is
**not** at parity with the Vulkan, EasyGL, SDL_GPU or bgfx backends. Read
["What works / what does not"](#what-works--what-does-not) before using it for anything real.

Select it with:

```bash
cmake -S . -B cmake-build-diligent \
  -DCNA_GRAPHICS_BACKEND=DILIGENT \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build cmake-build-diligent -j4
```

## What is different about this backend

Every other CNA backend targets one native graphics API. DiligentCore is itself a portable
abstraction over Direct3D 11, Direct3D 12, Vulkan, OpenGL/GLES and Metal, so CNA sits on two
stacked abstraction layers and **the native API is chosen at runtime**, not by the CMake option.

At startup the backend walks `D3D12 → Vulkan → D3D11 → OpenGL`, restricted to the engines
DiligentCore actually built for the platform, and uses the first that yields both a device and a
swap chain. The selected device type is logged:

```text
CNA Diligent: device type Vulkan
```

Pin one explicitly with the `CNA_DILIGENT_DEVICE` environment variable — `d3d12`, `vulkan`
(or `vk`), `d3d11`, `opengl` (or `gl`), or `auto`:

```bash
CNA_DILIGENT_DEVICE=vulkan ./cna_demo_2d
```

An unrecognised value throws rather than silently falling back, so a typo cannot make a bug report
describe the wrong device.

All shaders are authored once in HLSL and cross-compiled by Diligent: to SPIR-V through its glslang
HLSL front end on Vulkan, to GLSL through its own HLSL2GLSL converter on OpenGL, and to DXBC/DXIL
on Direct3D. There is no offline shader compilation step and no per-device shader source.

## Dependencies

DiligentCore is pinned to **v2.5.6** and fetched by CMake (`cmake/ThirdPartyDiligent.cmake`),
recursive submodules included. It is a large dependency (~200 MB checked out, several minutes to
build on four cores), so for repeated or offline builds point the build at a local clone using
FetchContent's own standard override:

```bash
git clone --depth 1 --branch v2.5.6 --recurse-submodules --shallow-submodules \
  https://github.com/DiligentGraphics/DiligentCore.git ~/deps/DiligentCore

cmake -S . -B cmake-build-diligent \
  -DCNA_GRAPHICS_BACKEND=DILIGENT \
  -DFETCHCONTENT_SOURCE_DIR_DILIGENTCORE=$HOME/deps/DiligentCore
```

On Linux the OpenGL device type additionally needs GLX development headers (DiligentCore's bundled
GLEW includes `<GL/glx.h>`):

```bash
sudo apt-get install -y libgl-dev libglx-dev
```

Without them CMake prints a STATUS line and builds the Vulkan device type only. Vulkan itself needs
no system SDK — DiligentCore vendors the Vulkan headers and loads the loader through volk at
runtime, so only a working `libvulkan.so.1` plus an ICD is required to *run*.

The configure output names exactly what was built:

```text
CNA Diligent: using DiligentCore v2.5.6, engines: Vulkan;OpenGL
```

## What works / what does not

Implemented:

- Device, immediate context and swap chain over a real SDL window, with per-device-type fallback.
- The full clear family (colour/depth/stencil and every combination), `Present`, swap interval,
  runtime resize.
- Virtual resolution and all five `CnaPresentationMode` policies, including the window↔logical
  coordinate transforms input needs on a letterboxed window.
- `Texture2D` — creation with a mip chain, `SetData` (whole image and per mip level), `GetData`
  readback.
- `VertexBuffer` and 16-/32-bit `IndexBuffer`.
- `SpriteBatch` — tint, rotation, origin, both flips, layer depth, transform matrix, per-batch
  sampler filter and address modes.
- 3D draws for vertex strides 16/20/24/32, including `BasicEffect`'s three directional lights with
  Blinn-Phong specular evaluated per pixel.
- `TextureCube` and `Texture3D` — creation with a mip chain, per-face / per-sub-box `SetData` and
  `GetData`. Storage and readback only: no built-in shader variant samples them yet.
- `RenderTarget2D` — off-screen colour with an optional real depth-stencil buffer, `GetData`
  readback, sampling the unbound target as a texture, and mip regeneration on unbind.
- `BlendState`, `DepthStencilState`, `RasterizerState` and slot-0 `SamplerState`, all folded into
  the pipeline-state cache key.
- `ReadBackbuffer` / `GraphicsDevice.GetBackBufferData`.

Not implemented — each **throws with its own name** rather than rendering an approximation, and
`GraphicsDevice.GraphicsCapabilities` reports each honestly:

| Feature | Tracked as |
| --- | --- |
| `RenderTargetCube`, MRT (2..4 simultaneous targets) | `DILIGENT-22`, `DILIGENT-24` |
| MSAA (back buffer and render targets) | `DILIGENT-25` |
| Sampling a cube map or volume texture from a shader | `DILIGENT-34`, `DILIGENT-42` |
| `AlphaTestEffect`, fog | `DILIGENT-31`, `DILIGENT-32` |
| `DualTextureEffect`, `EnvironmentMapEffect`, `SkinnedEffect`, `PbrEffect` | `DILIGENT-33`…`DILIGENT-36` |
| `OcclusionQuery` | `DILIGENT-41` |
| Custom `ShaderEffect` programs | `DILIGENT-42` |
| Hardware instancing | `DILIGENT-43` |

## Known limitations

- **X11 only on Linux.** Diligent's `LinuxNativeWindow` carries an X11 window id and display (or an
  XCB connection) and has no Wayland surface member, so a Wayland session must use SDL's X11
  fallback: `SDL_VIDEODRIVER=x11`. A Wayland session fails at backend construction with that
  instruction rather than deep inside Diligent.
- **OpenGL is built but unverified.** The device path exists and is reachable via
  `CNA_DILIGENT_DEVICE=opengl`, but the verification below was performed on the Vulkan device type only.
  OpenGL's swap-chain image origin differs from Direct3D's and the sprite path's Y orientation has
  not been confirmed there (`DILIGENT-30`).
- **Direct3D 11/12 are code paths only.** They compile only in a Windows-targeting build and have
  not been run.
- **Depth-stencil is always `D24_UNORM_S8_UINT`**, regardless of the requested `DepthFormat`.
- **MRT colour write masks and `BlendState.MultiSampleMask` have no effect** — this backend renders
  to a single, single-sampled target, so slots 1..3 and the coverage mask have nothing to act on.

## Tests

`DiligentDeviceSelectionTest.*` (part of the normal `CnaTests` suite) covers the runtime device
preference order and the `CNA_DILIGENT_DEVICE` override. It needs no GPU, no window and no display.

```bash
ctest --test-dir cmake-build-diligent -R DiligentDeviceSelection --output-on-failure
```

`Diligent_2D` (6 checks), `Diligent_3D` (5 checks) and `Diligent_RenderTarget` (5 checks) are the
real-device pixel proofs: they clear, draw `SpriteBatch` quads and 3D primitives on the back buffer
and into an off-screen target, and assert on pixels read back through
`GraphicsDevice.GetBackBufferData` / `RenderTarget2D.GetData`. Both pass against a real Vulkan device. On a machine with no
usable device they exit 77 and print `[SKIP] CNA Diligent smoke`, which CTest reports as a skip —
reporting a pass with nothing rendered would be dishonest.

```bash
ctest --test-dir cmake-build-diligent -R Diligent --output-on-failure
```

Without a hardware GPU these still run against Mesa's `lavapipe` software Vulkan ICD under a
virtual X server, which exercises the whole real pipeline (HLSL→SPIR-V compilation, pipeline state,
depth testing) but not a vendor driver:

```bash
Xvfb :99 -screen 0 1024x768x24 &
cmake -S . -B cmake-build-diligent -DCNA_TEST_DISPLAY=:99
ctest --test-dir cmake-build-diligent -R Diligent --output-on-failure
```

The rest of `CnaTests` passes under this backend (5692 passed, 7 skipped) except for one
pre-existing failure — `XnbContainerFuzzTest.MutatedRealModelFixtureNeverCrashesAndOnlyFailsCleanly`
fails identically on the `HEADLESS` backend and is unrelated to this one.

The backend is deliberately absent from `docs/graphics-backend-feature-matrix.md`: that document's
columns mean "verified on a real hardware GPU", and everything here was measured on a software
Vulkan device. See `plan_diligent.md`'s "Verification status" section for the levels it
distinguishes.
