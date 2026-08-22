# PortableGL renderer — capability boundary

`CNA_GRAPHICS_RENDERER=PORTABLEGL` renders through
[rswinkle/PortableGL](https://github.com/rswinkle/PortableGL), a single-header C99 CPU software
implementation of an OpenGL-3.x-shaped API. There is no GPU, no window and no SDL video subsystem:
the renderer owns a `glContext` whose framebuffer is ordinary RAM, `Present()` is a no-op, and
pixel truth is read back with `GraphicsDevice.GetBackBufferData()` — the same architectural shape
as `HEADLESS`, `SOFTWARE` and `STUB`. Unlike `SOFTWARE`'s hand-rolled rasterizer, every stage here
is a real PortableGL call: `glGenBuffers`/`glBufferData`/`glVertexAttribPointer`,
`pglCreateProgram` with real C vertex and fragment shader callbacks, `glDrawArrays`/
`glDrawElements`, `glClear`, `glViewport`, `glScissor`, `glTexImage2D`, `glBlendFuncSeparate`,
`glStencilOpSeparate`, `glCullFace`, `glPolygonMode`.

Upstream is pinned at tag **0.100.0** (commit `63a55db75ab07619797a93ff9bf3909355d27950`, MIT),
fetched at configure time by `cmake/ThirdPartyPortableGL.cmake`. Nothing in the vendored header is
patched; where PortableGL's own API cannot express a CNA semantic, the renderer either emulates it
with the API it does have (see `baseVertex` below) or refuses the operation.

**This renderer is a bounded CPU 3D path, not an XNA parity target.** Everything outside the
boundary in "What is not supported" fails loudly — a `System::NotSupportedException` naming the
missing capability, or one raised by the shared layer because the renderer creates no resource —
never a silent no-op.

## Configure

```bash
cmake -S . -B cmake-build-portablegl -DCMAKE_BUILD_TYPE=Debug \
      -DCNA_GRAPHICS_RENDERER=PORTABLEGL -DCNA_BUILD_TESTS=ON
cmake --build cmake-build-portablegl -j4
ctest --test-dir cmake-build-portablegl -R PortableGL --output-on-failure
```

| Option | Values | Default | Notes |
|---|---|---|---|
| `CNA_PORTABLEGL_GIT_TAG` | commit SHA | `63a55db…` (tag 0.100.0) | The pinned upstream commit fetched at configure time. |
| `FETCHCONTENT_SOURCE_DIR_PORTABLEGL` | path | — | CMake's own per-dependency override; point it at an existing PortableGL checkout for offline builds. |

No system graphics package is required: PortableGL is CPU-only.

## What works

| Feature | Evidence |
|---|---|
| Back buffer, `Clear` and all six depth/stencil clear variants, `GetBackBufferData` (top row first) | `PortableGL_Smoke`, `PortableGL_EdgeCases` |
| Back-buffer resize (`GraphicsDevice.Reset`) | `PortableGL_DepthResize` checks D–E |
| `VertexBuffer`/`IndexBuffer` (16- and 32-bit), texture creation and `Texture2D.SetData` updates | `PortableGL_Smoke`, `PortableGL_PipelineInterleave` |
| `DrawPrimitives` / `DrawIndexedPrimitives` with real `vertexStart`, `startIndex`, `baseVertex` and `VertexBufferBinding.VertexOffset` | `PortableGL_DrawOffsets` |
| Unlit `BasicEffect` subset: `VertexPositionColor` with `VertexColorEnabled`/`DiffuseColor`/`Alpha`, and `VertexPositionTexture` with texture 0 plus device sampler slot 0 | `PortableGL_EffectState`, `PortableGL_TexturedDraw` |
| `BlendState`: separate RGB/alpha factors and equations, the whole `Blend` enum, `BlendFactor`, target-0 `ColorWriteChannels` | `PortableGL_Blend` |
| `DepthStencilState`: depth test/write/function **and** the full stencil half, including two-sided stencil and standalone `GraphicsDevice.ReferenceStencil` | `PortableGL_DepthResize`, `PortableGL_Stencil` |
| `RasterizerState`: `CullMode` (both directions and `None`), `FillMode.WireFrame`, `ScissorTestEnable`, `DepthBias`/`SlopeScaleDepthBias` | `PortableGL_RasterState` |
| `Viewport` (including `MinDepth`/`MaxDepth`) and `ScissorRectangle`, converted from XNA's top-left origin | `PortableGL_RasterState` checks A–D |
| Textured `SpriteBatch` with the batch's own resolved `SamplerState` (`Point`/`Linear`, `Clamp`/`Wrap`/`Mirror`), tint, rotation, origin and `SpriteEffects` | `PortableGL_Sampler`, `PortableGL_TexturedDraw`, `PortableGL_EdgeCases` |
| Interleaving 3D draws and `SpriteBatch` draws in one frame without state leaking between them | `PortableGL_PipelineInterleave` |

`SupportsCapability()` reports `true` for exactly `ThreeD`, `DepthStencilBuffer`, `StencilBuffer`,
`AdditiveBlending` and `WireFrame` — each of which has an implementation, a public CNA path that
reaches it, and one of the permanent tests above.

## What is not supported, and how it fails

| Feature | Why | How it fails |
|---|---|---|
| `RenderTarget2D` / `RenderTargetCube` / MRT | A PortableGL context owns exactly one framebuffer, and there is no off-screen attachment mechanism to bind. | `CreateRenderTarget2D`/`CreateRenderTargetCube` create nothing, so `GraphicsDevice::SetRenderTargets` raises `System::NotSupportedException`; `PortableGLRenderer::SetRenderTargets`/`SetRenderTarget2D`/`SetRenderTargetCubeFace` refuse a non-empty binding as well. `PortableGL_Rejection` check D. |
| Custom `Effect` (`ShaderEffect`) | PortableGL's shader stage is a pair of C function pointers; there is no compiler for a CNA `Effect` to target. | `CreateEffectRenderer` returns null and `PortableGLSpriteBatchRenderer::SetCustomEffect` throws. `PortableGL_Rejection` check A. |
| Lit / fogged / alpha-tested `BasicEffect`, `DualTextureEffect`, `EnvironmentMapEffect`, `SkinnedEffect`, `PbrEffect`, `AlphaTestEffect` | The bounded 3D path implements only unlit colored and single-texture BasicEffect programs. | `Draw*PrimitivesEx` refuses the configuration before any native draw. `PortableGL_EffectState` check G. |
| Vertex layouts other than `VertexPositionColor` (stride 16) and `VertexPositionTexture` (stride 20) | The two 3D programs declare exactly those standard XNA layouts. | The draw is refused; a different declaration that shares a supported stride is refused too through `RequireFaithfulVertexDeclaration`, rather than reinterpreted. `PortableGL_Rejection` checks B–C. |
| Instancing, multi-stream vertex input | Only one stream is bound. | `DrawInstancedPrimitives` throws; a second bound stream is rejected by `RejectUnsupportedStreamCombination`. |
| `TextureCube`, `Texture3D`, `OcclusionQuery` | Not implemented. | The factory methods create nothing, so the shared layer raises `System::NotSupportedException`. |
| MSAA, `BlendState.MultiSampleMask` | PortableGL rasterizes one sample per pixel. | `MultiSampleAntiAliasing` reports false and `GetMultiSampleCount()` is 0; a non-default coverage mask is a documented gap, the same one EasyGL records for this field. |
| Anisotropic filtering, and any `TextureFilter` whose minification and magnification components differ | `texture2D()` samples with a single per-texture filter, and a PortableGL texture has exactly one mip level. | `SpriteBatch.Begin` with such a `SamplerState` throws. `PortableGL_Sampler` check E. |
| Mip mapping | No mip storage; `glGenerateMipmap` is an upstream no-op. | Mip levels above 0 are not uploaded to the renderer; `Texture2D`'s own CPU shadow answers `GetData`. |
| Mip selection in `GraphicsDevice.SamplerStates[n]` | PortableGL stores one image level only. The filter and U/V address components of slot zero are real for textured 3D; `MaxMipLevel` and LOD bias have no level to select. | The mip half is accepted and inert; filters PortableGL cannot express are refused when the texture is sampled. |
| A requested `SurfaceFormat`/`DepthFormat` other than `Color`/`Depth24Stencil8` | A PortableGL context has one fixed pixel layout and always allocates a combined 24-bit depth + 8-bit stencil buffer. | `GetAppliedBackBufferFormatEXT`/`GetAppliedDepthStencilFormatEXT` report what really exists, so `PresentationParameters` describes the surface rather than echoing the request. |

## Deliberate divergences worth knowing about

**Colour conversion.** PortableGL converts a fragment's `[0,1]` float colour to bytes by
*truncating* `v * 255` (its own `v4_to_Color` documents that choice), and it does so after
blending. This renderer's fragment shaders therefore quantize their output onto the 8-bit grid
before returning it, which removes the interpolation noise that would otherwise cost a whole LSB
and makes every unblended write byte-exact. The quantization is deliberately *not* a bias: alpha 0
stays exactly 0 and alpha 255 stays exactly 1, so a transparent source leaves the destination
bit-for-bit unchanged and blending consumes the exact colour the caller asked for. What remains is
that the **blended** result is still truncated upstream, so a partial-alpha composite can land one
LSB below what a round-to-nearest GPU writes. Blend checks whose factors are exactly 0 or 1 are
asserted byte-exactly; only the destination term is given a one-LSB tolerance.

**`baseVertex` emulation.** PortableGL 0.100.0 exposes no `glDrawElementsBaseVertex`. Because its
vertex fetch reads attribute *i* from `buffer + offset + stride * i`, the renderer advances
`offset` by `baseVertex * stride` instead — arithmetically identical, with no index staging and no
change to the caller's index buffer. The declared decoded-index window
(`minVertexIndex`/`numVertices`) is bounds-checked against the buffer before submission.

**Scissor.** PortableGL folds the scissor rectangle into its always-on rasterizer clip bounds the
moment `glScissor` is called, whether or not `GL_SCISSOR_TEST` is enabled — and `glClear` with the
test disabled clears `ux * uy` pixels linearly. The renderer therefore hands the rectangle over
only while the test is enabled and relies on `glDisable(GL_SCISSOR_TEST)` to restore full bounds.

**Winding.** `glFrontFace(GL_CCW)` is stated explicitly. A counter-clockwise-on-screen triangle is
the front face, so `CullMode.CullCounterClockwiseFace` culls `GL_FRONT` and the
`CounterClockwiseStencil*` half of a two-sided `DepthStencilState` installs on `GL_FRONT` — the
same orientation the Vulkan renderer's own differential two-sided-stencil finding settled on.
`PortableGL_Stencil` check F is the differential test that pins it here, and fails if the two faces
are swapped.

**Polygon offset.** `GL_POLYGON_OFFSET_FILL` is enabled only for a genuinely non-zero bias:
upstream's `calc_poly_offset()` divides by the first edge's `dx`/`dy` without guarding a zero
denominator, so keeping it out of the pipeline for the dominant zero-bias case avoids a NaN depth
slope for axis-aligned depth-constant geometry.

## Tests

All twelve suites are registered with CTest under the `PortableGL` label and run in every
`-DCNA_GRAPHICS_RENDERER=PORTABLEGL -DCNA_BUILD_TESTS=ON` build:

`PortableGL_Smoke`, `PortableGL_TexturedDraw`, `PortableGL_PipelineInterleave`,
`PortableGL_DepthResize`, `PortableGL_EdgeCases`, `PortableGL_DrawOffsets`,
`PortableGL_EffectState`, `PortableGL_Blend`, `PortableGL_Stencil`, `PortableGL_RasterState`,
`PortableGL_Sampler`, `PortableGL_Rejection`.

Each is a real `Game` that draws through the ordinary public XNA API and reads the back buffer
back; none asserts merely that a call returned.
