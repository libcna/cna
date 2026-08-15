# LLGL graphics renderer

## Authoritative post-audit contract (2026-08-09)

This section is the integration contract for `integration/post-audit-phase1` and supersedes the
broader historical implementation diary below wherever the two disagree. The diary is retained as
provenance for the original 68-commit lane; it is not a current capability claim.

- CNA exposes exactly one public renderer identity: `LLGL`.
- The supported route is `LLGL -> LLGL OpenGL RenderSystem -> native OpenGL/GLX`, on Linux/X11
  x86_64. It was runtime-tested on a dedicated Xvfb X11 server with Mesa llvmpipe.
- LLGL is pinned to `Release-v0.04b`, commit
  `1e78d8fa497f5cab76b231ba13f4d6249dac0e7e` (BSD-3-Clause). OpenGL is a required LLGL module.
  Vulkan and Null may remain compiled for coverage/diagnostics, but neither is a supported rendering
  fallback. `CNA_LLGL_RENDERER=auto` and `opengl` select OpenGL; an explicit `vulkan` request is
  rejected because the pinned path produced native validation errors. Null remains explicit
  lifecycle diagnostics and is never selected automatically.
- Platform scope is Linux/X11 only. Wayland, Windows and 32-bit architectures are not claimed by
  this CNA renderer. In particular, the historical i686 MinGW `__int128` failure came from Glide's
  mandatory x86 compatibility probe, not from an LLGL configure, build or test route. It is
  non-gating classification A; sharp-runtime is unchanged.

Current capabilities are deliberately narrower than upstream LLGL's API surface:

| Capability | Current LLGL contract |
| --- | --- |
| `ThreeD` | Supported and runtime-tested, including vertex/index offsets and stock effects. |
| `DepthStencilBuffer` | Depth attachment/test/write supported and runtime-tested. |
| `StencilBuffer` | Unsupported; enabling stencil rejects deterministically. |
| `MultiSampleAntiAliasing` | Not advertised. Back-buffer MSAA is clamped off on the supported OpenGL path. |
| `MultipleRenderTargets` | Runtime-tested for 2-4 `RenderTarget2D` slots through SpriteBatch/custom effects. Cube-face and mip-mapped MRT compositions reject. |
| `AnisotropicFiltering` | Supported when the measured LLGL device limit is greater than one. |
| `OcclusionQuery` | Supported and runtime-tested. |
| `CustomEffects` | Supported for the documented SpriteBatch GLSL path. |
| `Texture3D` | Upload/readback storage supported; shader sampling is not claimed. |
| `MultiStreamVertexInput` | Unsupported; more than one vertex stream rejects before submission. |
| `Instancing` | Unsupported; instance-frequency/instanced draws reject before submission. |
| `WireFrame` | Device-dependent and reported only when the active module exposes it. |
| `AdditiveBlending` | Supported and runtime-tested. Constant blend-factor states remain unsupported. |

`Texture2D` upload/readback, odd-width row handling, render-target sampling after unbind, mip-mapped
`RenderTarget2D`, MRT, viewport/scissor, ordered clears and deferred resource lifetime are gating
runtime paths. Plain `TextureCube` has exact per-face/per-mip transfer storage; cube shader sampling
and `RenderTargetCube` are outside the supported path and reject deterministically. `PbrEffect` is
runtime-covered; shader generation continues to use the checked-in authoritative GLSL sources and
the repository generator, with no generated artifact edited during integration.

The post-audit stream-array architecture is authoritative. LLGL consumes one geometry stream and
honours `VertexOffset`, `vertexStart`, `startIndex` and `baseVertex`; it does not restore removed
`GpuDrawParams` fields. Ordinary multistream and classic 1+1 instancing are unsupported and reject.

Post-audit findings:

- `LLGL-48`: resolved by retaining the complete blend-state identity in the primitive pipeline key;
  the shared colour-write-channel oracle is gating.
- `LLGL-52`: resolved on the supported OpenGL path; orthographic/CreateLookAt and indexed
  BasicEffect camera controls are gating.
- `LLGL-53`: resolved by a truthful boundary. Viewport/depth/render-target controls pass; non-zero
  depth bias and stencil are deliberately unsupported and reject instead of silently no-oping.
- `LLGL-54`: supported MRT compositions pass; cube-face and mip-mapped MRT combinations reject.
- `LLGL-55`/`LLGL-56`: X11/Xvfb and CNA-owned lifetime routes are covered. LeakSanitizer still
  reports narrowly classified allocations in pinned LLGL/SDL/Mesa GLX visual selection, not CNA.
- `LLGL-57`: resolved first-frame swap-chain/back-buffer extent drift after
  `GraphicsDevice::Reset` by synchronizing LLGL resolution at virtual-resolution and capture
  boundaries and invalidating readback cache on resize.
- `LLGL-58`: resolved a sanitizer finding in pinned LLGL's deferred OpenGL command buffer by
  supplying a valid pointer for its zero-count clear-value copy at both CNA render-pass call sites.

### Platform boundary

LLGL now receives a CNA-owned `LlglPlatformSurface` built only from `RendererSurfaceInfo`. On the
supported Linux route it translates the typed X11 display/window pair into `LLGL::NativeHandle`,
caches the drawable's real X11 visual for the adapter lifetime, and refreshes physical size and
display scale through `OnSurfaceChanged`. It never resolves or owns the platform window, and the
renderer target has no direct window-toolkit header, symbol or link dependency. Logical input
coordinates are scaled to physical drawable units before applying the presentation viewport.

This boundary remains deliberately X11-only because the pinned LLGL build's supported OpenGL/GLX
route is X11-only. A Wayland or other native handle is rejected immediately with both the expected
and received native systems in the error.

## Status

