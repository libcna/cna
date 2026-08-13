# TINYGL renderer

Capability boundary for `CNA_GRAPHICS_RENDERER=TINYGL`, CNA's fixed-function CPU OpenGL renderer.
Task breakdown and design decisions are in [`../plan_tinygl.md`](../plan_tinygl.md); the
pre-implementation probe that established the constraints below is in
[`../tinygl-spike/README.md`](../tinygl-spike/README.md).

Upstream: [C-Chads/tinygl](https://github.com/C-Chads/tinygl), the maintained fork of Fabrice
Bellard's TinyGL, pinned at commit `36a7987e`. A CPU implementation of a fixed-function OpenGL 1.x
subset — **no shaders exist anywhere in its design**.

## What it is, and what it is not

Like `HEADLESS`, `SOFTWARE`, `STUB` and `PORTABLEGL`, this renderer opens no window, initializes no
SDL video subsystem, and needs no GPU driver or display server. Unlike `SOFTWARE`, the rasterizer
is not CNA's — every transform, clip, cull, raster, texel fetch and blend is a real TinyGL call.

It is **not** an alias of an existing identity:

| | Pipeline model | Implementation |
|---|---|---|
| `OPENGL1` | fixed-function GL 1.x | a real GL driver |
| `PORTABLEGL` | shader-era GL 3.x | CPU (`rswinkle/PortableGL`) |
| `SOFTWARE` | CNA's own | CNA's own rasterizer |
| **`TINYGL`** | **fixed-function GL 1.x** | **CPU (C-Chads/tinygl)** |

## Verified capability status

✅ = a real implementation, a public CNA path that reaches it, and a permanent test that fails if
it is removed. ❌ = not implemented, and **refused deterministically** rather than silently
no-opped. ⚠️ = accepted, but executed as a documented approximation (see the section below).

| Feature | Status | Notes |
|---|---|---|
| Clear (colour, depth) | ✅ | `glClearColor`/`glClear`. `ClearDepth`'s value is ignored — TinyGL has no `glClearDepth` equivalent and always clears to its own far value. |
| Backbuffer readback | ✅ | Direct `ZBuffer::pbuf` access; upstream `glReadPixels` is a stub. |
| Resize | ✅ | `ZB_resize`, no context teardown. |
| `Texture2D` create/update/`GetData` | ✅ | `GetData` is exact (CPU shadow), alpha included. |
| 3D `VertexPositionColor` (stride 16) | ✅ | `glDrawArrays`. |
| 3D `VertexPositionColorTexture` (stride 24) | ✅ | Texel × vertex colour, which is XNA's own modulate. |
| Indexed draws | ✅ | `glArrayElement` inside `glBegin`/`glEnd` — TinyGL has **no `glDrawElements`**. |
| Draw offsets (`vertexStart`, `startIndex`, `baseVertex`, `VertexOffset`) | ✅ | |
| `BasicEffect`: `VertexColorEnabled`, `DiffuseColor`, `Alpha`, `TextureEnabled` | ✅ | |
| `SpriteBatch` (source/dest rects, tint, rotation, origin, flips) | ✅ | Real textured quads. |
| World/View/Projection | ✅ | TinyGL's own `GL_PROJECTION`/`GL_MODELVIEW` stacks. |
| `CullMode` | ✅ | `glFrontFace(GL_CW)` + `glCullFace`. |
| `FillMode.WireFrame` | ✅ | `glPolygonMode(GL_LINE)`. |
| Depth test / depth write | ✅ | Comparison is fixed at `LessEqual`, see below. |
| Blending — the executable subset | ✅ | See the blend table below. |
| Viewport | ✅ | Depth range must be 0..1. |
| `VertexDeclaration` fidelity guard | ✅ | A declaration that puts something else in the same stride is refused, not reinterpreted. |
| `BlendState.AlphaBlend` / `.NonPremultiplied` | ⚠️ | Executed as a 1-bit colour-key cutout. |
| `SamplerState` (filter, address mode) | ⚠️ | Inert: sampling is always nearest + wrap. |
| Texture resolution fidelity | ⚠️ | Every texture is resampled to 256×256 by TinyGL. |
| Stencil (test, ops, reference) | ❌ | No stencil plane exists. |
| Depth comparison other than `LessEqual` | ❌ | No `glDepthFunc`. |
| `BlendState.Additive` and other alpha factors | ❌ | No alpha factors in the rasterizer. |
| Partial `ColorWriteChannels` | ❌ | No `glColorMask`. |
| Scissor test | ❌ | No `glScissor`. |
| Depth bias | ❌ | `glPolygonOffset` stores without applying. |
| Viewport depth range ≠ 0..1 | ❌ | No `glDepthRange`. |
| Render targets (2D, cube, MRT) | ❌ | One framebuffer per context, no FBO concept. |
| `TextureCube`, `Texture3D` | ❌ | No cube or volume texture type. |
| Custom `Effect`, `SkinnedEffect`, PBR, per-pixel lighting | ❌ | No shader stage of any kind. |
| `BasicEffect` lighting, fog, alpha test | ❌ | Not wired to TinyGL's own `glLight*` pipeline yet — `plan_tinygl.md` `TINYGL-16`. |
| MSAA, anisotropic filtering, mip levels | ❌ | |
| Instancing, multi-stream vertex input | ❌ | |
| Occlusion queries | ❌ | |

`SupportsCapability()` returns true for `ThreeD` and `WireFrame` only. Everything else in
`CNA::GraphicsCapability` reports false, including `DepthStencilBuffer` (the depth half is real; the
pair the capability names is not) and `AdditiveBlending`.

## Blending

TinyGL's rasterizer switches on exactly this set, on RGB, with no alpha channel anywhere:

| Slot | Accepted `Blend` values |
|---|---|
| Source | `One`, `Zero`, `InverseSourceColor` |
| Destination | `One`, `Zero`, `InverseDestinationColor` |
| Equation | `Add`, `Subtract`, `ReverseSubtract` |

The asymmetry between the slots is upstream's: the source switch has a case for
`GL_ONE_MINUS_SRC_COLOR` and none for `GL_ONE_MINUS_DST_COLOR`, and vice versa. Anything outside
the set falls through to the switch's `default:` and behaves as `GL_ONE`, which is why this renderer
refuses it instead of forwarding it.

`ApplyBlendState()` has exactly three outcomes and no fourth:

1. All factors and equations in the set above → installed exactly. `BlendState::Opaque`
   `(One, Zero) + Add` is the identity and switches blending off.
2. `BlendState::AlphaBlend` or `BlendState::NonPremultiplied`, matched on their **complete**
   factor+function signature → executed as the colour-key cutout below.
3. Everything else, `BlendState::Additive` included → `System::NotSupportedException`.

A `BlendState` whose RGB and alpha halves disagree is refused: TinyGL applies one factor pair to the
whole pixel and has no alpha channel to apply the second pair to.

## The three recorded approximations

These are accepted rather than refused because refusing them would refuse XNA's own defaults and
leave a renderer that can only throw. Each is tested, and each reports `false` from the relevant
capability query.

### 1. Transparency is 1-bit

TinyGL has no alpha, but it does have `TGL_NO_DRAW_COLOR` (`0xFF00FF`): its triangle rasterizer
discards any textured fragment whose texel matches that key. On upload, texels with alpha below
`TinyGLTextureRenderer::kAlphaCutoutThreshold` (**128**) are written as the key colour, so TinyGL
performs a real per-fragment discard. Alpha is thresholded, never interpolated — a half-transparent
sprite is either fully drawn or fully absent, with no blending in between.

An opaque texel that genuinely *is* `0xFF00FF` would otherwise disappear; it is uploaded as
`0xFF01FF` (green nudged by one) so it stays visible. Both behaviours are asserted by
`TinyGL_TextureSprite`.

### 2. Sampler state is inert

`glTexParameteri` is an upstream no-op (`glopTexParameter` is commented out in `texture.c`), and the
texel fetch masks the fixed-point S/T coordinates against the texture dimension — which is wrap
addressing with a single nearest sample, and nothing else. Any `SamplerState` is therefore accepted
and sampled that way, `SamplerState::LinearClamp` (XNA's `SpriteBatch` default) included.
`TextureFilter::Anisotropic` is still refused, because `SupportsCapability(AnisotropicFiltering)`
reports false.

`GraphicsDevice.SamplerStates` slots are likewise recorded and inert.

### 3. Every texture is resampled to 256×256

`glTexImage2D` rescales every upload to `TGL_FEATURE_TEXTURE_DIM` with nearest-neighbour and no
interpolation. `Texture2D.Width`/`Height` keep reporting the size the game asked for — that is what
the XNA contract requires — so this is a sampling-fidelity loss, not an API-surface one. Normalized
UVs are unaffected; texel-exact expectations are not.

## Colour precision

TinyGL interpolates colours in fixed point, so a channel can read one LSB below the value that was
requested (a requested 255 measures 254; a requested 64/128/191 clear measures 63/127/191). Pixel
expectations against this renderer need a tolerance of about 2, and the shipped test suites use one.

## Known limitations beyond the table

- **One renderer per process.** TinyGL keeps its context in a file-scope global (`glInit`/`glClose`)
  with no make-current entry point, so constructing a second `TinyGLRenderer` throws.
- **Verified on Linux x86_64 only.** Nothing in the renderer is platform-specific and no platform
  gate is declared, but no other host has been built or run (`plan_tinygl.md` `TINYGL-19`).
- **An unsupported argument reaching TinyGL kills the process.** Upstream calls `gl_fatal_error()`
  instead of setting an error flag. Every validation in this renderer runs *before* the native call
  for that reason; `TinyGL_Rejection` is the suite that keeps it that way. If you extend this
  renderer, keep new validation on the same side of the call.

## Build and test

```bash
cmake -S . -B cmake-build-tinygl -DCMAKE_BUILD_TYPE=Debug \
      -DCNA_GRAPHICS_RENDERER=TINYGL -DCNA_BUILD_TESTS=ON
cmake --build cmake-build-tinygl -j4
(cd cmake-build-tinygl && ctest -R TinyGL --output-on-failure)
```

TinyGL is fetched at configure time; `-DFETCHCONTENT_SOURCE_DIR_TINYGL=/path/to/tinygl` points at an
existing checkout for an offline build. An OpenMP-capable toolchain is required — see
`plan_tinygl.md` §Build for why.

Four suites, 37 checks: `TinyGL_Smoke` (10), `TinyGL_TextureSprite` (7), `TinyGL_State` (9),
`TinyGL_Rejection` (11). All pass.
