# Diligent Engine graphics backend

## Status

The Diligent Engine backend is CNA's newest graphics backend and is **experimental**. Its
implemented surface covers the 2D/3D baseline plus render targets (2D and cube), occlusion queries,
MSAA, hardware instancing, `PbrEffect`/`SkinnedPbrEffect` and most stock effects, described in
`plan_diligent.md` Phases `DILIGENT-1` through `4`; it is **not** at parity with the Vulkan, EasyGL,
SDL_GPU or bgfx backends (custom `ShaderEffect` programs and `RenderTargetCube` MSAA are still
unimplemented). Read
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
on Direct3D. There is no offline shader compilation step and no per-device shader source. The
shared HLSL constant-buffer declarations use `#pragma pack_matrix(row_major)` rather than an
inline `row_major` qualifier on each matrix: the HLSL2GLSL converter only recognizes and strips the
pragma form, and passes an inline qualifier through into the GLSL output completely unchanged
(which is not valid GLSL and fails to compile).

Unlike the Vulkan/D3D device types, Diligent's own OpenGL context object does not create a GL
context itself — it asserts one is already current on the calling thread and attaches to it. This
backend creates and makes current a real SDL GL context (`SDL_GL_CreateContext`/
`SDL_GL_MakeCurrent`) before ever calling into Diligent's OpenGL device creation, and requests the
matching SDL window flag (`SDL_WINDOW_OPENGL` vs. `SDL_WINDOW_VULKAN`) by reading the same
`CNA_DILIGENT_DEVICE` override — SDL3 rejects a window created with both graphics flags set.

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
  `GetData`. A cube map is also sampleable, through `EnvironmentMapEffect`; volume textures are
  storage and readback only.
- `RenderTarget2D` — off-screen colour with an optional real depth-stencil buffer, `GetData`
  readback, sampling the unbound target as a texture, mip regeneration on unbind, and its own
  independent MSAA (a real multisampled colour/depth-stencil texture plus a resolve texture,
  resolved on unbind).
- `RenderTargetCube` — six per-face render-target views over one cube texture, a shared
  depth-stencil buffer, `GetData` per face, and sampling back through `EnvironmentMapEffect` the
  same way a plain `TextureCube` does.
- `AlphaTestEffect`'s per-pixel discard and `BasicEffect`'s fog, on every 3D shader variant.
- `DualTextureEffect` (two modulated layers), `EnvironmentMapEffect` (cube-map reflection, flat or
  Fresnel-weighted) and `SkinnedEffect` (72-bone palette, stride 52).
- Several simultaneous render targets: all bound slots are attached and cleared. Only slot 0
  receives fragments, because every built-in shader here declares a single output.
- `BlendState`, `DepthStencilState`, `RasterizerState` and slot-0 `SamplerState`, all folded into
  the pipeline-state cache key.
- `ReadBackbuffer` / `GraphicsDevice.GetBackBufferData`.
- `OcclusionQuery` — a real `IQuery`-backed query. On a device with the `occlusionQueryPrecise`
  feature this is an exact visible-sample count (`QUERY_TYPE_OCCLUSION`); without it (including
  Mesa's `lavapipe`, this backend's own verification device) it transparently falls back to
  `QUERY_TYPE_BINARY_OCCLUSION` and reports only 0 or 1, the same convention `OcclusionQuery`
  already documents for EasyGL's GLES3.
- MSAA on the back buffer: a real offscreen multisampled colour (and depth-stencil) texture,
  resolved into the swap chain's back buffer on `Present()`/`ReadBackbuffer()`. Requested counts
  are clamped to what the swap chain's colour and depth-stencil formats both actually support
  (`GetTextureFormatInfoExt()`), matching FNA's own `RenderTarget2D.MultiSampleCount`/backbuffer
  clamp semantics.