An **experimental** CNA graphics renderer built on [LLGL](https://github.com/LukasBanana/LLGL),
added 2026-07-31. Unlike every other renderer in this project it does not name a native graphics
API: LLGL is itself an abstraction layer, and the module it drives is selected when the process
starts.

What is implemented and verified today is the **2D pipeline, on both the Vulkan and the OpenGL
module**:

* platform-owned native window, `LLGL::RenderSystem`, swap chain (24-bit depth, 8-bit stencil), clear and
  present;
* virtual-resolution presentation (all five `CnaPresentationMode` policies share one code path;
  only `FixedHeightDynamicWidth` is pixel-verified) and window↔logical coordinate transforms;
* `Texture2D` upload per mip level and GPU readback;
* `SpriteBatch` with a per-blend-state pipeline cache and a per-sampler-state sampler cache,
  rotation, origin, both flips, tint, and the complete min/mag/mip triple for all nine XNA
  `TextureFilter` values;
* back-buffer readback, so `GraphicsDevice.GetBackBufferData` and the project's pixel tests work;
* the **3D path**: `VertexDeclaration` translation, real vertex and index buffer draws (with
  `vertexStart`, `startIndex` and `baseVertex` honoured), depth test and depth write, cull mode,
  and fill mode;
* **`BasicEffect` with one texture**, `DiffuseColor`, `Alpha`, vertex-colour modulation, fog,
  and **per-pixel directional lighting** (ambient, up to three lights, specular, `EmissiveColor`),
  textured or untextured, plus **`AlphaTestEffect`** and **`DualTextureEffect`**. Lighting still
  requires vertex colours when no texture is bound -- a lit, untextured, colourless draw is refused
  by name rather than silently dropping the light. Lighting also requires a vertex layout with
  normals regardless of texturing -- a lit, textured draw from a normal-less `VertexPositionTexture`
  layout throws rather than lighting with an implicit normal (see "Capability boundary" below);
* **`RenderTarget2D`**: draw into it (both `SpriteBatch` and the 3D path), unbind back to the
  swap chain, sample it back onto the screen, and `GetData()` straight off the colour attachment.
  See "Render targets" below for what this does and does not cover;
* **`RenderTargetCube`**: draw into any of its 6 faces independently, unbind, sample the result
  through `EnvironmentMapEffect`, and `GetData()` per face. See "Render targets" below;
* **occlusion queries**: real `LLGL::QueryHeap`-backed `OcclusionQuery`. See "Occlusion queries"
  below for how `IsComplete()`/`PixelCount()` behave on this renderer;
* **custom `ShaderEffect`s**, scoped to `SpriteBatch` draws. See "Custom effects" below for the
  runtime compile path and the uniform contract;
* **a real window resize** through `GraphicsDeviceManager.ApplyChanges()`, and **MSAA on the back
  buffer**, construction-time only and module-dependent — see the `MultiSampleAntiAliasing` row in
  "Capability boundary" below;
* **cube textures** (`TextureCube`): create, upload and read back all 6 faces and every mip level;
* **volume textures** (`Texture3D`): create, box-region upload and box-region read back. Not yet
  sampled from a 3D shader;
* **`EnvironmentMapEffect`**: cube-map reflections, its own dedicated vertex/fragment shader pair
  and pipeline layout (not the shared `Transform` block every other effect here uses). See
  "EnvironmentMapEffect" below;
* **`SkinnedEffect`**: GPU vertex skinning, up to 4 bone weight/index pairs, its own dedicated
  vertex/fragment shader pair and pipeline layout, plus a separate 72-bone transform buffer. See
  "SkinnedEffect" below;
* **multiple render targets (MRT)**: 2-4 `RenderTarget2D` slots bound simultaneously, written by a
  custom multi-output `ShaderEffect` drawn through `SpriteBatch`. See "Render targets" below for
  the scope boundary (`RenderTarget2D` slots only, no 3D draws while one is bound);
* **`PbrEffect`**: the glTF 2.0 metallic-roughness BRDF, 5 texture maps (base colour, normal,
  metallic-roughness, emissive, occlusion), its own dedicated vertex/fragment shader pair and
  pipeline layout;
* **`SkinnedPbrEffect`**: `PbrEffect`'s glTF BRDF over a GPU-skinned mesh, reusing `PbrEffect`'s
  own fragment shader verbatim (skinning is a vertex-stage-only concern) plus a new dedicated
  vertex shader and pipeline layout. See "PbrEffect" below;
* **MSAA render targets**: `RenderTarget2D`'s own `MultiSampleCount`, resolved into the target's
  colour texture before it is sampled or read back — unlike back-buffer MSAA, honoured on BOTH
  modules on this environment (a render target's sample count is read at its own construction, not
  gated by swap-chain construction timing). See "Render targets" below;
* **mip-mapped render targets**: `RenderTarget2D`'s own `mipMap` flag, a real mip chain
  regenerated with `LLGL::CommandBuffer::GenerateMips()` after every render pass the target
  appears in, and `GetData(level)` real for any level in range. See "Render targets" below.

Every item this renderer's design ever scoped for `RenderTarget2D` is now implemented; no
`GraphicsDevice.SupportsCapability()`/throw gap remains for it specifically (`RenderTargetCube`/MRT
still have their own smaller, documented scope boundaries -- see "Render targets" below).

Both modules are covered by their own CTests, so neither can break unnoticed because the default
preference happened to select the other one.

## Building

```bash
cmake -S . -B cmake-build-llgl -DCNA_GRAPHICS_RENDERER=LLGL -DCMAKE_BUILD_TYPE=Debug
cmake --build cmake-build-llgl -j4
```

CMake fetches LLGL at the pinned tag `Release-v0.04b`. For an offline or reproducible build, point
it at an existing checkout instead:

```bash
git clone https://github.com/LukasBanana/LLGL.git ~/deps/LLGL
git -C ~/deps/LLGL checkout Release-v0.04b
cmake -S . -B cmake-build-llgl -DCNA_GRAPHICS_RENDERER=LLGL -DCNA_LLGL_ROOT=$HOME/deps/LLGL
```

Which renderer modules get built:

| Option | Default | Notes |
| --- | --- | --- |
| `CNA_LLGL_BUILD_RENDERER_VULKAN` | `ON` when `find_package(Vulkan)` succeeds | LLGL marks its own Vulkan module experimental. |
| `CNA_LLGL_BUILD_RENDERER_OPENGL` | `ON` | Needs the usual GL/X11 development packages. |
| `CNA_LLGL_BUILD_RENDERER_NULL` | `ON` | Renders nothing; only reachable by explicit request. |

A module that was not built can never be selected, and configuring with neither OpenGL nor Vulkan
is a hard error.

### System packages (Debian/Ubuntu)

```bash
sudo apt-get install -y libgl1-mesa-dev libx11-dev libxrandr-dev libxext-dev libvulkan-dev
# LLGL-27: runtime GLSL->SPIR-V compile for custom ShaderEffects on the Vulkan module. Only the
# runtime .so is needed (no -dev package, no headers -- CMake hand-declares the small ABI subset
# it calls); libshaderc-dev also satisfies the same find_library() if it happens to be installed.
sudo apt-get install -y libshaderc1
# to regenerate the shader header (not needed for a normal build):
sudo apt-get install -y glslang-tools
```

## Choosing the renderer at runtime

```bash
CNA_LLGL_RENDERER=vulkan  ./your_game   # force one module, no fallback
CNA_LLGL_RENDERER=opengl  ./your_game
CNA_LLGL_RENDERER=null    ./your_game   # accepts every command, renders nothing (diagnostics only)
CNA_LLGL_RENDERER=auto    ./your_game   # the default: Vulkan, then OpenGL
```

An unknown value, or one naming a module this build does not contain, is rejected with an error
rather than silently falling back. The Null module is never part of the automatic chain.

The choice is probed once per process by actually loading the module, and the answer is cached:
`GraphicsDevice` needs it before the window exists (only the OpenGL module requires an
OpenGL-capable native window) and the renderer needs it again afterwards, and the two must agree.

`CNA_LLGL_DEBUG=1` additionally enables LLGL's own debug layer and routes its reports to stdout.
It validates every command and is far too costly to leave on, but it is the fastest way to find out
why a draw produced nothing.

## Platform support

X11 only. A Wayland native-window snapshot is refused before LLGL device creation: LLGL 0.04b
compiles Wayland support only when explicitly enabled and this integration does not enable it.
Handing LLGL a handle it cannot present to would be worse than saying so.

## Shaders

Both flavours are checked in and no shader toolchain is needed to build:

```text
src/Graphics/Renderers/Llgl/shaders/
  sprite2d.vert.glsl      sprite2d.frag.glsl        Vulkan flavour, compiled to SPIR-V
  sprite2d.gl.vert.glsl   sprite2d.gl.frag.glsl     OpenGL flavour, embedded as source
  colored3d.vert.glsl     textured3d.vert.glsl      3D vertex shaders, one per vertex layout
  colored_textured3d.vert.glsl                       (plus lit_*.vert.glsl for lighting,
  lit_textured3d.vert.glsl                            and a .gl. flavour of each)
  lit_colored_textured3d.vert.glsl
  lit_colored3d.vert.glsl                            lit, untextured-but-coloured (LLGL-31)
  untextured3d.frag.glsl  textured3d.frag.glsl      3D fragment shaders (alpha test + fog)
  lit_textured3d.frag.glsl                           (+ the lighting equation, .gl. too)
  lit_untextured3d.frag.glsl                         lit, untextured-but-coloured (LLGL-31)
  dual_textured3d.frag.glsl                          DualTextureEffect (reuses the plain
                                                       textured/colored_textured vertex shader --
                                                       no dedicated vertex shader of its own)
  env_map3d.vert.glsl     env_map3d.frag.glsl        EnvironmentMapEffect -- its OWN uniform
                                                       block (EnvMapParams), not the shared one
                                                       every other effect here uses
  skinned3d.vert.glsl     skinned3d.frag.glsl        SkinnedEffect -- its OWN uniform blocks
                                                       (SkinnedParams + a separate 72-bone
                                                       BoneBlock), not the shared one either
  effect3d_common.glsl.inc                           the uniform block they all share
  compile_shaders.py                                regenerates llgl_shaders.hpp
  llgl_shaders.hpp                                  generated; do not edit
```

After changing any shader:

```bash
python3 src/Graphics/Renderers/Llgl/shaders/compile_shaders.py
python3 src/Graphics/Renderers/Llgl/shaders/compile_shaders.py --check   # verifies freshness
```

Which flavour is used is decided from the shading language the loaded module reports, not from the
module's name — **and GLSL is preferred wherever a module offers it**. That order matters: a modern
OpenGL module reports SPIR-V as well (desktop GL ingests it through `GL_ARB_gl_spirv`), but the
SPIR-V here is compiled for Vulkan's binding model, and GL accepts it far enough to rasterize
geometry while silently zeroing every other attribute and the uniform block. That was `LLGL-17`.

Every unlit 3D shader (vertex AND fragment) declares the SAME 144-byte `Transform` uniform block,
even the ones that never read every field in it: OpenGL requires an identically named and laid-out
uniform block across every stage linked into one program, so a shader that is ever paired with
another one that reads `vertexColorEnabledPad` (offset 128) must declare that field too, whether or
not it uses it. The lit shaders don't need a separate field for the same flag -- they reuse
`ambientColorLighting.w`, since offset 128 becomes `worldMatrix` for them.

`GraphicsDevice.DrawUserPrimitives()`'s typed overloads (`VertexPositionColor`,
`VertexPositionTexture`, `VertexPositionColorTexture`, `VertexPositionNormalTexture`) work too
(`LLGL-32`): they route through `IGraphicsRenderer::CreateVertexBuffer(int)` (count-only, no
`VertexDeclaration`) and a raw byte `SetData`, so `LlglVertexBufferRenderer::ResolveVertexAttributes()`
infers the vertex layout from the upload stride instead -- 16/20/24/32/**48**/**52**/**68** bytes
are each a distinct, unambiguous size among `GraphicsDevice.cpp`'s own GPU-packed stream structs
plus `VertexPositionNormalTextureSkinned`'s own (stride 52, `SkinnedEffect`),
`VertexPositionNormalTangentTexture`'s own (stride 48, `PbrEffect`) and
`VertexPositionNormalTangentTextureSkinned`'s own (stride 68, `SkinnedPbrEffect`), the same
technique the Vulkan renderer's own `MakeExt3DKey()` already uses for these exact stream sizes. The
real-`VertexDeclaration` path (`MapVertexUsage()`) also maps `VertexElementUsage::BlendWeight`/
`BlendIndices`/`Tangent` now, at locations 4/5/6 -- `BlendIndices` binds as a genuine integer
vertex attribute (`LLGL::Format::RGBA8UInt`, read in GLSL as `uvec4`), not a normalized byte4.

## Render targets

`RenderTarget2D` draws into an off-screen colour (and always-allocated depth/stencil) attachment,
which is then either sampled back with `SpriteBatch`/the 3D path like any other `Texture2D`, or
read back directly with `GetData()`. `RenderTargetCube` draws into any of 6 faces independently --
one shared `TextureCube` colour texture (6 array layers) plus one shared depth/stencil texture
(matching FNA's own `RenderTargetCube`, one depth buffer for the whole cube), 6
`LLGL::RenderTarget`s built once at construction, each attaching a different `arrayLayer` of the
shared colour texture. A new `LlglBoundRenderTarget` common interface lets the renderer's "what's
currently bound" state point at either a plain `RenderTarget2D`, one cube face, or (see below) an
MRT bind, without any of the queue/replay code needing to know which -- the per-distinct-target
render-pass grouping described below already keys purely off `LLGL::RenderTarget*` pointer
identity, so this generalizes with no changes needed there. `RenderTargetCube`/MRT still do not
support mip-mapping (a `mipMap=true` cube or an MRT slot is silently created with a single level,
the same documented scope boundary `preserveContents` already has for both) -- see the dedicated
`RenderTarget2D` mip-mapping paragraph below for what a plain `RenderTarget2D` now does with it.

**`RenderTarget2D` `MultiSampleCount` is real.** `CreateRenderTarget2D`'s colour attachment uses
LLGL's anonymous (textureless) multisampled attachment pattern when `multiSampleCount > 1`: the
`RenderTargetDescriptor`'s `colorAttachments[0]` is left format-only (`texture == nullptr`) with
`samples` set to the request, and `resolveAttachments[0]` names the target's own real, sampleable
colour texture -- LLGL allocates and owns the internal MSAA buffer, resolving into the named
texture automatically at the end of each render pass. The applied count is read back from
`renderTarget->GetSamples()` after creation (a request LLGL cannot honour is silently reduced, not
rejected) and exposed through `RenderTarget2D.MultiSampleCount`/`GetMultiSampleCount()`, matching
this renderer's own back-buffer convention (0 = no MSAA, even though 1 is the internally stored "no
MSAA" sentinel). Every pipeline drawn against the currently bound target -- 3D, `SpriteBatch`, and
custom `ShaderEffect`s alike -- is now built with a `rasterizer.multiSampleEnabled`/render-pass
sample count that matches whatever is ACTUALLY bound (`LlglRenderer::GetPrimarySampleCountEXT()`),
not the swap chain's own, and the pipeline cache key folds the sample count in so a pipeline built
for one sample count is never reused for another -- see `Llgl_Msaa_RenderTarget`'s own CTest entry
below for the real bug this closed. Unlike the back buffer (`MultiSampleCount` is only ever
honoured at swap-chain/`GraphicsDevice` CONSTRUCTION time), a `RenderTarget2D`'s own sample count is
read at ITS OWN construction, so it works through the ordinary `Game` + `new RenderTarget2D(...)`
flow with no raw-`GraphicsDevice`-construction workaround needed, and — a genuine, positive
difference from back-buffer MSAA — is honoured on BOTH modules on this project's own test
environment, not gated behind the same Vulkan-only limitation `Llgl_Msaa` documents for the swap
chain. MRT binds do not support MSAA yet (`LlglBoundRenderTarget::GetSampleCount()` defaults to
1/no-MSAA); `RenderTargetCube` faces do now, see below.

**`RenderTargetCube` `MultiSampleCount` is real too (LLGL-34).** Colour follows the exact same
anonymous-attachment-plus-resolve pattern as `RenderTarget2D` above, just once per face (6 separate
`LLGL::RenderTarget`s, so LLGL allocates 6 independent, transient anonymous MSAA colour buffers --
never shared, which costs nothing extra since every one of them resolves into the cube's own
persistent, single-sample colour texture at the end of its own render pass anyway). The shared
depth/stencil texture (one for the whole cube, matching FNA's own convention -- see below) needed a
different treatment: LLGL requires a real, explicitly-referenced attachment texture to carry the
SAME sample count as the `RenderTargetDescriptor` using it, and `TextureDescriptor::samples` only
takes effect for `LLGL::TextureType::Texture2DMS`/`Texture2DMSArray` -- so the shared depth texture's
own type switches to `Texture2DMS` (instead of plain `Texture2D`) whenever MSAA is requested,
mirroring the Vulkan renderer's own `VulkanRenderTargetCubeRenderer::depthImage_`, "promoted to MSAA
samples when this cube engages MSAA". `RenderTargetCube.MultiSampleCount`/`GetMultiSampleCount()`
now report a real, device-clamped value (previously always 0), and every face's own
`LlglRenderTargetCubeFaceBinding::GetSampleCount()` reports it too, so pipelines drawn into an MSAA
cube face are built with the matching sample count exactly like an MSAA `RenderTarget2D`. Verified
by `Llgl_Msaa_RenderTargetCube`: on this project's own Vulkan module, MSAA into a cube face resolves
genuinely (a real antialiased edge, not merely "didn't crash") -- see "Tests" below.

**`BlendState.MultiSampleMask` is real (LLGL-33), module-dependent.** A `multiSampleMask_` member
(default `0xFFFFFFFF`, matching `BlendWriteState`'s own default) is set from
`ApplyBlendState`'s `writeState.multiSampleMask` and applied via
`pipelineDesc.blend.sampleMask` on every pipeline built afterwards -- 3D, `SpriteBatch`, and custom
`ShaderEffect` alike -- and folded into the sprite/custom-effect pipeline cache key
(`MakeBlendPipelineKey`) so a non-default mask is never reused for a draw that wants the default.
Confirmed by reading LLGL's own vendored source: the Vulkan module applies
`VkPipelineMultisampleStateCreateInfo::pSampleMask` unconditionally (harmless outside MSAA, since an
all-ones mask against one sample is a no-op); the OpenGL module's own `SetSampleMask` call is
permanently `#if 0`'d out, so it can never honour a sample mask at all, on any driver, on this
module -- a real, unfixable-from-CNA's-side limitation, not a bug. `Llgl_MultiSampleMask`/`_OpenGL`
detect this at runtime and report `[SKIP]` rather than `FAIL` for that one check, matching this
renderer's own established `Llgl_Msaa`/`Llgl_MRT` module-dependent precedent, since this renderer
picks its native module at runtime and a single compiled binary cannot express "Vulkan supports it,
OpenGL does not" through a compile-time flag the way single-native-API renderers can.
`AcquirePrimitivePipeline` (the 3D pipeline cache) has its own pre-existing, unrelated cache-key
limitation that discards several OTHER blend fields (not `MultiSampleMask`, which is unaffected --
see `known_bugs.md`'s open entry for the full writeup).

**`RenderTarget2D` `mipMap` is real.** `CreateRenderTarget2D`'s colour texture is allocated with a
full mip chain when `mipMap=true` (`RenderTarget2D.LevelCount`'s own `CalculateMipLevels(w, h)`
formula, computed identically here and matching the Vulkan renderer's own
`CalculateVulkanRTMipLevels`) -- the render-target ATTACHMENT itself still only ever binds level 0
(`LLGL::AttachmentDescriptor`'s own `mipLevel` default), and `RecordAndSubmitFrame()`/
`CaptureBackbuffer()` call `LLGL::CommandBuffer::GenerateMips()` on the colour texture right after
`EndRenderPass()` for any target that wants it -- the LLGL equivalent of the Vulkan renderer's own
`vkCmdBlitImage` cascade (`VulkanTargetPassEXT::MaybeGenerateMips`) and of EasyGL's
`glGenerateMipmap`-on-unbind, but a single built-in LLGL call instead of a hand-rolled blit loop.
Since a `RenderTarget2D` can be destroyed before the frame that references it is replayed, knowing
WHICH texture to regenerate at replay time is captured onto each `FrameCommand` at QUEUE time
(`mipRegenColorTexture`), mirroring `target`/`projectionBuffer`'s own existing pattern, rather than
looked up live from whatever is currently bound. `GetData(level)` is real for any level in
`[0, LevelCount)` now (previously a hard `level != 0` refusal) -- an out-of-range level throws
`System::NotSupportedException` through the shared `Texture2D::GetData` layer, same as any other
renderer readback failure. MRT binds do not support mip-mapping yet
(`LlglBoundRenderTarget::GetMipRegenColorTextureEXT()` defaults to null); `RenderTargetCube` faces
do now, see below.

**`RenderTargetCube` `mipMap` is real too (LLGL-35).** The shared cube colour texture is allocated
with a full mip chain when `mipMap=true` (`RenderTargetCube.LevelCount`'s own
`CalculateMipLevels(size)` formula, computed identically here), and each of the 6 per-face
attachments still only ever binds level 0, exactly like `CreateRenderTarget2D` above. One real LLGL
API constraint shapes how regeneration works here: `LLGL::CommandBuffer::GenerateMips(Texture&,
const TextureSubresource&)`'s own `baseArrayLayer` field is documented as ignored for a plain,
non-array `LLGL::TextureType::TextureCube` (only `TextureCubeArray` honours it), so there is no
cheaper, single-face-only regeneration call available -- every `GenerateMips()` call on the cube's
colour texture regenerates every face's own mip chain from that face's own current level-0 content,
regardless of which face's render pass just ended. `LlglRenderTargetCubeFaceBinding::
GetMipRegenColorTextureEXT()` (LLGL-35) therefore returns the SAME shared texture pointer for all 6
faces, so drawing into multiple faces in one frame calls the whole-cube regeneration once per face
bound -- redundant work (`Llgl_Mip_RenderTargetCube`'s own single-face test does not exercise this
multi-face cost directly) but not incorrect, since each call is a faithful, idempotent
regeneration of every face's own content. `RenderTargetCube::GetData(face, level, ...)` is real for
any level in `[0, LevelCount)` now (previously a hard `level != 0` refusal), matching
`RenderTarget2D::GetData(level)`'s own convention exactly.

Two implementation choices are worth knowing if you are debugging a render-target frame:

* **One render pass per distinct target, not per bind.** LLGL's public Vulkan API has no way to
  re-enter a render pass with `Load` semantics, so a frame that interleaves draws to the back
  buffer and one or more render targets is replayed as one pass per distinct target IDENTITY, in
  first-appearance order — every command for a given target is grouped together, not replayed in
  original interleaved order (`RenderTargetUsage.PreserveContents` is never actually READ by
  `CreateRenderTarget2D`/`CreateRenderTargetCube`, so it plays no explicit role in this). One
  consequence worth knowing precisely (LLGL-36 finding, cross-renderer `RenderTargetCube` oracle
  suite): as long as nothing FLUSHES the queued frame (`GetData()`, `Present()`) between two binds
  of the SAME target, this grouping means content genuinely accumulates across those binds — a
  "second bind" is not a literal, freshly-cleared second pass unless an explicit `Clear()` was
  actually queued in between (which only happens for a `DiscardContents` bind, at the shared XNA
  layer). This is an INCIDENTAL, not implemented-on-purpose, form of `PreserveContents` support —
  it does NOT survive a flush: once `frameCommands_` is submitted and cleared, the next bind of that
  same target starts a genuinely new, unpreserved pass. See `modules/graphics/examples/rendertargetcube_usage_test.cpp`/
  `rendertargetcube_msaa_face_test.cpp`'s own `CNA_RENDERER_LLGL` `Contract` branches (measured true,
  since neither file's own producer/marker draws are ever separated by a flush) versus
  `rendertargetcube_getdata_contract_test.cpp`'s own branch (measured false for its own U1/U2 checks
  specifically, which DO call `GetData()` between the two draws) for the full empirical picture.
* **Every render target shares the swap chain's own attachment formats.** The colour attachment
  always takes the swap chain's colour format, and a depth/stencil attachment matching the swap
  chain's own format is always allocated regardless of the requested `DepthFormat` (which only
  changes what `HasRealDepthBuffer()` reports). This is what lets every cached sprite/primitive
  pipeline — built once against the swap chain's render pass — be reused as-is for a render-target
  pass, instead of needing a second, render-target-keyed pipeline cache: Vulkan's render-pass-
  compatibility rule only requires matching attachment formats and sample counts, not the same
  `VkRenderPass` object. The 3D path (`BasicEffect`, depth-tested `VertexBuffer` draws) works into
  a render target too, not just `SpriteBatch` — both share the same reused pipelines.
* **`GraphicsDevice.Viewport` is applied per draw, not once per render-pass bucket (LLGL-39
  finding, FIXED — see `known_bugs.md`).** `ReplayFrameCommandsList()` now issues
  `commands_->SetViewport()` per `Clear`/`Primitives`/`Sprite` command using a physical-pixel
  rectangle `CaptureFrameCommandViewportEXT()` captured onto each `FrameCommand` at queue time —
  the whole target by default, narrowed only for `Primitives` when a custom `Viewport` was active,
  mirroring `ComputeEffectiveScissor`'s own narrowing. Sprites needed a separate, second fix:
  `QueueSpriteEXT()`'s CPU-baked geometry never added a custom `Viewport`'s own X/Y offset (only the
  scissor was narrowed to it), so it now translates by `viewportRect_[0]`/`[1]` before the existing
  letterbox scale, matching FNA's viewport-local `SpriteBatch` coordinate contract. A game that sets
  a DIFFERENT `Viewport` before each of several draws into the SAME target within one unflushed
  frame now gets each draw rasterized with its own viewport, not whichever was set last — verified
  by the same three test files that exposed the bug (`spritebatch_viewport_switch_test.cpp`,
  `spritebatch_custom_viewport_test.cpp`, `rendertargetcube_plural_binding_test.cpp`), all now fully
  passing under the default (Vulkan) module. A separate, OpenGL-module-only limitation (Y-offset
  scissor/viewport against the backbuffer renders nothing under `CNA_LLGL_RENDERER=opengl`) remains
  open — see `known_bugs.md`'s new entry.

Destroying a `RenderTarget2D` before `Present()` (create it, draw into it, sample it, let it go out
of scope, all within one `Draw()`) is safe: like `VertexBuffer`/`IndexBuffer`, the underlying LLGL
objects are released only once the frame that may still reference them has actually been
submitted. `RenderTarget2D::GetData()` also forces any of its own still-queued draws to be
submitted first — its content only exists once they are.
`RenderTargetCube`'s destructor does the same for its 6 face targets, releasing the shared colour
and depth textures exactly once (not 6 times) regardless of how many of the 6 faces were ever
drawn into.

**A plain `Texture2D`/`TextureCube`/`Texture3D` destroyed before `Present()` is safe too (LLGL-40
fix, previously a crash — see `known_bugs.md`).** All three now take the owning renderer at
construction and defer releasing their underlying `LLGL::Texture` through the same
`pendingTextureReleases_` pool `RenderTargetCube`'s own colour/depth attachments already used
(`ScheduleTextureReleaseEXT`, mirroring `ScheduleBufferReleaseEXT`), instead of releasing
immediately. Before this fix, drawing a locally-scoped `Texture2D` via `SpriteBatch` inside a
helper function and letting it go out of scope before the frame flushed (create it, draw it, let it
die, then `GetBackBufferData()`/`Present()` later in the same `Draw()`) segfaulted — the queued
`FrameCommand` still pointed at the now-freed texture. Found via
`backbuffer_readback_dimension_test.cpp`'s own A1 leg, the very first check to exercise exactly
that ordinary pattern.

Unlike `RenderTarget2D`'s anonymous (textureless) depth/stencil attachment, `RenderTargetCube`'s
shared depth/stencil buffer is a real, explicitly-owned `LLGL::Texture` — it has to be, since all 6
face `AttachmentDescriptor`s need to reference the SAME one, which an anonymous per-attachment
buffer cannot do. Sampling a `RenderTargetCube` through `EnvironmentMapEffect` (or anywhere else
that accepts an `ITextureCubeRenderer`) resolves through a new `ResolveSampledTextureCube()` helper
mirroring `ResolveSampledTexture()`'s own dual `LlglTextureRenderer`/`LlglRenderTargetRenderer`
resolution — a hard `dynamic_cast<const LlglTextureCubeRenderer*>` alone would have silently failed
to sample a rendered cube face.

### Multiple render targets (MRT)

`GraphicsDevice.SetRenderTargets` accepts 2-4 `RenderTarget2D` slots bound simultaneously, scoped
to a deliberately narrower first cut than this project's other MRT-capable renderers:

* **`RenderTarget2D` slots only.** Mixing a `RenderTargetCube` face into a multi-target set is
  refused by name rather than attempted.
* **Written only by a custom `ShaderEffect` drawn through `SpriteBatch`.** A 3D colour-only draw
  (`DrawPrimitivesEx`/`DrawIndexedPrimitivesEx`) while an MRT set is bound throws by name too --
  no stock effect family in this renderer declares more than one fragment output, and real XNA MRT
  is only meaningfully useful through a custom `layout(location=N) out`-per-slot fragment shader
  anyway.
* **`ColorWriteChannels1..3` are real** (LLGL-21 follow-up): each slot's own write mask applies
  independently. This needed `GraphicsPipelineDescriptor::blend.independentBlendEnabled = true`
  whenever more than one attachment is bound -- without it, LLGL silently reuses `blend.targets[0]`
  for every attachment regardless of what `targets[1..3]` were set to (confirmed by reading
  `VKGraphicsPSO.cpp`/`GLBlendState.cpp` directly). Module-dependent once that bug was fixed: the
  Vulkan module genuinely masks a non-zero slot on this environment; the OpenGL module's
  `glColorMaski` does not (a real GL driver constraint here, not a CNA defect) -- see
  `Llgl_MRT`'s own `[SKIP]`-gated check. `BlendState.MultiSampleMask` is now applied too (LLGL-33,
  see below) -- one mask shared by every attachment (XNA has only one, not one per MRT slot).

A new `LlglMRTBinding` combines the N bound targets' own colour textures (borrowed -- still owned
and released by the `RenderTarget2D` renderers that created them, never duplicated or double-freed
here) plus a fresh, anonymous depth/stencil attachment (matching `CreateRenderTarget2D`'s own
single-target depth attachment, rather than trying to share or preserve any one slot's own depth
buffer) into ONE `LLGL::RenderTarget`. Unlike `RenderTarget2D`/`RenderTargetCube`, an MRT bind has
no owning XNA-visible object -- `SetRenderTargets` just names N already-existing targets as slots
-- so `LlglMRTBinding` is owned by the renderer itself, replaced (and the previous one
deferred-released, exactly like a destroyed `RenderTarget2D` is) on every subsequent
`SetRenderTargets()`/`SetRenderTarget2D()` call rather than by RAII on a game-visible object.

Pipeline creation needed two changes to become MRT-aware: `GetPrimaryRenderPassEXT()` now returns
the CURRENTLY bound target's own render pass (a real, pre-existing `LLGL::RenderTarget::GetRenderPass()`
accessor) instead of always the swap chain's, since a multi-attachment bind's render pass genuinely
differs by attachment count; and both the sprite and custom-effect pipeline caches key on and build
against the active colour-attachment count, so a pipeline cached while 2 targets were bound is never
reused while 3 are.

## Occlusion queries

`OcclusionQuery.Begin()`/`End()` queue `LLGL::CommandBuffer::BeginQuery()`/`EndQuery()` into the
same deferred frame as everything else this renderer draws — LLGL requires both to be issued
inside an open render pass, which this renderer only opens at submit time.

Two things are worth knowing:

* **A fresh `LLGL::QueryHeap` is created for every `Begin()`, never reused.** LLGL 0.04b's own
  vendored Vulkan module never issues the `vkCmdResetQueryPool` a query needs before a second
  `vkCmdBeginQuery` on the same query index (the call exists in its source but is `#if 0`'d out),
  and reusing a query pool without an external reset LLGL does not expose would be undefined
  behaviour by the Vulkan spec's own query-reset rule. A fresh query pool is always in the valid
  "unavailable" state for its first use, so this sidesteps the gap entirely.
* **`IsComplete()`/`PixelCount()` answer synchronously.** The first call after `End()` forces a
  full submit-and-wait (the same `FlushPendingFrameEXT()` `RenderTarget2D::GetData()` uses) rather
  than genuinely polling across frames the way real hardware occlusion queries are meant to be
  used to avoid a CPU stall. This is a deliberate, documented trade of that performance
  characteristic for a result that is always immediately correct.

## Custom effects

`Microsoft::Xna::Framework::Graphics::ShaderEffect` (`CNAEXT`) compiles hand-authored GLSL vertex
and fragment source and draws through it. It is **scoped to `SpriteBatch` draws only** -- the
vertex shader is bound to the fixed sprite `position`/`texCoord`/`color` layout, not an arbitrary
`VertexDeclaration` a 3D draw might use, mirroring the native `VULKAN` renderer's own
`VulkanEffectRenderer` scope exactly rather than inventing a new limitation:

```cpp
ShaderEffect fx(device, vertexGlslSource, fragmentGlslSource);   // always real GLSL text
fx.SetUniformVec4("uColor", 1.0f, 0.0f, 0.0f, 1.0f);              // name accepted, not consulted
spriteBatch.Begin(SpriteSortMode::Immediate, BlendState::Opaque, nullptr, nullptr, nullptr, &fx);
```

Unlike the `VULKAN` renderer (which expects the caller to hand it pre-compiled SPIR-V, since it
names one fixed native API -- see `docs/shader-effect-vs-fx-bytecode.md`), `vertSrc`/`fragSrc` are
**always real GLSL text** here: this renderer picks its module at runtime, so the game has no
reliable way to know in advance which form to hand over. `CompileProgram()` (via the public
`ShaderEffect` constructor) hands the GLSL to LLGL directly when the loaded module accepts it
(OpenGL), or compiles it to SPIR-V first through a real runtime `libshaderc` call when it does not
(Vulkan) -- the same problem this project's `SDL_GPU` renderer already solved the same way.

Named-uniform setters (`SetUniformMat4`/`Vec4`/`Vec3`/`Vec2`/`Float`/`Int`) do **not** do real
name-based reflection -- LLGL exposes none for a raw GLSL/SPIR-V module, and adding one would need
a new dependency (SPIRV-Cross or similar). They map onto a fixed 32-float (128-byte) uniform block,
identical to the native Vulkan renderer's own documented `VulkanEffectRenderer::pushConst_` layout,
uploaded to a real constant buffer at binding 1 instead of a Vulkan push constant:

```glsl
layout(std140, binding = 1) uniform PC {
    vec4 vpSize_pad;  // xy = viewport/target size in pixels; set automatically, not by the game
    mat4 uMatrix;     // SetUniformMat4
    vec4 uColor;      // SetUniformVec4 / Vec3 (leaves w) / Vec2 (leaves z, w)
    vec4 uFloats;     // uFloats.x only -- SetUniformFloat / Int
} pc;
```

`name` is accepted (matching the shared `IEffectRenderer` signature every renderer implements) but
not consulted -- matching the same established precedent rather than inventing new semantics.
`colorMap`/`samplerState` (binding 2/3) sample the sprite's own texture, exactly as the stock
sprite shader does; there is no way yet to bind a second texture unit to a custom effect on this
renderer. See `modules/renderers/llgl/examples/llgl_shadereffect_test.cpp` for a complete worked example, including the
vertex shader's own pixel-to-NDC technique.

## EnvironmentMapEffect

Cube-map reflections. Unlike every other stock effect on this renderer, `EnvironmentMapEffect` does
NOT reuse the shared 100-float `Transform` uniform block or `AcquirePrimitiveVertexShader()`'s
variant selection -- its field set (Fresnel factor, environment map amount/specular; no per-light
specular, no alpha test) does not fit that layout, and its vertex/fragment pair
(`env_map3d.{vert,frag}.glsl`) is never linked with any other shader here. It gets its own:

* `primitiveEnvMapLayout_` pipeline layout: an `EnvMapParams` uniform buffer at binding 1,
  `colorMap`/`samplerState` (the diffuse texture) at 2/3, `envMap`/`envMapSampler` (the cube map)
  at 4/5;
* 84-float (336-byte) `EnvMapParams` per-draw uniform buffer pool (`envMapUniformBuffers_`/
  `envMapUniformData_`), grown and reused the same way `transformBuffers_`/
  `customEffectUniformBuffers_` are;
* dedicated vertex shader (`AcquirePrimitiveEnvMapVertexShader()`), computing a world-space normal
  (inverse-transpose of the world matrix) and eye vector for the fragment shader's reflection.

The fragment formula (ambient-free Lambertian light sum, `reflect(-eye, normal)`, cube sample
lerped with the base colour by a flat-or-Fresnel-weighted blend factor, both the base lerp target
AND the specular term scaled by the combined texture×diffuse alpha) is transliterated directly from
the Vulkan renderer's own `env_map3d.frag.glsl` -- itself the product of three previously-found
formula bugs documented in `docs/environmentmapeffect-support.md` (additive instead of lerp'd base
blend; an unscaled specular term; an unscaled base-lerp target) -- rather than re-derived from
scratch, so those same mistakes could not recur here.

**Both `Texture` and `EnvironmentMap` must be bound** -- there is no fabricated white-texture/cube
fallback for a null one (mirroring `DualTextureEffect`'s own established convention); `QueuePrimitives`
throws by name instead ("EnvironmentMapEffect needs both Texture and EnvironmentMap bound").

**Not tested on the OpenGL module in this project's own environment**: `CreateTextureCube` aborts
there with `"ValidateGLTextureType: ... LLGL::RenderingFeatures::hasCubeTextures not supported"` --
a genuine, pre-existing GLX/llvmpipe software-rasterizer limitation, not a regression in this
effect or in `CreateTextureCube` itself (cube textures were previously only ever exercised through
`CnaTests`' default Vulkan-preferred `TextureCubeTest`, never through a `CNA_LLGL_RENDERER=opengl`-
pinned CTest). No `_OpenGL` CTest variant is registered for `Llgl_EnvironmentMapEffect_
AlphaScaledLerp` for this reason.

## SkinnedEffect

GPU vertex skinning: up to 4 bone weight/index pairs blended per vertex against a per-draw bone
transform array. Like `EnvironmentMapEffect`, `SkinnedEffect` does NOT reuse the shared `Transform`
block or `AcquirePrimitiveVertexShader()`'s variant selection -- it gets its own:

* `primitiveSkinnedLayout_` pipeline layout: a `SkinnedParams` uniform buffer at binding 1, a
  SEPARATE `BoneBlock` (72 `mat4`s, 4608 bytes) at binding 2, `colorMap`/`samplerState` (the
  diffuse texture -- `SkinnedEffect` is always textured, unlike `BasicEffect`) at 3/4;
* two per-draw buffer pools: `skinnedUniformBuffers_`/`skinnedUniformData_` for the small
  92-float (368-byte) parameter block, and a SEPARATE `skinnedBoneBuffers_`/`skinnedBoneData_`
  pool for the 72-`mat4` bone array -- kept apart because the bone array is far larger than every
  other per-draw uniform block here, mirroring the Vulkan renderer's own `BoneBlock`/`FogParams`
  UBO split;
* dedicated vertex shader (`AcquirePrimitiveSkinnedVertexShader()`), reading two extra vertex
  attributes (`aBoneWeights` at location 4, a plain unnormalized `float4`; `aBoneIndices` at
  location 5, a genuine INTEGER attribute -- `LLGL::Format::RGBA8UInt`, read in GLSL as `uvec4`,
  not a normalized byte4).

The skinning formula blends up to `WeightsPerVertex` (1, 2, or 4) bone matrices, gated at runtime
by `weightsPerVertex >= 2.0`/`>= 4.0` rather than a compile-time-unrolled per-bone-count shader
variant the way real XNA compiles 9 distinct permutations -- matching this project's own
established simplification (Task 895). The skinned position feeds both `gl_Position` and the fog
factor (dotted against the POST-skin position, not pre-skin), and the bone-skin 3x3 is composed
with the outer world inverse-transpose normal matrix before lighting, so a rotated or
non-uniformly-scaled skinned model lights correctly. Both are transliterated directly from the
Vulkan renderer's own `skinned3d.vert.glsl`. The lighting formula (per-light Lambertian diffuse +
Blinn-Phong specular) pre-folds `AmbientLightColor*DiffuseColor` into `EmissiveColor` on the CPU
side exactly like `EnvironmentMapEffect` does (confirmed by reading `SkinnedEffect::FillGpuDrawParams`
directly), not `BasicEffect`'s separate-ambient-term convention.

**Two real, independent vertex-layout gaps were closed to add this** (found by reading the code,
not by a failing test): `MapVertexUsage()` (the real-`VertexDeclaration` attribute-mapping path)
had no cases for `VertexElementUsage::BlendWeight`/`BlendIndices` at all, and
`LlglVertexBufferRenderer::ResolveVertexAttributes()`'s declaration-less stride-inference switch
(`LLGL-32`) had no case for stride 52 (`VertexPositionNormalTextureSkinned`'s own GPU-packed size)
-- every `SkinnedEffect` pixel test drives it through exactly that path
(`VertexBuffer::SetDataRaw`), so the second gap would have thrown "this vertex layout is not
supported" without the fix.

**`Texture` must be bound** -- there is no fabricated white-texture fallback for a null one
(mirroring `DualTextureEffect`/`EnvironmentMapEffect`'s own established convention);
`QueuePrimitives` throws by name instead ("SkinnedEffect needs Texture bound").

Unlike `EnvironmentMapEffect`, `SkinnedEffect` needs no cube texture, so it works cleanly on the
OpenGL module too -- `Llgl_SkinnedEffect_IdentityBones`/`Llgl_SkinnedEffect_TwoBoneBlend` are
registered on both modules. Both tests were ported (not verbatim-shared, but adapted with only the
class name/comment changed) from the Vulkan renderer's own `examples/vulkan_skinnedeffect_*_test.cpp`,
which are already fully renderer-agnostic real-XNA-API code.

**`SkinnedEffect.VertexColorEnabled` is real too (LLGL-37, `CNAEXT` extension property; real XNA has
no such property -- CNB-66/67 added it project-wide for glTF `COLOR_0` import support).** A stride-56
vertex layout (the stride-52 layout above with a colour attribute APPENDED at offset 52, location 1
per this renderer's own `MapVertexUsage()` mapping, matching `ResolveVertexAttributes()`'s own
"append rather than insert" convention for every other colour-carrying layout here) selects a
SEPARATE compiled shader pair -- `shaders/skinned3d_color.vert/frag.glsl` + `.gl.` variants -- instead
of the plain `skinned3d.vert/frag.glsl` above, mirroring `AcquirePrimitiveVertexShader()`'s own
per-layout-shape shader-file-selection convention rather than EasyGL's single shader with an
always-declared, conditionally-read attribute. The enable/disable gate reuses `emissiveColorPad.w`
(otherwise unused, since `SkinnedEffect` pre-folds ambient into `EmissiveColor.xyz` and has no
separate ambient term of its own to occupy the fourth component) -- the same free-slot-reuse trick
`BasicEffect`'s own `ambientColorLighting.w` uses -- written unconditionally by `FillSkinnedUniforms`
(harmless for the plain shader, which never reads it). Modulation order matches every other renderer's
own implementation exactly: vertex-colour alpha multiplies into the combined output BEFORE the
specular highlight is added, vertex-colour RGB multiplies the WHOLE combined diffuse+specular output
AFTER it, so `VertexColorEnabled=true` with a pure black vertex colour genuinely zeroes the pixel
rather than leaking an unmodulated specular term through. `Llgl_SkinnedEffect_VertexColor`/`_OpenGL`
verify this with an analytically-derived straight-on camera/light case (not a golden image) --
4/4 PASS on both modules.

**Out of scope**: real XNA's `PreferPerPixelLighting` selecting a genuinely different, per-vertex
(Gouraud) lit shader -- this renderer is per-pixel-lit only, matching every established CNA renderer
except D3D9 (`GpuDrawParams::preferPerPixelLighting`'s own documented deviation). `SkinnedPbrEffect`
(stride 68, `PbrEffect` combined with skinning) is done too -- see "PbrEffect" below.

## PbrEffect

The glTF 2.0 metallic-roughness BRDF (`PbrEffect`, `CNAEXT` -- real XNA predates the PBR content
pipeline). Like `EnvironmentMapEffect`/`SkinnedEffect`, gets its own dedicated resources rather
than reusing the shared `Transform` block:

* `primitivePbrLayout_` pipeline layout: an 84-float (336-byte) `PbrParams` uniform buffer at
  binding 1, then 5 texture/sampler pairs at bindings 2-11 -- base colour, normal map,
  metallic-roughness map (glTF packing: G=roughness, B=metallic), emissive map, occlusion map;
* dedicated vertex shader (`AcquirePrimitivePbrVertexShader()`), needing a NEW vertex element this
  renderer never had before: `VertexElementUsage::Tangent` (`MapVertexUsage`'s new case, location
  6) -- the tangent-space TBN basis the fragment stage builds for normal mapping. A new stride-48
  (`VertexPositionNormalTangentTexture`) case was added to `ResolveVertexAttributes()`'s
  declaration-less fallback switch too, mirroring `LLGL-32`'s own stride-inference precedent;
* one per-draw buffer pool (`pbrUniformBuffers_`/`pbrUniformData_`) for the `PbrParams` block,
  same growth/reuse discipline as `envMapUniformBuffers_`/`skinnedUniformBuffers_`.

`PbrLight()` (GGX distribution, Smith-Schlick-GGX visibility, Schlick Fresnel -- the glTF 2.0
spec's own reference BRDF) is transliterated directly from the Vulkan renderer's own already-correct
`pbr3d.frag.glsl`, applying the fog-mix convention fix learned from `EnvironmentMapEffect`'s own
bug from the start (`mix(rgb, fogColor.rgb, vFogFactor)`, this renderer's "how much fog" convention)
rather than needing to rediscover it. Base colour factor and alpha are kept independent (not
premultiplied), matching glTF's own `baseColorFactor` convention rather than most other CNA stock
effects' `DiffuseColor`.

**Unlike `EnvironmentMapEffect`/`SkinnedEffect`'s "throw if the required texture is missing"
convention, PbrEffect's 4 optional maps resolve to a 1x1 default texture instead of throwing** --
`EnsureDefaultPbrTexturesEXT()` lazily creates an opaque white texture (used for
`MetallicRoughnessMap`/`EmissiveMap`/`OcclusionMap` when null) and an RGBA(128,128,255,255) flat
normal texture (decoding to tangent-space (0,0,1), used for `NormalMap` when null), mirroring the
Vulkan renderer's own `EnsureDefaultWhiteTexture`/`EnsureDefaultFlatNormalTexture` precedent -- real
`PbrEffect::FillGpuDrawParams()` can legitimately leave all 4 null (only `Texture`/
`MetallicFactor`/`RoughnessFactor` are required), so throwing would incorrectly reject a valid,
minimally-configured draw. `Texture` (base colour) is still required and throws by name if missing
("PbrEffect needs Texture bound"), matching every other stock effect's own precedent. All 5 texture
units share this renderer's one global sampler state (`ApplySamplerState` only ever tracks slot 0)
-- the SAME `LLGL::Sampler` object is bound at all 5 sampler slots, since each GLSL `sampler2D`
declaration still needs its own binding even when the underlying resource is identical.

`Llgl_PbrEffect_HandDerived` (+ `_OpenGL`) is adapted from the Vulkan renderer's own
`modules/renderers/vulkan/examples/vulkan_pbreffect_handderived_test.cpp` (itself fully renderer-agnostic real public XNA
API + `VertexBuffer::SetDataRaw`), drawing into an off-screen `RenderTarget2D` read back with
`GetData()` instead of the source's own hand-rolled `Game` subclass that resizes the whole window
-- `PixelTestGame`'s `Game` construction has no equivalent hook, and reading a hard-coded small
pixel address directly off the (much larger) default back buffer sampled a world position over a
full unit away from the coordinate origin the analytic derivation assumes (found via a debug
shader pass outputting `vWorldPos` directly -- a test-authoring mistake, not a renderer defect).

### SkinnedPbrEffect

`PbrEffect`'s glTF BRDF over a GPU-skinned mesh -- `SkinnedEffect`'s own weightsPerVertex-gated
bone blend applied to position/normal/tangent before the same `PbrLight()` fragment stage runs.
The key design choice: `BoneBlock` (72 `mat4`s) is placed at binding **12**, deliberately AFTER
every PBR texture/sampler pair (bindings 1-11, byte-for-byte identical to `primitivePbrLayout_`),
rather than shifting them to make room. That means **`primitivePbrFragmentShader_` is reused
verbatim, unchanged** -- skinning is entirely a vertex-stage concern, so only a new vertex shader
(`AcquirePrimitivePbrSkinnedVertexShader()`, `pbr3d_skinned.vert.glsl`/`.gl.vert.glsl`) and pipeline
layout (`primitivePbrSkinnedLayout_`) are needed; had the shared bindings shifted instead, the
already-compiled fragment shader binary would no longer match the new layout's binding numbers.

`PbrParams`' own `roughnessWeightsPad.y` field was reserved for `WeightsPerVertex` from
`PbrEffect`'s own first cut (documented there as "unused by this shader"), so `FillPbrUniforms()`
needed zero changes to support skinning -- it already wrote `params.weightsPerVertex`
unconditionally. The bone transform buffer pool (`skinnedBoneBuffers_`/`skinnedBoneData_`,
`FillSkinnedBoneData()`) is reused verbatim from plain `SkinnedEffect` too, since bone data is
entirely effect-agnostic -- 72 `mat4`s mean the same thing regardless of which fragment shader
samples the result. A new stride-68 (`VertexPositionNormalTangentTextureSkinned`) case was added
to `ResolveVertexAttributes()`'s fallback switch (the stride-48 PBR layout with the stride-52
skinning suffix appended); no new vertex-attribute locations were needed, since `Tangent`(6)/
`BlendWeight`(4)/`BlendIndices`(5) were already reserved by `PbrEffect`/`SkinnedEffect`.

`Llgl_PbrEffect_HandDerived`'s own Check (d) (ported back from the Vulkan source, no new test
file) proves a single identity bone (weight 1.0, default `Matrix.Identity` -- a mathematical no-op
skin transform) reproduces `PbrEffect`'s own Check (a) value exactly, on both modules.

## Tests

```bash
ctest --test-dir cmake-build-llgl -R Llgl --output-on-failure
```

`Llgl_Smoke` covers the device/window/swap-chain lifecycle, buffer round-trips, and 60 frames of
clear and present. `Llgl_2D` asserts real pixels read back from the GPU: quadrant orientation
(where a Y-flip mistake shows up immediately), tint multiplication, `SpriteEffects` flipping, and
`NonPremultiplied` alpha blending. `Llgl_TextureReadback` round-trips texture uploads byte-exactly,
`Llgl_Presentation` covers the five presentation policies, `Llgl_3D` covers the 3D draw path
(vertex colours, depth ordering, indexed draws, cull mode, wireframe), `Llgl_BasicEffect` covers
textures, tinting, alpha, fog and the alpha test, `Llgl_Lighting` covers ambient/directional/
specular/emissive lighting both with and without a texture bound, and `Llgl_RenderTarget` covers
drawing into a `RenderTarget2D`, unbinding back to the swap chain, sampling the target back onto
the screen, and `GetData()`.
`Llgl_OcclusionQuery` covers a fully visible quad, a fully occluded one (real depth test), and two
draws inside one `Begin()`/`End()` summing their contributions. `Llgl_ShaderEffect` covers a
custom GLSL shader genuinely tinting a sprite by its own uniform, against a stock-shader control
case that must not show the tint. `Llgl_Resize` covers a real window resize driven through
`GraphicsDeviceManager.ApplyChanges()` (growing, shrinking, and a `Letterbox` presentation rect
recomputing from the resized window), settled with `SDL_SyncWindow()` before each post-resize read
since `SDL_SetWindowSize()` is not guaranteed synchronous under X11. `Llgl_Msaa` covers a genuinely
antialiased diagonal edge against a hard, unblended one with MSAA off, using two raw
`GraphicsDevice` objects constructed directly (MSAA is construction-time only on this renderer, so
neither `Game`'s eagerly-constructed device nor `ApplyChanges()` can reach it) — the sample-count-
dependent checks report `[SKIP]` on a module that does not apply MSAA to the default framebuffer at
all (the OpenGL module, on this project's own test environment) rather than failing. `Llgl_DualTexture`
covers `DualTextureEffect`'s `VertexColorEnabled`-gated tint and proves the two textures sample
independently (a white base plus a red overlay must read back red, not white).
`Llgl_DualTextureEffect_VertexColor` and `Llgl_GraphicsDevice_DefaultStateOcclusion` are pre-existing,
cross-renderer shared sources (already registered on EasyGL/Vulkan/Bgfx) reused verbatim once
`LLGL-32` made `DrawUserPrimitives()` work on this renderer, exercising it through two of its four
recognised upload strides. `Llgl_EnvironmentMapEffect_AlphaScaledLerp` is another such reused
source, covering `EnvironmentMapEffect`'s alpha-scaled cube-map base lerp (Task 891's fix); see
"EnvironmentMapEffect" above for why it has no `_OpenGL` twin (a genuine `hasCubeTextures`
limitation of this project's own OpenGL module, not a gap in this renderer).
`Llgl_SkinnedEffect_IdentityBones`/`Llgl_SkinnedEffect_TwoBoneBlend` are ported (not verbatim, but
adapted with only the class name/comment changed) from the Vulkan renderer's own sources -- see
"SkinnedEffect" above. `Llgl_SkinnedEffect_VertexColor` (LLGL-37) is adapted the same way from
`modules/renderers/vulkan/examples/vulkan_skinnedeffect_vertexcolor_test.cpp`'s own analytically-derived technique (checks
(a)/(b)/(c) only; that file's own (d)/(e) are an unrelated Vulkan dynamic-blend-factor finding) --
plain `SkinnedEffect` works on both modules, so this gets an `_OpenGL` twin too.
`Llgl_RenderTargetCube` covers 6 independent per-face draw/`GetData()` round
trips plus sampling the result through `EnvironmentMapEffect`; like the `EnvironmentMapEffect` test
it has no `_OpenGL` twin, for the same `hasCubeTextures` reason.
`Llgl_Msaa_RenderTargetCube` (LLGL-34) reuses `Llgl_Msaa_RenderTarget`'s own diagonal-edge technique
against one cube face (`CubeMapFace::PositiveX`) instead of a plain `RenderTarget2D`: a hard,
unblended edge with `MultiSampleCount=0`, and (module-dependent, like `Llgl_Msaa_RenderTarget`) a
genuinely blended edge once MSAA is requested and honoured -- on this project's own test
environment it passes all 7 checks on the Vulkan module, the same module every other
`RenderTargetCube` test here already depends on; no `_OpenGL` twin, same `hasCubeTextures` reason.
`Llgl_Mip_RenderTargetCube` (LLGL-35) reuses `Llgl_RenderTarget2D_Mip`'s own asymmetric-split
technique against one cube face: a 7:1 red/blue split rendered into a `mipMap=true`
`RenderTargetCube`, read back directly at level 0 (crisp), an intermediate level that is an EXACT
1:1 downsample of the source pattern, and the coarsest 1x1 level (the whole image's true weighted
average) -- proving the downsample cascade computed the right content, not just the right
dimensions, and that a non-mipmapped cube (and an out-of-range level on a mipmapped one) both
correctly reject `GetData()`; no `_OpenGL` twin, same `hasCubeTextures` reason.
`Llgl_RenderTargetCube_GetDataContract`/`Llgl_RenderTargetCube_Usage`/`Llgl_RenderTargetCube_MsaaFace`
(LLGL-36) are pre-existing, shared, cross-renderer `RenderTargetCube` oracles (already registered on
EasyGL/Vulkan/Bgfx/etc) newly wired up here: byte-exact face readback with correct row order and
orientation, `RenderTargetUsage` behaviour (see "Render targets" above for the `PreserveContents`
finding these uncovered), and per-face MSAA isolation (this renderer has none of the
shared-multisample-attachment aliasing defect REMED-GFX-141 fixed elsewhere, since every cube face
is already its own distinct `LLGL::RenderTarget`) -- 56/30/32 checks pass respectively; no `_OpenGL`
twin on any of the three, same `hasCubeTextures` reason.
`Llgl_MRT` covers a real 2-output custom `ShaderEffect` writing two DIFFERENT values to two
simultaneously bound `RenderTarget2D` slots from the SAME draw call, a 3D colour-only draw throwing
while the MRT set is bound, back-buffer isolation, that an ordinary single-target draw still works
correctly once the MRT bind ends, and (LLGL-21 follow-up) that `BlendState.ColorWriteChannels1`
genuinely masks slot 1 independently of slot 0 within one bind cycle -- module-dependent, `[SKIP]`
on the OpenGL module rather than failed, since its `glColorMaski` does not honour the mask on this
environment. Unlike `RenderTargetCube`, plain `RenderTarget2D` slots work on both modules, so it
has an `_OpenGL` twin.
`Llgl_PbrEffect_HandDerived` covers the glTF metallic-roughness BRDF against hand-derived analytic
values at a fully dot-product-aligned pixel -- full dielectric, fully metallic, a control case
proving `MetallicFactor` genuinely changes the result, and a `SkinnedPbrEffect` identity-bone check
reproducing the same BRDF value through a GPU-skinned mesh; like `Llgl_MRT`, plain
`VertexPositionNormalTangentTexture` (stride 48) works on both modules, so it has an `_OpenGL` twin.
`Llgl_Msaa_RenderTarget` covers the same genuinely-antialiased-diagonal-edge technique as `Llgl_Msaa`,
but against an off-screen `RenderTarget2D` instead of the back buffer -- a single ordinary
`PixelTestGame` device suffices (no raw-`GraphicsDevice`-construction workaround needed, since a
render target's own `MultiSampleCount` is read at its own construction), and unlike `Llgl_Msaa` the
blended-edge check is not module-dependent here: it genuinely passes on both modules.
`Llgl_RenderTarget2D_Mip` covers a real, correctly downsampled mip chain: a 7:1 asymmetric red/blue
split rendered into a `mipMap=true` `RenderTarget2D`, read back directly at level 0 (crisp), an
intermediate level that is an EXACT 1:1 downsample of the source pattern (still crisp, proving the
cascade computed the right content, not just the right dimensions), and the coarsest 1x1 level
(the whole image's true weighted average, proving the cascade runs all the way to the top) --
adapted from `vulkan_rendertarget2d_mip_test.cpp`'s own technique but reading levels back directly
via the now-real `GetData(level)` instead of forcing GPU LOD selection through an extreme
minification draw. Also covers `GetData()` correctly rejecting a level outside `LevelCount`, on
both a mip-mapped and a plain target.
`Llgl_MultiSampleMask` (LLGL-33) covers `BlendState.MultiSampleMask` against a genuinely
multisampled `RenderTarget2D`: an Opaque-blended full-target quad over a differently-coloured clear,
the default all-ones mask resolving normally, and `MultiSampleMask=0` resolving to the clear colour
on a module that honours it -- module-dependent, `[SKIP]` on the OpenGL module rather than failed,
since its sample-mask application is permanently disabled in vendored LLGL (see "Render targets"
above).
Eight more shared, cross-renderer tests (already registered on EasyGL and usually several other
renderers) were newly wired up here (`plan_llgl.md` Phase LLGL-7, `LLGL-39`):
`Llgl_RenderTarget2D_DepthBuffer` (a `RenderTarget2D`'s own depth buffer really gates draws, not
just stores a property), `Llgl_RenderTarget_ViewportScissorReset` (binding/unbinding a render
target resets `Viewport`/`ScissorRectangle` to the new target's/back buffer's size),
`Llgl_SkinnedEffect_LightingConformance` (analytic ambient/emissive lighting term isolation),
`Llgl_ViewportResetAfterResize` (a backbuffer resize resets `Viewport` unconditionally),
`Llgl_GraphicsDevice_ClearDepth` (`Clear()`'s own `depth` parameter genuinely reaches the depth
buffer), and three more that initially failed and are covered above under "`GraphicsDevice.Viewport`
is applied per draw" -- `Llgl_RenderTargetCube_PluralBinding`, `Llgl_SpriteBatch_CustomViewport`,
`Llgl_SpriteBatch_ViewportSwitch` (all fully passing under the default/Vulkan module; no `_OpenGL`
variant, see `known_bugs.md`'s new OpenGL-Y-offset-scissor entry). Two OTHER files from the same
batch remain deliberately unregistered after being found to genuinely fail here for real, unrelated,
identified reasons -- see `known_bugs.md`'s open entries for
`rasterizerstate_cullmode_indexed_basiceffect_test.cpp` and
`rasterizerstate_cullmode_camera_test.cpp`.
Three more, from Phase LLGL-7's `LLGL-40` back-buffer batch, are wired up too:
`Llgl_BackBuffer_PassOrder` (30/30, the swap-chain-bucket-ordering fix above),
`Llgl_BackBuffer_ReadbackDimension` (8/8, the texture-lifetime fix above -- `GetBackBufferData`'s
required element count is authoritative regardless of viewport/round-trips/resize) and
`Llgl_BackBuffer_HeadlessReject`'s own `LLGL` Contract branch (12/12, needed no fix). A fourth file
from the same batch, `backbuffer_first_read_test.cpp`, stays unregistered: 9/13 legs pass, but its
own row-pitch matrix (widths 63/64/65 against a fixed height of 17) and one more (64x32) hit the
open `FixedHeightDynamicWidth` logical-width finding in `known_bugs.md`.
Phase LLGL-7's `LLGL-41` `RenderTarget`/`RenderTargetCube` batch adds `Llgl_RenderTarget_
PassBoundary` (43/43 -- `segmentsBindCycles` reads true even though buckets group by target
identity, because every command inside one bucket, including each bind's own explicit
`DiscardContents` `Clear()`, still replays in original public order) and 18 of
`rendertarget_effect_source_test.cpp`'s own 20 legs, registered individually as `Llgl_RenderTarget_
EffectSource_<leg>` so its own C1 leg's driver crash cannot take the other 18's coverage down with
it. `rendertarget_depthstencil_usage_test.cpp` (28/29) and that same C1/F1 pair stay unregistered:
both trace to a genuine, open, general bucket-ordering finding (a target revisited after depending
on another target, or two targets aliasing one physical resource, replay out of public order) plus
(C1 only) a second, unrelated crash from a custom `ShaderEffect` using multiple Vulkan descriptor
sets -- see `known_bugs.md`'s two open entries for the full analysis.
`rendertarget_producer_consumer_test.cpp` also stays unregistered (39/41): D5 ("A -> B -> A", the
same shape as the F1 finding above) and I2 (a backbuffer draw sampling a target that gets rebound
again LATER in the same frame, after the swap-chain-always-trails-last rule from the `LLGL-40` fix
forces its own read to happen last) are two more reproductions of the identical bucket-ordering
finding -- `known_bugs.md`'s entry now covers all four legs across three files. Every other check in
this file, including the D1-D4 producer chains, MSAA and mip-mapped producers, `RenderTargetUsage`
variants and the never-read-target sampling legs, passes.
`Llgl_RenderTarget_FirstUse` (26/26) establishes that a brand-new `RenderTarget2D`/`RenderTargetCube`
constructed, bound, drawn into and read back all within one public frame already works here with no
warm-up frame, extra `Present()`, dummy draw or manual flush -- needed no fix.
`rendertarget_backbuffer_consumer_test.cpp` also stays unregistered (88/90): its G1 check -- a
BACKBUFFER consumer issued between two bind cycles of the same target, expecting the FIRST cycle's
content -- is a fifth reproduction of the same bucket-ordering finding, sharing `rendertarget
_producer_consumer_test.cpp`'s own I2 shape exactly. Every other producer/consumer/ordering leg in the
file passes, including MSAA and mip-mapped producers, `RenderTargetCube` face bind cycles, multi-family
(`SpriteBatch` + 3D) replay order, and 8 same-frame bind cycles repeated across 8 consecutive frames.
`bound_target_lifetime_test.cpp` (LLGL-41's last file) also stays unregistered, but for a good reason:
3/18 legs pass in full and, critically, **0/18 legs crashed** -- this fixture exists specifically to
catch a SIGSEGV when a bound render target is destroyed mid-cycle, and that defect simply does not
reproduce here. Every leg's own destroy-while-bound assertions (the next target reads correctly, a
live MRT sibling still resolves and survives, `Present()` refuses identically for a live or destroyed
bound target, 120 create/destroy-while-bound rounds complete cleanly) pass wherever not entangled with
two unrelated, already-catalogued findings: 15/18 legs fail their own unconditional backbuffer check,
a third reproduction of the `FixedHeightDynamicWidth` logical-width finding (this file's 72x36 request
derives a 60-wide logical space, confirmed via a temporary debug print), and leg L1's MRT-slot
mip-regeneration gap is the same one already declared on Vulkan and bgfx, not new here.
`rendertarget_sampling_orientation_test.cpp` also stays unregistered: its first 10 checks (`SpriteBatch`
orientation into and out of a `RenderTarget2D`, `BasicEffect`/`AlphaTestEffect` mesh-UV sampling,
`RenderTarget2D` vs. `Texture2D` byte-exact agreement) all pass, but its CD4 check -- a lit, textured
`BasicEffect` draw from a normal-less `VertexPositionTexture` layout -- throws uncaught and aborts
the whole process, since this fixture has no try/catch around that specific check. See
`known_bugs.md`'s open entry for the underlying capability gap.
Phase LLGL-7's `LLGL-42` `Texture`/`TextureCube`/`Texture3D` batch adds seven more fully-passing
tests with no fix required: `Llgl_CubeVolume_SetDataContract`/`Llgl_CubeVolume_GetDataContract`
(56/56 each -- `TextureCube`/`Texture3D` SetData/GetData are exact at every mip level, and
`RenderTargetCube::SetData` correctly refuses since `LlglRenderTargetCubeRenderer` only overrides
`GetData`), `Llgl_Texture2D_GetDataTransferRange` (74/74) and `Llgl_Texture2D_GetDataContract`
(40/40, both covering `Texture2D`/`RenderTarget2D` GetData's startIndex/elementCount contract and
render-target readback exactness), `Llgl_TextureFilterOrdinalContract` (70/70, all nine
`TextureFilter` ordinals resolve to the correct min/mag/mip native sampler) and
`Llgl_PointSamplingContract` (146/146, point selection stays exact across every address mode,
non-integer scale, render-target source and viewport offset this fixture probes), and
`Llgl_ColorSpace_MidTone` (17/17, a render-target colour round-trip is byte-identical with no sRGB
conversion baked in).
Phase LLGL-7's `LLGL-43` deferred-capture/`SpriteBatch` viewport batch adds three more fully-passing
tests with no fix required: `Llgl_Deferred_Viewport` (39/39 -- every deferred draw executes under the
`GraphicsDevice.Viewport` active at its own public call; `depthRangeApplies` is declared `false`
since this renderer's `SetViewport` never forwards `minDepth`/`maxDepth` to LLGL, the same boundary
already declared on bgfx), `Llgl_Deferred_Scissor` (47/47 -- the same contract for
`GraphicsDevice.ScissorRectangle`/`RasterizerState.ScissorTestEnable`, including a degenerate
zero-width/height rectangle rasterizing nothing, unlike Vulkan/EasyGL/bgfx's own declared exception),
and `Llgl_SpriteBatch3DOrder` (83/83, 3 declared skips -- a stock 3D draw issued after a `SpriteBatch`
inside one bind cycle executes in public order, not grouped by family). `deferred_source_lifetime
_test.cpp` stays unregistered (8/17 legs pass in full): critically, **0/17 legs crashed** -- the
REMED-GFX-167 defect this fixture exists to catch does not reproduce on LLGL, and the other 9 legs
fail only the already-catalogued `FixedHeightDynamicWidth` backbuffer artifact (a fourth
reproduction, this file's own 72x36 backbuffer request).
Phase LLGL-7's final `LLGL-44` misc-state batch adds `Llgl_GraphicsDevice_OrderedClear` (46/46, no
fix required -- `Clear()` is a genuinely ordered command here, honours `RenderTargetUsage
.PreserveContents`, and ignores both `Viewport` and `ScissorRectangle`). The other two files stay
unregistered, each for a real, distinct, newly-found `OPEN` finding: `frontface_winding_test.cpp`
(115/127) traces its 12 failures to a `VertexBuffer`/`IndexBuffer` reused (via `SetData()`+draw)
twice within one frame silently losing the FIRST draw's content, since `SetData()` writes into the
live GPU buffer immediately while the queued command only replays at frame end; a candidate fix was
implemented, found to introduce a NEW crash on an unrelated entry point, and reverted.
`stock_effect_sampler_contract_test.cpp` (64/65) traces its one failure to `ApplySamplerState()`
tracking only slot 0's own sampler state, so `DualTextureEffect`'s slot 1 (and `PbrEffect`'s other 4
texture units, already self-documented in a code comment) always samples with slot 0's filter/address
rather than its own. See `known_bugs.md`'s two new open entries for the full analysis of both.
Every other test is registered a second time pinned to the OpenGL
module through `CNA_LLGL_RENDERER`, which also exercises the selection path itself. All these tests
need a display; on a machine without one they report SKIPPED
rather than FAILED. On a headless machine a virtual display works:

```bash
Xvfb :99 -screen 0 1280x1024x24 &
ctest --test-dir cmake-build-llgl -R Llgl --output-on-failure   # configure with -DCNA_TEST_DISPLAY=:99
```

## Capability boundary

`GraphicsDevice.SupportsCapability()` answers honestly for this renderer:

| Capability | Supported | Why |
| --- | --- | --- |
| `DepthStencilBuffer` | yes | The swap chain really has both attachments and all seven clear paths work. |
| `MultiSampleAntiAliasing` | module-dependent | `MultiSampleCount` is honoured only at swap-chain CONSTRUCTION time (no way to enable it after the fact via `GraphicsDeviceManager.ApplyChanges()` — a `Game`'s eagerly-constructed device is always built with `MultiSampleCount=0`). On this project's own test environment the Vulkan module (lavapipe) applies it and produces a genuinely antialiased edge; the OpenGL module (llvmpipe/GLX) does not apply it at any sample count. Pixel-verified, including the module-dependent behaviour itself, by `Llgl_Msaa` (`LLGL-23`). This row is about the BACK BUFFER only — `RenderTarget2D.MultiSampleCount` is a separate, unconditionally-real capability on both modules; see "Render targets" above and `Llgl_Msaa_RenderTarget`. |
| `AnisotropicFiltering` | device-dependent | From LLGL's reported `limits.maxAnisotropy`. |
| `ThreeD` | yes | Draws with depth, cull and fill state, one texture, fog, the alpha test, and per-pixel lighting (textured or untextured-with-vertex-colours); the remaining stock effects are not implemented. |
| `WireFrame` | module-dependent | Real on the OpenGL module; the Vulkan module cannot, and refuses rather than drawing an empty frame. |
| `OcclusionQuery` | yes | Real `LLGL::QueryHeap`-backed queries — see "Occlusion queries" above for how `IsComplete()`/`PixelCount()` behave on this renderer. |
| `CustomEffects` | yes | Real `ShaderEffect`, scoped to `SpriteBatch` draws — see "Custom effects" above. |
| `Texture3D` | yes | Real `LLGL::TextureType::Texture3D` storage — `CreateTexture3D`/box-region `SetData`/`GetData`. Nothing samples a volume texture from a 3D shader yet. |
| `MultipleRenderTargets` | yes | 2-4 `RenderTarget2D` slots, written by a custom `ShaderEffect` drawn through `SpriteBatch` — see "Multiple render targets (MRT)" above for the scope boundary. |

There is no standalone `SupportsCapability` flag for single-target `RenderTarget2D` support (XNA
has none either) — `CreateRenderTarget2D` returning a real renderer instead of null is the signal.
