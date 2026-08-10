# FNA3D renderer

`-DCNA_GRAPHICS_RENDERER=FNA3D` selects CNA's FNA3D renderer: XNA's programming model rendered
through **FNA3D**, the C graphics library FNA itself renders through.

## Identity

| Field | Value |
|---|---|
| Public identity | `FNA3D` (`GraphicsRendererType::Fna3d`) |
| Compile definition | `CNA_RENDERER_FNA3D` |
| Module | `modules/renderers/fna3d` |
| CMake target | `cna_renderer_fna3d` |
| Namespace | `CNA::Internal::Renderers::Fna3d` |
| Main class | `Fna3dRenderer` |
| Category | Portable RHI (like `LLGL`, `DILIGENT`, `SOKOL`, `BGFX`) — not one native API |

## Upstream

| Field | Value |
|---|---|
| Repository | https://github.com/FNA-XNA/FNA3D |
| Pinned revision | `3240147` (release tag **26.08**) |
| License | zlib |
| Vendored submodule | MojoShader (`icculus/mojoshader`, `6333f74`), built from FNA3D's own CMake |
| Dependencies | **SDL 3.2.0 or newer, and nothing else** — the very SDL3 CNA already vendors |
| Integration | `cmake/ThirdPartyFNA3D.cmake`, FetchContent at the pin, built as a static archive |
| Offline builds | `-DFETCHCONTENT_SOURCE_DIR_FNA3D=/path/to/FNA3D` (checkout must have its MojoShader submodule initialized) |

FNA3D's own API design is XNA 4.0: `FNA3D_DrawIndexedPrimitives`, `FNA3D_SetBlendState`,
`FNA3D_VerifySampler`, `FNA3D_SetRenderTargets`, `FNA3D_ResolveTarget` and the rest map to
`IGraphicsRenderer` almost one to one, and **every enumeration it exposes is numerically the XNA
enumeration CNA already ports** — pinned by `static_assert` in `Fna3dEnumMapping.hpp` so a future
divergence on either side is a compile error rather than a silently wrong blend mode.

## Actual graphics route

```
CNA game code
  -> Microsoft::Xna::Framework::Graphics (GraphicsDevice / SpriteBatch / BasicEffect / ...)
  -> CNA::Internal::Renderers::Fna3d::Fna3dRenderer   (IGraphicsRenderer)
  -> FNA3D                                            (chooses its driver at runtime)
  -> SDL_GPU  |  Direct3D 11  |  OpenGL
```

FNA3D tries its drivers in the order **SDL_GPU, Direct3D 11, OpenGL** and reports the SDL window
flags the chosen driver needs. Pin a driver with the `FNA3D_FORCE_DRIVER` SDL hint
(`OpenGL`, `SDL_GPU`, and the aliases `Vulkan`/`D3D12`/`Metal`, which FNA3D maps onto SDL_GPU
backends). CNA's own tests set `FNA3D_FORCE_DRIVER=OpenGL` so a machine with a partially
functional Vulkan stack cannot silently change which driver the assertions were written against.

Driver selection happens *before* the window exists, because
`FNA3D_PrepareWindowAttributes()` also primes the GL attributes the window's visual is chosen
from. `GraphicsDevice::getRendererWindowFlags()` therefore calls
`Fna3d::Detail::PrepareWindowFlags()` while assembling the SDL window flags — the same
runtime-decides-the-flag shape LLGL, Diligent and bgfx already use.

## Shaders: the one thing FNA3D constrains

`FNA3D_CreateEffect`, which takes a **compiled Direct3D 9 Effect Framework binary** and runs it
through MojoShader, is the *only* shader entry point in `FNA3D.h`. There is no call anywhere in
the library that compiles a GLSL or HLSL source string, and the drivers refuse to draw without a
bound MojoShader program.

