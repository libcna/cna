# LLGL graphics backend

## Status

An **experimental** CNA graphics backend built on [LLGL](https://github.com/LukasBanana/LLGL),
added 2026-07-31. Unlike every other backend in this project it does not name a native graphics
API: LLGL is itself an abstraction layer, and the module it drives is selected when the process
starts.

What is implemented and verified today is the **2D pipeline, on both the Vulkan and the OpenGL
module**:

* real SDL window, `LLGL::RenderSystem`, swap chain (24-bit depth, 8-bit stencil), clear and
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
  by name rather than silently dropping the light;
* **`RenderTarget2D`**: draw into it (both `SpriteBatch` and the 3D path), unbind back to the
  swap chain, sample it back onto the screen, and `GetData()` straight off the colour attachment.
  See "Render targets" below for what this does and does not cover;
* **`RenderTargetCube`**: draw into any of its 6 faces independently, unbind, sample the result
  through `EnvironmentMapEffect`, and `GetData()` per face. See "Render targets" below;
* **occlusion queries**: real `LLGL::QueryHeap`-backed `OcclusionQuery`. See "Occlusion queries"
  below for how `IsComplete()`/`PixelCount()` behave on this backend;
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

Every item this backend's design ever scoped for `RenderTarget2D` is now implemented; no
`GraphicsDevice.SupportsCapability()`/throw gap remains for it specifically (`RenderTargetCube`/MRT
still have their own smaller, documented scope boundaries -- see "Render targets" below).

Both modules are covered by their own CTests, so neither can break unnoticed because the default
preference happened to select the other one.

## Building

```bash
cmake -S . -B cmake-build-llgl -DCNA_GRAPHICS_BACKEND=LLGL -DCMAKE_BUILD_TYPE=Debug
cmake --build cmake-build-llgl -j4
```

CMake fetches LLGL at the pinned tag `Release-v0.04b`. For an offline or reproducible build, point
it at an existing checkout instead:

```bash
git clone https://github.com/LukasBanana/LLGL.git ~/deps/LLGL
git -C ~/deps/LLGL checkout Release-v0.04b
cmake -S . -B cmake-build-llgl -DCNA_GRAPHICS_BACKEND=LLGL -DCNA_LLGL_ROOT=$HOME/deps/LLGL
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
`SDL_WINDOW_OPENGL` window) and the backend needs it again afterwards, and the two must agree.

`CNA_LLGL_DEBUG=1` additionally enables LLGL's own debug layer and routes its reports to stdout.
It validates every command and is far too costly to leave on, but it is the fastest way to find out
why a draw produced nothing.

## Platform support

X11 only. A Wayland SDL window is refused with an error naming `SDL_VIDEODRIVER=x11`: LLGL 0.04b
compiles Wayland support only when explicitly enabled and this integration does not enable it.
Handing LLGL a handle it cannot present to would be worse than saying so.

## Shaders

Both flavours are checked in and no shader toolchain is needed to build:

```text
src/CNA/Internal/Backends/Llgl/shaders/
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
python3 src/CNA/Internal/Backends/Llgl/shaders/compile_shaders.py
python3 src/CNA/Internal/Backends/Llgl/shaders/compile_shaders.py --check   # verifies freshness
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
(`LLGL-32`): they route through `IGraphicsBackend::CreateVertexBuffer(int)` (count-only, no
`VertexDeclaration`) and a raw byte `SetData`, so `LlglVertexBufferBackend::ResolveVertexAttributes()`
infers the vertex layout from the upload stride instead -- 16/20/24/32/**48**/**52**/**68** bytes
are each a distinct, unambiguous size among `GraphicsDevice.cpp`'s own GPU-packed stream structs
plus `VertexPositionNormalTextureSkinned`'s own (stride 52, `SkinnedEffect`),
`VertexPositionNormalTangentTexture`'s own (stride 48, `PbrEffect`) and
`VertexPositionNormalTangentTextureSkinned`'s own (stride 68, `SkinnedPbrEffect`), the same
technique the Vulkan backend's own `MakeExt3DKey()` already uses for these exact stream sizes. The
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
shared colour texture. A new `LlglBoundRenderTarget` common interface lets the backend's "what's
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
this backend's own back-buffer convention (0 = no MSAA, even though 1 is the internally stored "no
MSAA" sentinel). Every pipeline drawn against the currently bound target -- 3D, `SpriteBatch`, and
custom `ShaderEffect`s alike -- is now built with a `rasterizer.multiSampleEnabled`/render-pass
sample count that matches whatever is ACTUALLY bound (`LlglGraphicsBackend::GetPrimarySampleCountEXT()`),
not the swap chain's own, and the pipeline cache key folds the sample count in so a pipeline built
for one sample count is never reused for another -- see `Llgl_Msaa_RenderTarget`'s own CTest entry
below for the real bug this closed. Unlike the back buffer (`MultiSampleCount` is only ever
honoured at swap-chain/`GraphicsDevice` CONSTRUCTION time), a `RenderTarget2D`'s own sample count is
read at ITS OWN construction, so it works through the ordinary `Game` + `new RenderTarget2D(...)`
flow with no raw-`GraphicsDevice`-construction workaround needed, and — a genuine, positive
difference from back-buffer MSAA — is honoured on BOTH modules on this project's own test
environment, not gated behind the same Vulkan-only limitation `Llgl_Msaa` documents for the swap
chain. MRT binds and `RenderTargetCube` faces do not support MSAA yet (`LlglBoundRenderTarget::GetSampleCount()`
defaults to 1/no-MSAA for both).

**`RenderTarget2D` `mipMap` is real.** `CreateRenderTarget2D`'s colour texture is allocated with a
full mip chain when `mipMap=true` (`RenderTarget2D.LevelCount`'s own `CalculateMipLevels(w, h)`
formula, computed identically here and matching the Vulkan backend's own
`CalculateVulkanRTMipLevels`) -- the render-target ATTACHMENT itself still only ever binds level 0
(`LLGL::AttachmentDescriptor`'s own `mipLevel` default), and `RecordAndSubmitFrame()`/
`CaptureBackbuffer()` call `LLGL::CommandBuffer::GenerateMips()` on the colour texture right after
`EndRenderPass()` for any target that wants it -- the LLGL equivalent of the Vulkan backend's own
`vkCmdBlitImage` cascade (`VulkanTargetPassEXT::MaybeGenerateMips`) and of EasyGL's
`glGenerateMipmap`-on-unbind, but a single built-in LLGL call instead of a hand-rolled blit loop.
Since a `RenderTarget2D` can be destroyed before the frame that references it is replayed, knowing
WHICH texture to regenerate at replay time is captured onto each `FrameCommand` at QUEUE time
(`mipRegenColorTexture`), mirroring `target`/`projectionBuffer`'s own existing pattern, rather than
looked up live from whatever is currently bound. `GetData(level)` is real for any level in
`[0, LevelCount)` now (previously a hard `level != 0` refusal) -- an out-of-range level throws
`System::NotSupportedException` through the shared `Texture2D::GetData` layer, same as any other
backend readback failure. MRT binds and `RenderTargetCube` faces do not support mip-mapping yet
(`LlglBoundRenderTarget::GetMipRegenColorTextureEXT()` defaults to null for both).

Two implementation choices are worth knowing if you are debugging a render-target frame:

* **One render pass per distinct target, not per bind.** LLGL's public Vulkan API has no way to
  re-enter a render pass with `Load` semantics, so a frame that interleaves draws to the back
  buffer and one or more render targets is replayed as one pass per distinct target IDENTITY, in
  first-appearance order — every command for a given target is grouped together, not replayed in
  original interleaved order. `RenderTargetUsage.PreserveContents` is not honoured across separate
  binds of the same target within one frame as a result.
* **Every render target shares the swap chain's own attachment formats.** The colour attachment
  always takes the swap chain's colour format, and a depth/stencil attachment matching the swap
  chain's own format is always allocated regardless of the requested `DepthFormat` (which only
  changes what `HasRealDepthBuffer()` reports). This is what lets every cached sprite/primitive
  pipeline — built once against the swap chain's render pass — be reused as-is for a render-target
  pass, instead of needing a second, render-target-keyed pipeline cache: Vulkan's render-pass-
  compatibility rule only requires matching attachment formats and sample counts, not the same
  `VkRenderPass` object. The 3D path (`BasicEffect`, depth-tested `VertexBuffer` draws) works into
  a render target too, not just `SpriteBatch` — both share the same reused pipelines.

Destroying a `RenderTarget2D` before `Present()` (create it, draw into it, sample it, let it go out
of scope, all within one `Draw()`) is safe: like `VertexBuffer`/`IndexBuffer`, the underlying LLGL
objects are released only once the frame that may still reference them has actually been
submitted. `RenderTarget2D::GetData()` also forces any of its own still-queued draws to be
submitted first — its content, unlike a plain `Texture2D`'s, only exists once they are.
`RenderTargetCube`'s destructor does the same for its 6 face targets, releasing the shared colour
and depth textures exactly once (not 6 times) regardless of how many of the 6 faces were ever
drawn into.

Unlike `RenderTarget2D`'s anonymous (textureless) depth/stencil attachment, `RenderTargetCube`'s
shared depth/stencil buffer is a real, explicitly-owned `LLGL::Texture` — it has to be, since all 6
face `AttachmentDescriptor`s need to reference the SAME one, which an anonymous per-attachment
buffer cannot do. Sampling a `RenderTargetCube` through `EnvironmentMapEffect` (or anywhere else
that accepts an `ITextureCubeBackend`) resolves through a new `ResolveSampledTextureCube()` helper
mirroring `ResolveSampledTexture()`'s own dual `LlglTextureBackend`/`LlglRenderTargetBackend`
resolution — a hard `dynamic_cast<const LlglTextureCubeBackend*>` alone would have silently failed
to sample a rendered cube face.

### Multiple render targets (MRT)

`GraphicsDevice.SetRenderTargets` accepts 2-4 `RenderTarget2D` slots bound simultaneously, scoped
to a deliberately narrower first cut than this project's other MRT-capable backends:

* **`RenderTarget2D` slots only.** Mixing a `RenderTargetCube` face into a multi-target set is
  refused by name rather than attempted.
* **Written only by a custom `ShaderEffect` drawn through `SpriteBatch`.** A 3D colour-only draw
  (`DrawPrimitivesEx`/`DrawIndexedPrimitivesEx`) while an MRT set is bound throws by name too --
  no stock effect family in this backend declares more than one fragment output, and real XNA MRT
  is only meaningfully useful through a custom `layout(location=N) out`-per-slot fragment shader
  anyway.
* **`ColorWriteChannels1..3` are real** (LLGL-21 follow-up): each slot's own write mask applies
  independently. This needed `GraphicsPipelineDescriptor::blend.independentBlendEnabled = true`
  whenever more than one attachment is bound -- without it, LLGL silently reuses `blend.targets[0]`
  for every attachment regardless of what `targets[1..3]` were set to (confirmed by reading
  `VKGraphicsPSO.cpp`/`GLBlendState.cpp` directly). Module-dependent once that bug was fixed: the
  Vulkan module genuinely masks a non-zero slot on this environment; the OpenGL module's
  `glColorMaski` does not (a real GL driver constraint here, not a CNA defect) -- see
  `Llgl_MRT`'s own `[SKIP]`-gated check. `BlendState.MultiSampleMask` is still not applied (LLGL's
  sample mask lives in the blend descriptor and would multiply the pipeline cache with no real use
  on this backend yet).

A new `LlglMRTBinding` combines the N bound targets' own colour textures (borrowed -- still owned
and released by the `RenderTarget2D` backends that created them, never duplicated or double-freed
here) plus a fresh, anonymous depth/stencil attachment (matching `CreateRenderTarget2D`'s own
single-target depth attachment, rather than trying to share or preserve any one slot's own depth
buffer) into ONE `LLGL::RenderTarget`. Unlike `RenderTarget2D`/`RenderTargetCube`, an MRT bind has
no owning XNA-visible object -- `SetRenderTargets` just names N already-existing targets as slots
-- so `LlglMRTBinding` is owned by the backend itself, replaced (and the previous one
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
same deferred frame as everything else this backend draws — LLGL requires both to be issued
inside an open render pass, which this backend only opens at submit time.

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

`Microsoft::Xna::Framework::Graphics::ShaderEffect` (`NOXNA`) compiles hand-authored GLSL vertex
and fragment source and draws through it. It is **scoped to `SpriteBatch` draws only** -- the
vertex shader is bound to the fixed sprite `position`/`texCoord`/`color` layout, not an arbitrary
`VertexDeclaration` a 3D draw might use, mirroring the native `VULKAN` backend's own
`VulkanEffectBackend` scope exactly rather than inventing a new limitation:

```cpp
ShaderEffect fx(device, vertexGlslSource, fragmentGlslSource);   // always real GLSL text
fx.SetUniformVec4("uColor", 1.0f, 0.0f, 0.0f, 1.0f);              // name accepted, not consulted
spriteBatch.Begin(SpriteSortMode::Immediate, BlendState::Opaque, nullptr, nullptr, nullptr, &fx);
```

Unlike the `VULKAN` backend (which expects the caller to hand it pre-compiled SPIR-V, since it
names one fixed native API -- see `docs/shader-effect-vs-fx-bytecode.md`), `vertSrc`/`fragSrc` are
**always real GLSL text** here: this backend picks its module at runtime, so the game has no
reliable way to know in advance which form to hand over. `CompileProgram()` (via the public
`ShaderEffect` constructor) hands the GLSL to LLGL directly when the loaded module accepts it
(OpenGL), or compiles it to SPIR-V first through a real runtime `libshaderc` call when it does not
(Vulkan) -- the same problem this project's `SDL_GPU` backend already solved the same way.

Named-uniform setters (`SetUniformMat4`/`Vec4`/`Vec3`/`Vec2`/`Float`/`Int`) do **not** do real
name-based reflection -- LLGL exposes none for a raw GLSL/SPIR-V module, and adding one would need
a new dependency (SPIRV-Cross or similar). They map onto a fixed 32-float (128-byte) uniform block,
identical to the native Vulkan backend's own documented `VulkanEffectBackend::pushConst_` layout,
uploaded to a real constant buffer at binding 1 instead of a Vulkan push constant:

```glsl
layout(std140, binding = 1) uniform PC {
    vec4 vpSize_pad;  // xy = viewport/target size in pixels; set automatically, not by the game
    mat4 uMatrix;     // SetUniformMat4
    vec4 uColor;      // SetUniformVec4 / Vec3 (leaves w) / Vec2 (leaves z, w)
    vec4 uFloats;     // uFloats.x only -- SetUniformFloat / Int
} pc;
```

`name` is accepted (matching the shared `IEffectBackend` signature every backend implements) but
not consulted -- matching the same established precedent rather than inventing new semantics.
`colorMap`/`samplerState` (binding 2/3) sample the sprite's own texture, exactly as the stock
sprite shader does; there is no way yet to bind a second texture unit to a custom effect on this
backend. See `examples/llgl_shadereffect_test.cpp` for a complete worked example, including the
vertex shader's own pixel-to-NDC technique.

## EnvironmentMapEffect

Cube-map reflections. Unlike every other stock effect on this backend, `EnvironmentMapEffect` does
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
the Vulkan backend's own `env_map3d.frag.glsl` -- itself the product of three previously-found
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
  other per-draw uniform block here, mirroring the Vulkan backend's own `BoneBlock`/`FogParams`
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
Vulkan backend's own `skinned3d.vert.glsl`. The lighting formula (per-light Lambertian diffuse +
Blinn-Phong specular) pre-folds `AmbientLightColor*DiffuseColor` into `EmissiveColor` on the CPU
side exactly like `EnvironmentMapEffect` does (confirmed by reading `SkinnedEffect::FillGpuDrawParams`
directly), not `BasicEffect`'s separate-ambient-term convention.

**Two real, independent vertex-layout gaps were closed to add this** (found by reading the code,
not by a failing test): `MapVertexUsage()` (the real-`VertexDeclaration` attribute-mapping path)
had no cases for `VertexElementUsage::BlendWeight`/`BlendIndices` at all, and
`LlglVertexBufferBackend::ResolveVertexAttributes()`'s declaration-less stride-inference switch
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
class name/comment changed) from the Vulkan backend's own `examples/vulkan_skinnedeffect_*_test.cpp`,
which are already fully backend-agnostic real-XNA-API code.

**Out of scope**: real XNA's `PreferPerPixelLighting` selecting a genuinely different, per-vertex
(Gouraud) lit shader -- this backend is per-pixel-lit only, matching every established CNA backend
except D3D9 (`GpuDrawParams::preferPerPixelLighting`'s own documented deviation). The
`VertexColorEnabled` CNA-only (`NOXNA`) extension property and its stride-56 vertex-colour variant
are not implemented. `SkinnedPbrEffect` (stride 68, `PbrEffect` combined with skinning) is done
too -- see "PbrEffect" below.

## PbrEffect

The glTF 2.0 metallic-roughness BRDF (`PbrEffect`, `NOXNA` -- real XNA predates the PBR content
pipeline). Like `EnvironmentMapEffect`/`SkinnedEffect`, gets its own dedicated resources rather
than reusing the shared `Transform` block:

* `primitivePbrLayout_` pipeline layout: an 84-float (336-byte) `PbrParams` uniform buffer at
  binding 1, then 5 texture/sampler pairs at bindings 2-11 -- base colour, normal map,
  metallic-roughness map (glTF packing: G=roughness, B=metallic), emissive map, occlusion map;
* dedicated vertex shader (`AcquirePrimitivePbrVertexShader()`), needing a NEW vertex element this
  backend never had before: `VertexElementUsage::Tangent` (`MapVertexUsage`'s new case, location
  6) -- the tangent-space TBN basis the fragment stage builds for normal mapping. A new stride-48
  (`VertexPositionNormalTangentTexture`) case was added to `ResolveVertexAttributes()`'s
  declaration-less fallback switch too, mirroring `LLGL-32`'s own stride-inference precedent;
* one per-draw buffer pool (`pbrUniformBuffers_`/`pbrUniformData_`) for the `PbrParams` block,
  same growth/reuse discipline as `envMapUniformBuffers_`/`skinnedUniformBuffers_`.

`PbrLight()` (GGX distribution, Smith-Schlick-GGX visibility, Schlick Fresnel -- the glTF 2.0
spec's own reference BRDF) is transliterated directly from the Vulkan backend's own already-correct
`pbr3d.frag.glsl`, applying the fog-mix convention fix learned from `EnvironmentMapEffect`'s own
bug from the start (`mix(rgb, fogColor.rgb, vFogFactor)`, this backend's "how much fog" convention)
rather than needing to rediscover it. Base colour factor and alpha are kept independent (not
premultiplied), matching glTF's own `baseColorFactor` convention rather than most other CNA stock
effects' `DiffuseColor`.

**Unlike `EnvironmentMapEffect`/`SkinnedEffect`'s "throw if the required texture is missing"
convention, PbrEffect's 4 optional maps resolve to a 1x1 default texture instead of throwing** --
`EnsureDefaultPbrTexturesEXT()` lazily creates an opaque white texture (used for
`MetallicRoughnessMap`/`EmissiveMap`/`OcclusionMap` when null) and an RGBA(128,128,255,255) flat
normal texture (decoding to tangent-space (0,0,1), used for `NormalMap` when null), mirroring the
Vulkan backend's own `EnsureDefaultWhiteTexture`/`EnsureDefaultFlatNormalTexture` precedent -- real
`PbrEffect::FillGpuDrawParams()` can legitimately leave all 4 null (only `Texture`/
`MetallicFactor`/`RoughnessFactor` are required), so throwing would incorrectly reject a valid,
minimally-configured draw. `Texture` (base colour) is still required and throws by name if missing
("PbrEffect needs Texture bound"), matching every other stock effect's own precedent. All 5 texture
units share this backend's one global sampler state (`ApplySamplerState` only ever tracks slot 0)
-- the SAME `LLGL::Sampler` object is bound at all 5 sampler slots, since each GLSL `sampler2D`
declaration still needs its own binding even when the underlying resource is identical.

`Llgl_PbrEffect_HandDerived` (+ `_OpenGL`) is adapted from the Vulkan backend's own
`examples/vulkan_pbreffect_handderived_test.cpp` (itself fully backend-agnostic real public XNA
API + `VertexBuffer::SetDataRaw`), drawing into an off-screen `RenderTarget2D` read back with
`GetData()` instead of the source's own hand-rolled `Game` subclass that resizes the whole window
-- `PixelTestGame`'s `Game` construction has no equivalent hook, and reading a hard-coded small
pixel address directly off the (much larger) default back buffer sampled a world position over a
full unit away from the coordinate origin the analytic derivation assumes (found via a debug
shader pass outputting `vWorldPos` directly -- a test-authoring mistake, not a backend defect).

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
`GraphicsDevice` objects constructed directly (MSAA is construction-time only on this backend, so
neither `Game`'s eagerly-constructed device nor `ApplyChanges()` can reach it) — the sample-count-
dependent checks report `[SKIP]` on a module that does not apply MSAA to the default framebuffer at
all (the OpenGL module, on this project's own test environment) rather than failing. `Llgl_DualTexture`
covers `DualTextureEffect`'s `VertexColorEnabled`-gated tint and proves the two textures sample
independently (a white base plus a red overlay must read back red, not white).
`Llgl_DualTextureEffect_VertexColor` and `Llgl_GraphicsDevice_DefaultStateOcclusion` are pre-existing,
cross-backend shared sources (already registered on EasyGL/Vulkan/Bgfx) reused verbatim once
`LLGL-32` made `DrawUserPrimitives()` work on this backend, exercising it through two of its four
recognised upload strides. `Llgl_EnvironmentMapEffect_AlphaScaledLerp` is another such reused
source, covering `EnvironmentMapEffect`'s alpha-scaled cube-map base lerp (Task 891's fix); see
"EnvironmentMapEffect" above for why it has no `_OpenGL` twin (a genuine `hasCubeTextures`
limitation of this project's own OpenGL module, not a gap in this backend).
`Llgl_SkinnedEffect_IdentityBones`/`Llgl_SkinnedEffect_TwoBoneBlend` are ported (not verbatim, but
adapted with only the class name/comment changed) from the Vulkan backend's own sources -- see
"SkinnedEffect" above. `Llgl_RenderTargetCube` covers 6 independent per-face draw/`GetData()` round
trips plus sampling the result through `EnvironmentMapEffect`; like the `EnvironmentMapEffect` test
it has no `_OpenGL` twin, for the same `hasCubeTextures` reason.
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
Every other test is registered a second time pinned to the OpenGL
module through `CNA_LLGL_RENDERER`, which also exercises the selection path itself. All these tests
need a display; on a machine without one they report SKIPPED
rather than FAILED. On a headless machine a virtual display works:

```bash
Xvfb :99 -screen 0 1280x1024x24 &
ctest --test-dir cmake-build-llgl -R Llgl --output-on-failure   # configure with -DCNA_TEST_DISPLAY=:99
```

## Capability boundary

`GraphicsDevice.SupportsCapability()` answers honestly for this backend:

| Capability | Supported | Why |
| --- | --- | --- |
| `DepthStencilBuffer` | yes | The swap chain really has both attachments and all seven clear paths work. |
| `MultiSampleAntiAliasing` | module-dependent | `MultiSampleCount` is honoured only at swap-chain CONSTRUCTION time (no way to enable it after the fact via `GraphicsDeviceManager.ApplyChanges()` — a `Game`'s eagerly-constructed device is always built with `MultiSampleCount=0`). On this project's own test environment the Vulkan module (lavapipe) applies it and produces a genuinely antialiased edge; the OpenGL module (llvmpipe/GLX) does not apply it at any sample count. Pixel-verified, including the module-dependent behaviour itself, by `Llgl_Msaa` (`LLGL-23`). This row is about the BACK BUFFER only — `RenderTarget2D.MultiSampleCount` is a separate, unconditionally-real capability on both modules; see "Render targets" above and `Llgl_Msaa_RenderTarget`. |
| `AnisotropicFiltering` | device-dependent | From LLGL's reported `limits.maxAnisotropy`. |
| `ThreeD` | yes | Draws with depth, cull and fill state, one texture, fog, the alpha test, and per-pixel lighting (textured or untextured-with-vertex-colours); the remaining stock effects are not implemented. |
| `WireFrame` | module-dependent | Real on the OpenGL module; the Vulkan module cannot, and refuses rather than drawing an empty frame. |
| `OcclusionQuery` | yes | Real `LLGL::QueryHeap`-backed queries — see "Occlusion queries" above for how `IsComplete()`/`PixelCount()` behave on this backend. |
| `CustomEffects` | yes | Real `ShaderEffect`, scoped to `SpriteBatch` draws — see "Custom effects" above. |
| `Texture3D` | yes | Real `LLGL::TextureType::Texture3D` storage — `CreateTexture3D`/box-region `SetData`/`GetData`. Nothing samples a volume texture from a 3D shader yet. |
| `MultipleRenderTargets` | yes | 2-4 `RenderTarget2D` slots, written by a custom `ShaderEffect` drawn through `SpriteBatch` — see "Multiple render targets (MRT)" above for the scope boundary. |

There is no standalone `SupportsCapability` flag for single-target `RenderTarget2D` support (XNA
has none either) — `CreateRenderTarget2D` returning a real backend instead of null is the signal.