- Hardware instancing (`DrawInstancedPrimitivesEx`): a per-instance vertex buffer supplies one 4x4
  world matrix per instance (four consecutive `float4` rows) at vertex input slot 1 with a
  per-instance step rate, alongside the per-vertex `Position`-only stream at slot 0. Deliberately
  minimal, matching every other CNA backend's own instancing baseline: no texture, no lighting,
  flat diffuse colour output.
- `SetStringMarkerEXT` — a real `IDeviceContext::InsertDebugLabel()` call, visible to external debug
  tools (RenderDoc, PIX, Vulkan validation layers with debug-utils enabled); has no rendering effect
  of its own.
- `SetDataOptions` streaming hints on vertex/index buffers: `Discard`/`None` map to
  `MAP_FLAG_DISCARD`, `NoOverwrite` to `MAP_FLAG_NO_OVERWRITE`, the same mapping this backend's
  D3D11 sibling uses.
- Per-vertex lighting (`BasicEffect`/`SkinnedEffect`'s `PreferPerPixelLighting == false`, real
  XNA's own default): lighting is evaluated once per vertex and Gouraud-interpolated, rather than
  always re-evaluated per fragment. Same Blinn-Phong formula as the per-pixel path either way --
  only the stage differs.
- `PbrEffect`/`SkinnedPbrEffect` (strides 48/68): the glTF 2.0 metallic-roughness BRDF (GGX
  distribution, Smith-Schlick-GGX visibility, Schlick Fresnel), five optional texture maps (base
  colour, normal, metallic-roughness, emissive, occlusion) each falling back to their own glTF
  "map absent" identity when unbound. `SkinnedPbrEffect` combines the same BRDF with `Skinned3D`'s
  bone-palette skinning; its pixel stage is the unskinned shader's own, unmodified.
- Real per-slot `SamplerState`: each of the up to 16 sampler slots (`SamplerStateCollection`) keeps
  its own filter/address/anisotropy state, matching the established cross-backend HLSL register
  convention (`g_Texture`→slot 0, `g_Texture2`/`g_EnvMap`→slot 1, the 4 `PbrEffect` maps→slots 1-4).
  Verified by `Diligent_DualTextureEnvMap`'s two independent-sampler-slot checks (`DILIGENT-48`).

Not implemented — each **throws with its own name** rather than rendering an approximation, and
`GraphicsDevice.GraphicsCapabilities` reports each honestly:

| Feature | Tracked as |
| --- | --- |
| MSAA on `RenderTargetCube` | `DILIGENT-25` |
| Sampling a volume texture from a shader | `DILIGENT-42` |
| `SkinnedEffect`'s stride-56 vertex-colour variant | `DILIGENT-35` |
| Custom `ShaderEffect` programs | `DILIGENT-42` |

## Known limitations

- **X11 only on Linux.** Diligent's `LinuxNativeWindow` carries an X11 window id and display (or an
  XCB connection) and has no Wayland surface member, so a Wayland session must use SDL's X11
  fallback: `SDL_VIDEODRIVER=x11`. A Wayland session fails at backend construction with that
  instruction rather than deep inside Diligent.
- **OpenGL creates a device and renders most of the baseline, but is not fully verified or CTest-covered
  yet (`DILIGENT-30`).** The verification below was performed on the Vulkan device type; running the
  same GPU test binaries by hand with `CNA_DILIGENT_DEVICE=opengl` passes most checks, but two real
  bugs remain open: a texture/shader-resource-variable binding issue where the second distinct
  texture sampled in a session appears to still read the first one's content (affects
  `SpriteBatch.Draw` with a `sourceRectangle`, `DualTextureEffect`'s second layer, sampling an
  unbound `RenderTarget2D`, and `RenderTarget2D` MSAA resolve), and `SkinnedEffect`'s vertex shader
  failing to convert to GLSL at all (a local `float4x4` variable never gets translated, unlike the
  same type inside a constant buffer).
- **Direct3D 11/12 are code paths only.** They compile only in a Windows-targeting build and have
  not been run.