This renderer therefore draws through the XNA stock effects, vendored under
`modules/renderers/fna3d/effects/` (provenance and license in that directory's README) and
embedded into a generated header at build time. It is the only CNA renderer that executes **XNA's
actual shader programs** rather than a reimplementation of them: `BasicEffect`, `AlphaTestEffect`,
`DualTextureEffect`, `EnvironmentMapEffect` and `SkinnedEffect` are the real compiled artefacts,
and the variant is chosen with the same integer `ShaderIndex` arithmetic XNA's own `OnApply()`
computes (`Fna3dStockEffects.hpp`, unit-tested exhaustively).

The direct consequence is that **`GraphicsCapability::CustomEffects` is false** and
`CreateEffectRenderer` returns null: a caller's `ShaderEffect` GLSL/HLSL source cannot be compiled
by anything FNA3D offers, and claiming otherwise would be a promise broken at the first custom
effect.

## Capabilities

| Capability | Value | Notes |
|---|---|---|
| `ThreeD` | true | Full `Draw*Primitives` routes |
| `DepthStencilBuffer` / `StencilBuffer` | true | Backbuffer and per-target depth/stencil renderbuffers |
| `MultiSampleAntiAlias` | device-dependent | Answered from `FNA3D_GetMaxMultiSampleCount` |
| `MultipleRenderTargets` | true | `FNA3D_SetRenderTargets` takes the whole ordered set |
| `AnisotropicFiltering` | true | `FNA3D_SamplerState::maxAnisotropy` |
| `WireFrame` | true | `FNA3D_FILLMODE_WIREFRAME` |
| `OcclusionQuery` | true | `FNA3D_CreateQuery`/`QueryPixelCount` |
| `Texture3D` | true | `FNA3D_CreateTexture3D` |
| `Instancing` | device-dependent | Answered from `FNA3D_SupportsHardwareInstancing` |
| `AdditiveBlending` | true | |
| **`MultiStreamVertexInput`** | **true** | Native: `FNA3D_ApplyVertexBufferBindings` takes an array of per-stream declarations, each with its own stride, vertex offset and instance frequency |
| **`CustomEffects`** | **false** | See above — no source-string shader compilation exists in FNA3D |

`GetMaxVertexStreams()` reports XNA's own 16-binding ceiling.

## Implemented

- Device creation on CNA's SDL3 window; teardown releases every effect, buffer and the device.
- Clear (colour / depth / stencil and every combination), present, backbuffer readback.
- Presentation modes Letterbox / Overscan / Stretch / NativeBackBuffer / FixedHeightDynamicWidth,
  expressed as the destination rectangle `FNA3D_SwapBuffers` already takes, plus the matching
  window↔logical coordinate transforms.
- `Texture2D`, `Texture3D`, `TextureCube`: creation, mip levels, sub-rectangle upload and readback.
- `RenderTarget2D` and `RenderTargetCube`: MSAA colour renderbuffers with resolve-on-unbind, mip
  generation, per-target depth/stencil renderbuffers, `PreserveContents`, MRT sets, readback.
- Vertex and index buffers (16- and 32-bit) with `SetDataOptions` forwarded verbatim, growing the
  GPU allocation on demand.
- The caller's own `VertexDeclaration` bound verbatim per stream; a stride-keyed table stands in
  only for the internal routes that bind no public buffer (SpriteBatch, `DrawUser*`).
- Blend / depth-stencil / rasterizer / sampler state, colour write masks, multisample mask, blend
  factor, standalone reference stencil, scissor and viewport.
- 2D `SpriteBatch` through the stock `SpriteEffect`: tint, source rectangles, rotation about a
  scaled origin, both flips, layer depth, sampler filter and address modes, Immediate flushing.
- 3D through the stock effects, including instanced draws.
- Hardware occlusion queries.
- `SetStringMarkerEXT` → `FNA3D_SetStringMarker`.

## Not supported, and why

| Area | Behaviour |
|---|---|
| Custom `ShaderEffect` (GLSL/HLSL source) | `CreateEffectRenderer` returns null; `CustomEffects` is false. FNA3D compiles no shader source. |
| `DrawMeshEXT` | Inherits the shared refusal — that is Skia's bounded `SkVertices` ABI. |
| Render-target array slices | `SetRenderTargets` throws for a non-zero `arraySlice`; CNA exposes no texture arrays and FNA3D's binding has no slice field. |
| Unknown vertex stride with no `VertexDeclaration` | Throws, naming the stride. FNA3D binds real per-stream declarations and this renderer will not guess a layout. |
| Out-of-contract state ordinals | Throw, naming the state and the ordinal, instead of casting into an undefined FNA3D enumerator. |
| Context-loss simulation | Not implemented; FNA3D exposes no device-loss surface, and the shared `DebugSimulateContextLoss` default is a no-op. |

### Backbuffer readback quirk (upstream, worked around here)

`FNA3D_ReadBackbuffer`'s **sub-rectangle** origin is not the same on every driver: the OpenGL
driver forwards `(x, y)` straight to `glReadPixels`, whose origin is the bottom-left, and only
then flips the returned rows — so a full-frame read comes back correctly top-row-first while a
sub-rectangle read comes back from the mirrored row range. The D3D11 driver, routing through
`GetTextureData2D`, uses the top-left origin. This was measured, not inferred:
`fna3d-spike/fna3d_sprite_spike.c` renders a quad at rows 12–35, finds it there in a full-frame
read, and gets the cleared colour from the equivalent sub-rectangle read.

CNA's contract is unambiguous ("x, y are top-left in game coordinates"), so `Fna3dRenderer`
reads the whole backbuffer — which every driver agrees is top-row-first — and crops in CNA.
Readback is already a full CPU/GPU sync point FNA3D documents as screenshot-only.

## Validation

| Kind | Status |
|---|---|
| Native runtime (Linux, Xvfb + Mesa llvmpipe, FNA3D OpenGL driver) | **Performed** — all six `Fna3d_*` CTest binaries pass, all pixel oracles |
| Unit tests (`CnaTests`, `Fna3d*`) | **Performed** — 30 tests |
| Existence-gate spikes | **Performed** — `fna3d-spike/` |
| SDL_GPU driver | **Not exercised here**: this container has no Vulkan ICD, so FNA3D declines SDL_GPU and falls through to OpenGL. The code path is driver-agnostic; the gate is external. |
| Direct3D 11 driver | **Not exercised here**: Windows-only (or DXVK-native). External gate. |
| macOS / iOS | Not exercised. External gate. |

## Tests

| CTest name | What it proves |
|---|---|
| `Fna3d_Smoke` | Identity, device creation, clear + readback at three points, repeated frames, texture upload/readback through the renderer (not the CPU shadow), buffer counts, occlusion query |
| `Fna3d_2D` | SpriteBatch placement, non-bleed on all four sides, tint, source rectangles, flips, rotation, point-sampling texel edges |
| `Fna3d_3D` | BasicEffect vertex-colour/diffuse/textured variants, indexed route, depth test both orders, AlphaTestEffect discard and keep, wireframe |
| `Fna3d_RenderTarget` | Target clear/readback, geometry placement inside a target, unbind restores the backbuffer, sampling a rendered target, depth/stencil reporting, MSAA resolve, `PreserveContents` |
| `Fna3d_State` | AlphaBlend, Additive, colour write masks, BlendFactor, scissor on/off, a non-default depth comparison, stencil write-then-test, viewport sub-rectangle |
| `Fna3d_Capabilities` | Every capability answer matched against a real factory or a real refusal, and both rejection paths |

`CnaTests --gtest_filter=Fna3d*` covers the device-free logic: the `ShaderIndex` arithmetic against
an independently transcribed reference, the presentation layout and its inverse transform, the
enum bridge's range rejection, and the stride table.
