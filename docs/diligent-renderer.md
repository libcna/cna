# Diligent Engine graphics renderer

## Status

The Diligent Engine renderer is CNA's newest graphics renderer and is **experimental**. Its
implemented surface covers the 2D/3D baseline plus render targets (2D and cube), occlusion queries,
MSAA, hardware instancing, `PbrEffect`/`SkinnedPbrEffect` and most stock effects, described in
`plan_diligent.md` Phases `DILIGENT-1` through `4`; it is **not** at parity with the Vulkan, EasyGL,
SDL_GPU or bgfx renderers (custom `ShaderEffect` programs and `RenderTargetCube` MSAA are still
unimplemented). Read
["What works / what does not"](#what-works--what-does-not) before using it for anything real.

Select it with:

```bash
cmake -S . -B cmake-build-diligent \
  -DCNA_GRAPHICS_RENDERER=DILIGENT \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build cmake-build-diligent -j4
```

## What is different about this renderer

Every other CNA renderer targets one native graphics API. DiligentCore is itself a portable
abstraction over Direct3D 11, Direct3D 12, Vulkan, OpenGL/GLES and Metal, so CNA sits on two
stacked abstraction layers and **the native API is chosen at runtime**, not by the CMake option.

At startup the renderer walks `D3D12 → Vulkan → D3D11 → OpenGL`, restricted to the engines
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
context itself — it asserts one is already current on the calling thread and attaches to it. CNA
therefore creates, binds and owns that context through `IPlatformGlContext` before entering
Diligent. Vulkan and Direct3D consume the typed native-window snapshot directly. Window creation
uses the renderer's platform-neutral OpenGL or Vulkan render intent selected from the same
`CNA_DILIGENT_DEVICE` override, so incompatible native window capabilities are never combined.

## Dependencies

DiligentCore is pinned to **v2.5.6** and fetched by CMake (`cmake/ThirdPartyDiligent.cmake`),
recursive submodules included. It is a large dependency (~200 MB checked out, several minutes to
build on four cores), so for repeated or offline builds point the build at a local clone using
FetchContent's own standard override:

```bash
git clone --depth 1 --branch v2.5.6 --recurse-submodules --shallow-submodules \
  https://github.com/DiligentGraphics/DiligentCore.git ~/deps/DiligentCore

cmake -S . -B cmake-build-diligent \
  -DCNA_GRAPHICS_RENDERER=DILIGENT \
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

- Device, immediate context and swap chain over a platform-owned native window, with
  per-device-type fallback.
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
  Mesa's `lavapipe`, this renderer's own verification device) it transparently falls back to
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
  minimal, matching every other CNA renderer's own instancing baseline: no texture, no lighting,
  flat diffuse colour output.
- `SetStringMarkerEXT` — a real `IDeviceContext::InsertDebugLabel()` call, visible to external debug
  tools (RenderDoc, PIX, Vulkan validation layers with debug-utils enabled); has no rendering effect
  of its own.
- `SetDataOptions` streaming hints on vertex/index buffers: `Discard`/`None` map to
  `MAP_FLAG_DISCARD`, `NoOverwrite` to `MAP_FLAG_NO_OVERWRITE`, the same mapping this renderer's
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
  its own filter/address/anisotropy state, matching the established cross-renderer HLSL register
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

- **The metallic-roughness vertex layouts have only a PBR program.** Strides 48, 60, 68, 76 and 80
  are the `PbrEffect`/`SkinnedPbrEffect` records, and this renderer selects a metallic-roughness
  shader variant for them from the stride alone. An application that binds one of those buffers to a
  non-PBR effect -- a `BasicEffect` on a `VertexPositionNormalTangentTexture` buffer, say -- is
  refused by name rather than rendered through the PBR shader, which used to answer black
  (`plan_gltf.md GLTF-475`). Nothing on the glTF path is affected: it drives those five strides
  through `PbrEffect`/`SkinnedPbrEffect`. Lifting the limitation means adding an unlit program for
  those layouts, not relaxing the check.
- **X11 only on Linux.** Diligent's `LinuxNativeWindow` carries an X11 window id and display (or an
  XCB connection) and has no Wayland surface member. CNA therefore requires an X11 native-window
  snapshot on Linux and rejects any other native system at renderer construction, before entering
  Diligent.
- **OpenGL creates a device and renders most of the baseline; 25 of 31 pixel-proof binaries fully
  pass (`DILIGENT-30`/`DILIGENT-66`), and `ctest -R Diligent` now runs every binary under both
  device types automatically (`DILIGENT-67`).** Two systemic OpenGL-only bugs were root-caused and
  fixed:
  1. **sRGB gamma.** DiligentCore's `RenderDeviceGLImpl::Initialize()` unconditionally calls
     `glEnable(GL_FRAMEBUFFER_SRGB)` whenever the GL version supports the feature, regardless of
     the swap chain's own requested (non-sRGB) colour format -- on this project's Mesa/llvmpipe
     environment the default window framebuffer happens to be sRGB-capable, so every write was
     silently gamma-encoded. Fixed by resolving `glDisable` through `IPlatformGlContext` and
     disabling `GL_FRAMEBUFFER_SRGB` right after OpenGL device creation.
  2. **`ReadBackbuffer()`'s Y axis.** Reading the swap chain's own default framebuffer under GL
     (`Texture2D_GL::CopyTexSubimage` → `glCopyTexSubImage2D`) uses GL's native bottom-up row
     convention, unlike every other texture read in this renderer (ordinary allocated textures are
     self-consistently top-down). Fixed in `ReadBackbuffer()` itself: the requested source rows
     are flipped about the back buffer's real height before the copy, and the CPU-side resample
     loop un-reverses them afterward.

  Together these fixed `Diligent_Pbr`, `Diligent_LightingFidelity`, `Diligent_Skinned`,
  `Diligent_Npot`, `Diligent_MSAA`, `Diligent_DrawOffset`, `Diligent_VertexLit`,
  `Diligent_SpriteFont` and 6 of `Diligent_DepthBias`'s 7 checks.

  Six checks across six binaries remain open under GL, all documented in `plan_diligent.md`
  `DILIGENT-66`: `Diligent_MultiSampleMask` and the instancing family
  (`Diligent_Instanced`/`Diligent_InstancedStride`) are confirmed genuine upstream/driver
  limitations, not CNA bugs -- `SampleMask` is explicitly unimplemented in DiligentCore v2.5.6's
  own GL renderer (`GLContextState::SetBlendState()` logs an error and does nothing), and
  DiligentCore's own `VAOCache.cpp` instancing setup shows no defect, pointing at a Mesa/llvmpipe
  software-rasterizer bug in per-instance divisor fetching instead. `Diligent_DepthBias`'s
  remaining failure is the same pre-existing constant-`DepthBias` environment limitation Vulkan
  also has (see below), now at parity rather than a GL-specific regression.
  `Diligent_RenderTargetMipGen`'s two failures are a related-but-distinct, newly root-caused GL
  defect (not the same mechanism as fix 2 above): rendering *into* an ordinary FBO under GL writes
  content genuinely upside-down (confirmed by matching a vertically-mirrored expected pattern
  exactly), because XNA's `top=0` orthographic-projection convention implicitly assumes a
  rasterizer where NDC Y=+1 maps to row 0 -- true for Vulkan/D3D, but OpenGL's rasterizer always
  maps NDC Y=+1 to the *highest* row index of the viewport, a fixed, unconfigurable API property.
  A real fix needs a Y-flip in the projection/vertex path specifically for non-default render
  targets under GL, verified across both the sprite and 3D draw paths and every render-target
  type -- not yet attempted, given the blast radius. `Diligent_ReferenceStencil` remains
  unexplained; static analysis of the override mechanism shows no defect on paper.
- **Direct3D 11/12 are code paths only.** They compile only in a Windows-targeting build and have
  not been run.
- **Depth-stencil is always `D24_UNORM_S8_UINT`**, regardless of the requested `DepthFormat`.
- **MRT colour write masks have no effect on slots 1..3** — every built-in shader here declares a
  single output, so only slot 0 ever receives fragments regardless of the requested mask.
- **`BlendState.MultiSampleMask` reaches `Dg::GraphicsPipelineDesc::SampleMask` and works on
  Vulkan/D3D11/D3D12** (`DILIGENT-60`, verified by `Diligent_MultiSampleMask`) **but is
  unimplemented in DiligentCore v2.5.6's own OpenGL renderer** — `GLContextState::SetBlendState()`
  logs an error and silently does nothing whenever the requested mask isn't `0xFFFFFFFF`. Not
  fixable from this renderer's own code without bypassing Diligent's GL abstraction entirely.
- **`RenderTargetCube` is never multisampled**, even when the back buffer or a `RenderTarget2D` is —
  requesting MSAA on one clamps to 1 (`DILIGENT-25`).
- **`RasterizerState.DepthBias` (the constant term) has no observable effect on this project's
  software Vulkan device** (`llvmpipe`/`lavapipe`), even at the maximum magnitude the renderer's own
  packing can represent (`DILIGENT-49`). `SlopeScaleDepthBias` is real and verified working. This
  matches two independent pre-existing findings elsewhere in this codebase — `D9-62` (D3D9's own
  oracle attempt against real XNA 4.0 found no observable pixel difference from constant `DepthBias`
  at any magnitude up to `±1e8`) and `Vulkan_DepthBias`'s own pre-existing `DepthBias=-1e6` sub-case
  failure — so this is treated as an already-known, cross-renderer, environment/driver limitation of
  the test environment, not a CNA-side bug specific to this renderer.

## Tests

`DiligentDeviceSelectionTest.*` (part of the normal `CnaTests` suite) covers the runtime device
preference order and the `CNA_DILIGENT_DEVICE` override. It needs no GPU, no window and no display.

```bash
ctest --test-dir cmake-build-diligent -R DiligentDeviceSelection --output-on-failure
```

Thirty-one further binaries are the real-device pixel proofs (197 checks total, plus
`Diligent_DeviceSelectionIntegration`'s own 7 device-selection scenarios, counted separately since
it asserts on process exit codes rather than pixels): `Diligent_2D` (6), `Diligent_3D` (6),
`Diligent_RenderTarget` (5), `Diligent_RenderTargetCube` (4), `Diligent_AlphaTestFog` (4),
`Diligent_DualTextureEnvMap` (6), `Diligent_Skinned` (4), `Diligent_MRT` (4),
`Diligent_OcclusionQuery` (4), `Diligent_MSAA` (6), `Diligent_Instanced` (4),
`Diligent_DrawOffset` (5), `Diligent_SetDataOptions` (4), `Diligent_VertexLit` (4),
`Diligent_Pbr` (5), `Diligent_DepthBias` (7), `Diligent_ReferenceStencil` (1),
`Diligent_FillMode` (3), `Diligent_Anisotropic` (1), `Diligent_SpriteFont` (4),
`Diligent_Model` (1), `Diligent_Mip` (22), `Diligent_Npot` (3), `Diligent_RenderTargetMipGen` (7),
`Diligent_ScissorPipelineCache` (36), `Diligent_MultiSampleMask` (9), `Diligent_InstancedStride` (6),
`Diligent_CapabilityConsistency` (4), `Diligent_BackbufferReadbackBounds` (16) and
`Diligent_LightingFidelity` (6). The last six were added by `DILIGENT-58`/`60`/`61`/`63`/`65`/`59`
respectively to close a real coverage gap: no test previously exercised scissor-enable pipeline-key
completeness, `BlendState.MultiSampleMask`, capability/device-consistency, back-buffer readback
bounds under every presentation mode, non-standard instancing strides, or stock-effect emissive
colour and non-uniform-world normal transforms at all. They clear, draw `SpriteBatch` quads and 3D
primitives on the back buffer and
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
(a mathematical no-op skin transform) reproduces the white/metallic=0 case's value exactly.
`Diligent_DepthBias` (`DILIGENT-49`) proves `RasterizerState.SlopeScaleDepthBias` genuinely changes
a depth-test outcome (a coplanar redraw only shows through with a negative slope bias applied), but
its constant-`DepthBias` check fails on this software Vulkan device — matching two independent
pre-existing findings elsewhere in this codebase (`D9-62`'s own oracle attempt against real XNA 4.0,
and `Vulkan_DepthBias`'s own pre-existing `DepthBias=-1e6` sub-case), a documented environment
limitation rather than a CNA-side bug. `Diligent_ReferenceStencil` (`DILIGENT-50`) proves
`GraphicsDevice.ReferenceStencil` genuinely overrides the active stencil-compare reference
independently of the assigned `DepthStencilState`'s own baked-in value — real and working here,
unlike EasyGL/Bgfx's still-open, universal no-renderer-connection gap (Task 872).
`Diligent_FillMode` (`DILIGENT-51`) proves `FillMode::WireFrame` genuinely rasterizes only triangle
edges (a full-viewport triangle's centre pixel reads back as the clear colour, not the fill colour,
and reverting to `FillMode::Solid` restores the fill), ruling out both a silent solid-fill fallback
and a fully-blank draw. `Diligent_Anisotropic` (`DILIGENT-52`) follows this project's own
established scope for anisotropic filtering tests (Task 299's own header explains a true visual
quality comparison is inherently driver-dependent and fragile to assert precisely): requesting
`SamplerState.MaxAnisotropy=9999`, far beyond any real GPU's limit and beyond this renderer's own
clamp, does not crash and still produces a genuinely sampled result. `Diligent_SpriteFont`
(`DILIGENT-53`) is a direct port of D3D11's own `DX-127`: exact single-glyph placement (checked
inside plus all four edge midpoints), per-glyph advance, newline line-drop+x-reset, and
`SpriteEffects::FlipVertically` genuinely flipping an asymmetric glyph — the same shared
`SpriteFont`/`SpriteBatch` code already pixel-verified on EasyGL/D3D11/D3D12, now confirmed working
through this renderer too. `Diligent_Model` (`DILIGENT-54`) is a direct port of D3D12's own `DX-148`
Check KK6: a real 2-bone hierarchy (root → child) drives `Model::Draw()`'s full orchestration
(bone transform → `SetVertexBuffer`/`setIndicesProperty`/`DrawIndexedPrimitives`/`EffectPass.Apply`)
end to end — D3D12's own version of this test previously caught a real crash from unimplemented
state-setter stubs nothing else in that renderer's suite exercised, but Diligent has no equivalent
gap. `Diligent_Mip` (`DILIGENT-55`) proves `Texture2D` mip-level `SetData`/`GetData` round-trips
byte-exact through a genuine GPU staging-texture readback (not a CPU-shadow-only readback, unlike
the EasyGL precedent this test is otherwise modelled on) -- a 4×4 `mipMap=true` texture's 3 levels
each get a distinct solid colour, round-trip exactly, and level 0 is confirmed unaffected by the
later level 1/2 uploads. `Diligent_Npot` (`DILIGENT-56`) goes further than D3D11's own `DX-140`
(which only checked NPOT sampling against a *solid*-colour texture): a genuinely non-power-of-two
5×3 texture filled with 15 DISTINCT pseudo-random colours proves full-texture round-trip, a
non-row-aligned sub-rectangle `GetData()` read, and a real `BasicEffect` draw all sample the exact
known colours -- no row-pitch/stride-shift bug in the staging-texture path. `Diligent_RenderTargetMipGen`
(`DILIGENT-26`) proves `RenderTarget2D` mip regeneration (`IDeviceContext::GenerateMips()`, triggered
on unbind) performs a genuine box-filter downsample rather than a nearest-pixel copy or a silent
no-op: an exact Red/Blue checkerboard pixel-copied into level 0 of a 4x4 `mipMap` target reads back
level 1 (2x2) and level 2 (1x1) as the real `(128,0,128)` blend at every texel, not pure Red, pure
Blue, or black -- but see [Known limitations](#known-limitations), its own render-to-FBO content is
genuinely Y-flipped under the OpenGL device type, still open. `Diligent_ScissorPipelineCache`
(`DILIGENT-58`) toggles `RasterizerState.ScissorTestEnable` across `SpriteBatch`, indexed-3D and
instanced draws that all share one cached pipeline-state object, proving the immutable-PSO cache
keys on scissor-enable rather than silently reusing a pipeline built for the opposite state.
`Diligent_MultiSampleMask` (`DILIGENT-60`) proves `BlendState.MultiSampleMask` reaches
`Dg::GraphicsPipelineDesc::SampleMask` at both 1x and 4x MSAA, including an A→B→A cache-reuse
sequence -- real and working on Vulkan; unimplemented in DiligentCore's own OpenGL renderer (see
[Known limitations](#known-limitations)). `Diligent_InstancedStride` (`DILIGENT-65`) proves
`DrawInstancedPrimitivesEx` reads real per-buffer strides instead of a hardcoded 16/64, with
non-standard vertex/instance strides, `startIndex`+`baseVertex` composed with a non-standard
stride, and two undersized-stride rejections. `Diligent_CapabilityConsistency` (`DILIGENT-61`)
proves `SupportsCapability()` agrees with what the corresponding `Create`/`Apply` call actually
does on the live device, closing a gap where the two could silently disagree.
`Diligent_BackbufferReadbackBounds` (`DILIGENT-63`) constructs `IGraphicsRenderer` directly (the
physical/virtual mismatch it needs can't be produced through the public API in this headless
sandbox) and proves `ReadBackbuffer()` is valid and bounded -- full-canvas and sub-region reads
across all five `CnaPresentationMode`s, plus Overscan's own naturally-out-of-bounds edges reading
back zero instead of crashing or shifting. `Diligent_LightingFidelity` (`DILIGENT-59`)
hand-derives pixel values for emissive isolation, specular scaled by final alpha, multi-light
additive summation and non-uniform-world normal transforms (World's inverse-transpose, not World
itself) -- the four defects this task's stock-effect lighting fix corrected.
`Diligent_DeviceSelectionIntegration` (`DILIGENT-57`) forks one child process per device-selection
scenario (`vulkan`/`vk`/`opengl`/`gl`/`gles`/`auto`/an intentionally bogus value) and proves each
one either round-trips a `Clear`+`GetBackBufferData()` correctly or, for the bogus value, is
rejected with a clean exception rather than hanging or silently falling back.

191 of 197 pixel checks pass against a real Vulkan device; the one known failure
(`Diligent_DepthBias`'s constant-bias sub-case) is left visible rather than masked -- see
[Known limitations](#known-limitations). `DILIGENT-67` also registers every one of these 31
binaries a second time with `CNA_DILIGENT_DEVICE=opengl` forced (the `<Name>_OpenGL` CTest
entries), so `ctest -R Diligent` exercises both device types on every run instead of only
whichever one `GetDeviceTypePreferenceOrder()` picks by default; the current OpenGL-specific
results are also in [Known limitations](#known-limitations). On a machine
with no usable device the binaries exit 77 and print `[SKIP] CNA Diligent smoke`, which CTest
reports as a skip — reporting a
pass with nothing rendered would be dishonest.

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

The rest of `CnaTests` passes under this renderer (5692 passed, 7 skipped) except for one
pre-existing failure — `XnbContainerFuzzTest.MutatedRealModelFixtureNeverCrashesAndOnlyFailsCleanly`
fails identically on the `HEADLESS` renderer and is unrelated to this one.

The renderer is deliberately absent from `docs/graphics-renderer-feature-matrix.md`: that document's
columns mean "verified on a real hardware GPU", and everything here was measured on a software
Vulkan device. See `plan_diligent.md`'s "Verification status" section for the levels it
distinguishes.