- **Depth-stencil is always `D24_UNORM_S8_UINT`**, regardless of the requested `DepthFormat`.
- **MRT colour write masks have no effect on slots 1..3** — every built-in shader here declares a
  single output, so only slot 0 ever receives fragments regardless of the requested mask.
  `BlendState.MultiSampleMask` also has no effect: it targets per-sample coverage, which this
  backend's pipelines don't expose a way to set.
- **`RenderTargetCube` is never multisampled**, even when the back buffer or a `RenderTarget2D` is —
  requesting MSAA on one clamps to 1 (`DILIGENT-25`).

## Tests

`DiligentDeviceSelectionTest.*` (part of the normal `CnaTests` suite) covers the runtime device
preference order and the `CNA_DILIGENT_DEVICE` override. It needs no GPU, no window and no display.

```bash
ctest --test-dir cmake-build-diligent -R DiligentDeviceSelection --output-on-failure
```

Fifteen further binaries are the real-device pixel proofs (71 checks total): `Diligent_2D` (6),
`Diligent_3D` (6), `Diligent_RenderTarget` (5), `Diligent_RenderTargetCube` (4),
`Diligent_AlphaTestFog` (4), `Diligent_DualTextureEnvMap` (6), `Diligent_Skinned` (4),
`Diligent_MRT` (4), `Diligent_OcclusionQuery` (4), `Diligent_MSAA` (5), `Diligent_Instanced` (4),
`Diligent_DrawOffset` (5), `Diligent_SetDataOptions` (4), `Diligent_VertexLit` (4) and
`Diligent_Pbr` (5). They clear, draw `SpriteBatch` quads and 3D primitives on the back buffer and
into off-screen 2D/cube targets, and assert on pixels (or query results) read back through
`GraphicsDevice.GetBackBufferData` / `RenderTarget2D.GetData` / `RenderTargetCube.GetData` /
`OcclusionQuery`. `Diligent_MSAA` uses a diagonal-edge differential (binary transition with MSAA
off vs. genuinely blended pixels with it on) rather than a solid-fill check, since a solid fill
can't tell "MSAA happened" apart from "MSAA was silently ignored". `Diligent_Instanced` draws
three instances of one quad at distinct per-instance translations from a single
`DrawInstancedPrimitives` call and asserts each instance's own position reads back the quad's
colour while the untouched background between them stays the clear colour. `Diligent_DrawOffset`
proves `DrawPrimitives`/`DrawIndexedPrimitives`/`DrawInstancedPrimitivesEx` honor non-zero
`vertexStart`/`startIndex`/`baseVertex` offsets rather than silently drawing from the start of the
bound buffer. `Diligent_SetDataOptions` proves a second, differently-coloured
`SetData(..., NoOverwrite)` upload into an already-`Discard`-uploaded `DynamicVertexBuffer`/
`DynamicIndexBuffer` genuinely reaches the GPU buffer rather than being dropped or leaving stale
content. `Diligent_VertexLit` proves `PreferPerPixelLighting=true` and `=false` render a
flat-normal quad pixel-identically for both `BasicEffect` and `SkinnedEffect`, the strongest
possible check since the two shading paths share the exact same lighting formula and only differ
in evaluation frequency. `Diligent_Pbr` uses the same analytically-hand-derived technique as
`vulkan_pbreffect_handderived_test.cpp` -- a flat quad viewed straight down its own normal with the
light aimed the same way collapses every BRDF dot product to exactly 1 at the backbuffer's centre
pixel, so the metallic-roughness shader reduces to a closed-form constant independently re-derived
in Python; three hand-derived `PbrEffect` cases (white/metallic=0, red/metallic=1, red/metallic=0)
all match their predicted RGB values exactly, and `SkinnedPbrEffect` with a single identity bone
(a mathematical no-op skin transform) reproduces the white/metallic=0 case's value exactly. All 69
pass against a real Vulkan device. On a machine with no usable device they exit 77 and print
`[SKIP] CNA Diligent smoke`, which CTest reports as a skip — reporting a pass with nothing rendered
would be dishonest.

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
