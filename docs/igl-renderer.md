# The IGL renderer

`CNA_GRAPHICS_RENDERER=IGL` selects CNA's renderer built on
[facebook/igl](https://github.com/facebook/igl), Meta's "Intermediate Graphics Library", pinned at
`v1.1.1`.

> **Status: experimental, and built.** This banner used to read "not yet compiled"; that stopped
> being true on 2026-08-16, when the renderer first configured, compiled, rendered a frame and
> passed its pixel-conformance tests against Mesa llvmpipe, and it has been built and run in every
> session since. What is *verified* is narrower than what is *implemented*, and `plan_igl.md` §1
> is the per-phase record of which is which. The short version: the OpenGL/GLX backend is verified
> across the whole example suite; the Vulkan backend brings up a device, renders 3D, depth and
> render targets, `SpriteBatch` and MSAA correctly, and its one remaining shortfall is that a custom
> `ShaderEffect` cannot take parameters there (`plan_igl.md` IGL-43). As of the 2026-08-18 row-order
> repair the registered suite is 65/65 green (34 host-portable unit cases plus 31 example tests), and
> running all 26 example binaries explicitly on each backend gives 26/26 on OpenGL and 23/26 on
> Vulkan — the three Vulkan shortfalls are all that one gap, and are in the gap table below and in
> `plan_igl.md` §1.

## What this renderer is

Like `LLGL`, `IGL` names a **portable abstraction, not a native graphics API**. `igl::IDevice`
fronts real OpenGL/OpenGL ES, Vulkan and Metal implementations; CNA compiles the backends it can
(`CNA_IGL_BUILD_BACKEND_OPENGL`, `CNA_IGL_BUILD_BACKEND_VULKAN`) and selects one per process.

## Choosing the backend

```bash
CNA_IGL_BACKEND=auto     # default preference: OpenGL, then Vulkan (compiled-in ones only)
CNA_IGL_BACKEND=opengl   # or gl, glx
CNA_IGL_BACKEND=vulkan   # or vk
```

The choice is **resolved once, cached, and never falls back**. It has to be: `GraphicsDevice` reads
it to decide the platform window's render intent *before the renderer exists*, and a native window
cannot be both OpenGL- and Vulkan-capable. An explicit backend this build does not contain is an
error by name, not a silent substitution.

That resolution is what the renderer's own `GraphicsRendererDescriptor` is built from
(`IglRendererDescriptor.cpp`), so all three of its window-facing answers follow the backend rather
than the compile-time fact that CNA selected IGL:

| Backend | `windowKind` | `glFramebuffer` | `needsGlContext` |
|---------|--------------|-----------------|------------------|
| OpenGL | `RendererWindowKind::OpenGL` | depth 24, stencil 8, double-buffered, multisample-capable | yes — it adopts the platform's GL context |
| Vulkan | `RendererWindowKind::Vulkan` | none (a Vulkan-intent window carries no GL visual) | no — it builds its surface from the native window handle |

Both halves matter. The window kind is what `AreWindowKindsCompatible` compares when a fallback
chain moves from one renderer to another, so recording OpenGL for a Vulkan run would let a
later candidate adopt a window it cannot render into. The framebuffer request matters because GLX
fixes a window's visual — and therefore its depth, stencil and multisample bits — when the window
is created: asking afterwards silently yields whatever the default visual carried, in practice a
0-bit stencil buffer that turns every `DepthStencilState.StencilEnable` into a no-op.

OpenGL leads the default preference because IGL's OpenGL backend can **adopt** the GL context CNA's
own `CNA::Platform::IPlatformGlContext` already creates for the window
(`igl::opengl::glx::Context`'s adopting constructor). IGL's other Linux GLX constructor opens its
own display connection and leaves the drawable unset, which cannot present to a window.

## Platform support

| Platform | State |
|----------|-------|
| Linux / X11 | Both backends |
| Linux / Wayland | Not wired up — the constructor throws by name |
| Windows, macOS | Not wired up (IGL has WGL and Metal backends; CNA has no bring-up path for them here yet) |

## Capability boundary

| Capability | IGL renderer |
|------------|--------------|
| `ThreeD` | Yes |
| `DepthStencilBuffer` / `StencilBuffer` | Yes, when the surface has a depth/stencil attachment |
| `MultipleRenderTargets` | Yes, 2–4 `RenderTarget2D` slots (`IGL_COLOR_ATTACHMENTS_MAX`) |
| `MultiStreamVertexInput` | Yes — `igl::VertexAttribute::bufferIndex` expresses it natively |
| `Instancing` | Yes — `VertexSampleFunction::Instance` |
| `CustomEffects` | Yes on OpenGL; parameters are refused on Vulkan (see below) |
| `Texture3D` | Yes (real volume storage, sampling verified); voxels cannot be read back — see the gap table |
| `AnisotropicFiltering` | Yes |
| `MultiSampleAntiAliasing` | Yes on render targets; on the back buffer only via the OpenGL visual |
| `WireFrame` | OpenGL only — Vulkan needs `fillModeNonSolid`, which IGL does not request |
| `OcclusionQuery` | **No** — IGL exposes no query object on any backend at `v1.1.1` |
| `AdditiveBlending` | Yes |

## Surface formats

`igl::TextureFormat` is not a superset of XNA's `SurfaceFormat`, and the gaps are not all missing
names: several IGL formats carry a familiar name and a different texel layout, and a few differ
between IGL's own two backends. Since this renderer picks its backend at run time, a format whose
layout depends on which backend was chosen is no use to it.

These formats have an IGL counterpart with the same texel size, channel order and
normalized/integer/float interpretation on **both** backends:

`Color`, `ColorBgraEXT`, `ColorSrgbEXT`, `ByteEXT`, `UShortEXT`, `Rg32`, `Single`, `Vector2`,
`Vector4`, `HalfSingle`, `HalfVector2`, `HalfVector4`, `HdrBlendable`.

Everything else is **refused by name** rather than substituted:

| Format | Why |
|--------|-----|
| `Bgr565` | XNA packs R5G6B5; IGL's `B5G6R5_UNorm` is the reverse order, and its OpenGL backend refuses that format outright |
| `Bgra5551` | XNA packs A1R5G5B5; IGL offers only B5G5R5A1 and R5G5B5A1 |
| `Bgra4444` | XNA packs A4R4G4B4; IGL's `ABGR_UNorm4` is R4G4B4A4 on OpenGL and B4G4R4A4 on Vulkan |
| `Rgba1010102` | XNA packs A2B10G10R10; IGL's `RGB10_A2_UNorm_Rev` is that on OpenGL and A2R10G10B10 on Vulkan |
| `Rgba64` | An 8-byte R16G16B16A16 unsigned-normalized texel. IGL v1.1.1 has no 16-bit-per-channel RGBA format; `RGBA_UInt32` is a 16-byte integer-sampled texel, not a wider match |
| `Alpha8` | `VK_FORMAT_UNDEFINED` on Vulkan, and the `GL_ALPHA` family is not in the OpenGL core profile this renderer requests |
| `Dxt1` / `Dxt3` / `Dxt5` / `Dxt5SrgbEXT` | IGL v1.1.1 carries no BC1/BC2/BC3 format |
| `Bc7EXT` / `Bc7SrgbEXT` | IGL has the format, but this renderer has no compressed-block upload path — every transfer goes through `ITexture::upload`, which moves linear rows |
| `NormalizedByte2` / `NormalizedByte4` | IGL v1.1.1 has no signed-normalized texture format |

**What a game sees today.** The renderer reports `Unsupported` for the refused set and `Defer` for
the supported one, so the framework's own rule (`Texture::ValidateFormat`, `SurfaceFormat.Color`
only — the same rule 47 of the 49 renderers use) still decides what a public `Texture2D` or
`RenderTarget2D` may be. The supported set above is reachable through the renderer contract
(`IGraphicsRenderer::CreateTexture`, `CreateRenderTarget2DEXT`), which is where
`igl_surfaceformat_test.cpp` exercises it. Promoting those formats to the public API is a
deliberate non-goal for now: it would need per-format end-to-end verification of upload, sampling
*and* readback, not just of storage.

Transfer sizes come from the shared `Texture::GetFormatSizeEXT` / `GetBlockSizeSquaredEXT`
metadata, never from `width * 4` — a texel here may be 1, 2, 4, 8 or 16 bytes, and an upload whose
row pitch is shorter than one packed row is refused rather than read past.

## Known gaps, and why

| Gap | Cause | Behaviour |
|-----|-------|-----------|
| Occlusion queries | No IGL API for them | `SupportsCapability` reports false; a query completes immediately with 0 samples rather than spinning forever |
| Back-buffer MSAA on Vulkan | IGL's swap-chain images are single-sample and this renderer adds no resolve pass of its own | `GetMultiSampleCount()` returns 0 — the count that is genuinely in effect |
| Swap interval on Vulkan | Present mode is fixed when the swap chain is created | `SetSwapInterval` returns false |
| Sampler LOD bias | `igl::SamplerStateDesc` has no such field | recorded for diagnostics, not applied |
| `Texture3D.GetData` (volume readback) | IGL `v1.1.1` cannot attach a 3D texture to a framebuffer, which is its only readback route. `opengl::TextureBufferBase::attach` falls through to `glFramebufferTexture2D` for a volume — `getNumLayers()` counts array layers, of which a volume has one — and the driver answers `GL_INVALID_OPERATION … invalid textarget GL_TEXTURE_3D`; the Vulkan copy is 2D-only in the same way | `GetData` returns false and the shared layer raises `NotSupportedException` rather than fabricating voxels. Upload and sampling are unaffected (`plan_igl.md` IGL-17) |
| Cube render-target MSAA | `igl::FramebufferDesc` cannot express a multisampled cube attachment with a per-face resolve | applied count reported as 1 |
| Non-`Color` surface formats in the public API, beyond `Rg32` and `Single` | Promotion promises a whole path — the typed `SetData`/`GetData` overloads, sampling, render-target use, and the framework's four-byte colour-transfer rule — so each format needs that verified end to end on both backends. `Rg32` and `Single` now have it (`Igl_PublicSurfaceFormat`); the rest do not. `ByteEXT`, `UShortEXT` and `HalfSingle` additionally have texels that are not a multiple of four bytes, so they would be admitted by the renderer gate and then mishandled by the layer above it | the renderer refuses what IGL cannot store, accepts the two verified formats, and defers the rest to the framework's `Color`-only rule (see *Surface formats* above) |
| A custom `ShaderEffect`'s parameters on Vulkan | Loose non-block uniforms do not exist in Vulkan GLSL, and IGL's Vulkan encoder leaves `bindUniform` unimplemented | Refused by name at draw time rather than drawn with stale values. The effect itself compiles, binds and draws on Vulkan (`plan_igl.md` IGL-42/IGL-43) |
| A custom `ShaderEffect`'s GLSL is not portable between the two backends | SPIR-V requires an explicit `layout(location = N)` on every user input and output — the varyings between stages included — and `layout(set = N, binding = N)` on samplers; desktop GLSL 4.10 requires neither, and the two backends do not accept the same `#version` | Supply two sources and pick by backend. A shader that violates this is refused with glslang's own line-and-reason text plus the requirement in words (`plan_igl.md` IGL-70); `igl_custom_effect_backend_test.cpp` is the worked example of both variants |

## IGL's debug trap is turned off deliberately

IGL's debug builds answer an internal failure by logging it **and** raising `SIGTRAP`
(`igl::_IGLDebugBreak`, reached from every `IGL_DEBUG_ABORT` and `IGL_SOFT_ASSERT`). That is the
right default for IGL's own samples and the wrong one for an embedder: the same call sites also fill
in the `igl::Result` this renderer already checks, so the trap removes CNA's ability to report a
failure it is equipped to handle — a `ShaderEffect` whose GLSL does not compile used to take the
process down instead of raising the compile error.

`IglRenderer`'s constructor therefore calls `igl::setDebugBreakEnabled(false)` once per process.
Only the break is disabled: IGL still logs every such failure at error level, and CNA still checks
every `Result` and throws by name.

## How the frame is built

A **lazily opened render pass**, not a deferred command list:

* `Clear*` ends whatever pass is open and opens a new one whose load actions carry the clear values;
* a draw joins the open pass, opening one that *loads* previous contents when none is;
* `SetRenderTargets` ends the pass and switches framebuffer;
* `Present()` ends the pass, presents the drawable and submits the frame;
* a readback, or an upload into a buffer an already-recorded draw references, calls
  `FlushPendingFrameEXT()` first, so the GPU work that preceded it has genuinely run.

## Coordinate conventions

Both backends present a **bottom-left origin**: IGL's Vulkan encoder binds a negative-height
viewport specifically so its coordinate system matches OpenGL's. Two consequences:

1. **Presentation lives in the projection.** The two backends disagree about what a non-zero
   viewport *origin* means, so this renderer always binds a full-surface viewport and folds XNA's
   `Viewport` rectangle and the letterbox/overscan rectangle into a clip-space scale/offset matrix.
   Clipping is done by the scissor rectangle, whose Y origin *is* converted per backend (OpenGL
   measures it from the bottom, Vulkan from the top).
2. **Off-screen targets render Y-flipped — on the OpenGL backend only.** That stores their rows
   top-first, so `RenderTarget2D.GetData()` and sampling a target both agree with an uploaded
   `Texture2D`, and the pipeline's front-face winding is reversed to match so `CullClockwiseFace`
   culls the same triangles either way. The Vulkan backend needs neither, because "up the screen"
   is a different direction through image memory in each: a GL texture's row 0 sits at `t=0`, the
   bottom, while a Vulkan image's row 0 is the top. Applying it on both stored rendered content
   upside down relative to uploaded content (`plan_igl.md` IGL-67).
3. **Vulkan readbacks undo one row flip.** `igl::vulkan::Framebuffer::copyBytesColorAttachment`
   reverses the rows of every rectangle it copies and its OpenGL counterpart reverses none, so
   exactly one has to be undone for both backends to owe a caller the same bytes. The back buffer's
   readback additionally converts the requested rectangle's Y origin, for the same reason the
   scissor rectangle already did (`plan_igl.md` IGL-60).

Clip depth is corrected with `z' = 2z − w` only when `IDevice::getNormalizedZRange()` reports
`NegOneToOne` (the OpenGL backend); XNA's projections target Direct3D's `[0, w]`.

## Shaders

One **uber-shader** serves every stock effect. Lighting, fog, texturing, alpha test, dual texture,
env mapping, skinning and PBR are uniforms in the `CnaEffect` std140 block (`uFlags.x` is the
feature bitmask), not preprocessor variants. Only two things are real shader variants:

* the **vertex attribute mask** — Vulkan rejects a shader input with no matching vertex attribute;
* the **colour attachment count** — a Vulkan pipeline requires the fragment outputs to match the
  render pass.

The generated GLSL differs per backend in exactly one respect, the resource declarations:

| | OpenGL (4.1 core) | Vulkan |
|---|---|---|
| Samplers | `uniform sampler2D uTexture0;` — the unit comes from `RenderPipelineDesc::fragmentUnitSamplerMap` | `layout(set = 0, binding = 0) uniform sampler2D uTexture0;` |
| Uniform blocks | `layout(std140) uniform CnaEffect {…}` — the binding comes from `uniformBlockBindingMap` | `layout(set = 1, binding = 0, std140) uniform CnaEffect {…}` |

Set 0 for combined image samplers and set 1 for buffers are IGL's own fixed descriptor layout
(`VulkanContext`'s `kBindPoint_CombinedImageSamplers` / `kBindPoint_Buffers`).

### Writing a custom `ShaderEffect` for this renderer

Attribute locations follow the renderer's own slot table:

| Location | Name | Type |
|----------|------|------|
| 0 | `aPosition` | `vec3` (sprite Z carries layer depth) |
| 1 | `aNormal` | `vec3` |
| 2 | `aColor` | `vec4` |
| 3 | `aTexCoord0` | `vec2` |
| 4 | `aTexCoord1` | `vec2` |
| 5 | `aBlendIndices` | `vec4` |
| 6 | `aBlendWeights` | `vec4` |
| 7 | `aTangent` | `vec4` |

The **names** matter on the OpenGL backend — IGL locates a vertex attribute by name there — so a
custom shader must use them, not only the locations.

## Building

See `plan_igl.md` §6. `cmake/ThirdPartyIGL.cmake` deliberately fetches only the dependencies the
four IGL library targets actually include (`glm`, `fmt`, `glslang`, `SPIRV-Headers`, plus `volk` and
`vma` for Vulkan) rather than running IGL's own `deploy_deps.py`, which downloads over a gigabyte of
sample/shell/test dependencies CNA never links.
