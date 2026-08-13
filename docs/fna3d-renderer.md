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
| **`Instancing`** | **false** | FNA3D instances natively, but none of XNA's compiled stock effects declares a per-instance vertex input, so a per-instance stream could not reach a shader that reads it. `DrawInstancedPrimitivesEx` refuses with the same reason. |
| `AdditiveBlending` | true | |
| **`MultiStreamVertexInput`** | **true** | Native: `FNA3D_ApplyVertexBufferBindings` takes an array of per-stream declarations, each with its own stride, vertex offset and instance frequency |
| **`CustomEffects`** | **false** | See above — no source-string shader compilation exists in FNA3D |

`GetMaxVertexStreams()` reports XNA's own 16-binding ceiling.

## Implemented

- Device creation on CNA's SDL3 window; transactional setup rolls back effects and the device on
  failure. Resource wrappers share a liveness/identity token, so they can be destroyed safely
  after their native device and reject post-device use deterministically.
- Clear (colour / depth / stencil and every combination), present, backbuffer readback.
- Presentation modes Letterbox / Overscan / Stretch / NativeBackBuffer / FixedHeightDynamicWidth,
  expressed as the destination rectangle `FNA3D_SwapBuffers` already takes, plus the matching
  window↔logical coordinate transforms.
- `Texture2D`, `Texture3D`, `TextureCube`: creation, mip levels, sub-rectangle upload and readback.
- `RenderTarget2D` and `RenderTargetCube`: MSAA colour renderbuffers with resolve-on-unbind, mip
  generation, per-target depth/stencil renderbuffers, `PreserveContents`, MRT sets, readback.
- Vertex and index buffers (16- and 32-bit) with `SetDataOptions` forwarded verbatim, growing the
  GPU allocation on demand and rejecting byte counts beyond FNA3D's signed 32-bit ABI limit.
- The caller's own `VertexDeclaration` bound verbatim per stream; a stride-keyed table stands in
  only for the internal routes that bind no public buffer (SpriteBatch, `DrawUser*`).
- Blend / depth-stencil / rasterizer / sampler state, colour write masks, multisample mask, blend
  factor, standalone reference stencil, scissor and viewport.
- 2D `SpriteBatch` through the stock `SpriteEffect`: tint, source rectangles, rotation about a
  scaled origin, both flips, layer depth, sampler filter and address modes, Immediate flushing;
  large batches are split at 16,384 quads before their 16-bit indices can wrap, and deferred
  texture renderers are retained through backend `End` so queued submissions cannot dangle.
- 3D through the stock effects (not instanced — see the capability table).
- Hardware occlusion queries.
- `SetStringMarkerEXT` → `FNA3D_SetStringMarker`.
- Driver limits queried once at device creation — `FNA3D_SupportsDXT1` / `SupportsS3TC` /
  `SupportsBC7` / `SupportsSRGBRenderTargets` and `FNA3D_GetMaxTextureSlots` — and enforced: a
  texture or render target in a format the driver has no storage for is refused by name, and so is
  a bind past the driver's real fragment sampler-slot count.
- Format-correct transfer sizing for every `SurfaceFormat`, block-compressed families included:
  `ceil(w/4) * ceil(h/4) * blockBytes`, with the row pitch counted in block rows and compressed
  sub-regions constrained to 4×4 block boundaries (except a legal NPOT mip edge tail).
- `SetDataOptions::NoOverwrite` gated on `FNA3D_SupportsNoOverwrite` and downgraded to `None`
  where the driver has no such fast path.
- `FNA3D_SetTextureName` on textures and render targets, so a RenderDoc/apitrace capture of a CNA
  frame shows named resources.
- `FNA3D_LinkedVersion()` checked against `FNA3D_COMPILED_VERSION` at device creation, so a
  mismatched shared FNA3D is reported rather than silently trusted.

## Not supported, and why

| Area | Behaviour |
|---|---|
| Custom `ShaderEffect` (GLSL/HLSL source) | `CreateEffectRenderer` returns null; `CustomEffects` is false. FNA3D compiles no shader source. |
| `DrawMeshEXT` | Inherits the shared refusal — that is Skia's bounded `SkVertices` ABI. |
| Render-target array slices | `SetRenderTargets` throws for a non-zero `arraySlice`; CNA exposes no texture arrays and FNA3D's binding has no slice field. |
| Unknown vertex stride with no `VertexDeclaration` | Throws, naming the stride. FNA3D binds real per-stream declarations and this renderer will not guess a layout. |
| Out-of-contract state ordinals | Throw, naming the state and the ordinal, instead of casting into an undefined FNA3D enumerator. |
| Instanced drawing | `DrawInstancedPrimitivesEx` throws; `Instancing` is false. The limit is the stock effects' vertex inputs, which is the custom-shader limit again. |
| Multiple simultaneous devices on one thread | Not claimed. FNA3D's OpenGL driver does not make each device's context current around commands/teardown; the plan tracks the required contract decision or upstream fix as FNA3D-51. Sequential replacement devices are covered. |
| Context-loss simulation | Not implemented; FNA3D exposes no device-loss surface, and the shared `DebugSimulateContextLoss` default is a no-op. |
| Block-compressed readback on OpenGL / D3D11 | `GetData` returns false — "this renderer read nothing" — rather than reporting an untouched buffer as a successful read. Both drivers refuse compressed `GetTextureData2D` upstream; SDL_GPU forwards it. Which one applies is measured once per device by a 4×4 DXT1 probe, not guessed from the driver name. |
| A compressed `Texture3D` / `TextureCube` | Refused by name. Volume and cube transfers are RGBA8 in CNA's own renderer contract (the DDS/XNB readers decompress on the CPU before upload), so a compressed request there would be mis-sized rather than served. |

### What FNA3D offers that CNA cannot currently reach

Not gaps in this renderer — routes the shared `IGraphicsRenderer` contract has no shape for. Each
would need a shared-contract change, so each is reported rather than faked:

| FNA3D entry point | Why it is unreachable |
|---|---|
| `FNA3D_SetTextureDataYUV` | `IGraphicsRenderer` has no YUV texture route; `VideoDecoder` converts YUV→RGBA in the media module before any renderer sees a frame. |
| `FNA3D_GetVertexBufferData` / `FNA3D_GetIndexBufferData` | The buffer renderer interfaces expose no readback; XNA's `GetData` on those buffers is served from the shared layer's own CPU shadow. |
| `FNA3D_CloneEffect` | CNA holds one instance of each stock effect per device and has no public `Effect` clone route. |
| `FNA3D_VerifyVertexSampler` | The stock effects declare no vertex-shader sampler, and there is no vertex-texture binding in the contract. The driver's vertex slot count is still reported through `GetMaxVertexTextureSlotsEXT()`. |

A public block-compressed `Texture2D` is blocked one level above this renderer as well: the shared
`Texture::ValidateFormat` admits only `SurfaceFormat::Color` for every renderer except Skia. The
renderer contract has no such restriction — an `ImageData` naming a compressed format reaches
`CreateTexture` directly — and that is the layer `Fna3d_Compressed` measures.

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
| Native runtime (Linux, SDL offscreen video driver + Mesa llvmpipe, FNA3D OpenGL driver) | **Performed** — all thirteen `Fna3d_*` CTest binaries pass, including the sequential-device/post-device lifetime regression |
| XNA 4.0 oracle corpus (39 scenes vs real XNA reference images) | **Performed** — `Fna3d_XNA_Oracle`; every scene renders, 10/39 exact at the EasyGL baseline. This is the only coverage DualTextureEffect, EnvironmentMapEffect and SkinnedEffect have. See [`fna3d-parity-report.md`](fna3d-parity-report.md) |
| Unit tests (`CnaTests`, `Fna3d*` plus the generic disposed-`Texture2D` regression) | **Performed** — format-region and shared transfer-contract coverage pass |
| Sanitizers (ASan + UBSan) | **Performed** — the enlarged 13-test suite passes, including post-device lifetime and the >16-bit SpriteBatch path. Leak detection is disabled for the external graphics stack; UBSan still reports the pre-existing MojoShader decimal-parser signed overflow, but found no CNA-originating defect (FNA3D-47). |
| Existence-gate spikes | **Performed** — `fna3d-spike/` |
| SDL_GPU driver | **Not exercised here**: this container has no Vulkan ICD, so FNA3D declines SDL_GPU and falls through to OpenGL. The code path is driver-agnostic; the gate is external. This is also the only driver on which compressed readback is expected to succeed, so that arm of `Fna3d_Compressed` is unexercised here. |
| Direct3D 11 driver | **Not exercised here**: Windows-only (or DXVK-native). External gate. |
| Driver matrix (`plan_fna3d.md` FNA3D-34) | **Open.** This renderer is validated on FNA3D's **OpenGL driver**, not across the matrix. This lane has already found three driver-dependent behaviours (sub-rectangle readback origin, volume readback, compressed readback), so an OpenGL pass must not be read as validating SDL_GPU or Direct3D 11. |
| macOS / iOS | Not exercised. External gate. |

## Tests

| CTest name | What it proves |
|---|---|
| `Fna3d_Smoke` | Identity, device creation, clear + readback at three points, repeated frames, texture upload/readback through the renderer (not the CPU shadow), buffer counts, occlusion query |
| `Fna3d_2D` | SpriteBatch placement, non-bleed on all four sides, tint, source rectangles, flips, rotation, point-sampling texel edges, and the first sprite beyond one 16-bit-index batch |
| `Fna3d_3D` | BasicEffect vertex-colour/diffuse/textured variants, indexed route, depth test both orders, AlphaTestEffect discard and keep, wireframe |
| `Fna3d_RenderTarget` | Target clear/readback, geometry placement inside a target, unbind restores the backbuffer, sampling a rendered target, depth/stencil reporting, MSAA resolve, `PreserveContents` |
| `Fna3d_State` | AlphaBlend, Additive, colour write masks, BlendFactor, scissor on/off, a non-default depth comparison, stencil write-then-test, viewport sub-rectangle |
| `Fna3d_Capabilities` | Every capability answer matched against a real factory or a real refusal, and both rejection paths |
| `Fna3d_RenderTarget_Advanced` | All six cube faces hold distinct colours and geometry renders into a bound face; two MRT slots are both written and keep their own storage; a mipmapped target's level count, base level and level-1 storage |
| `Fna3d_Buffers` | POSITION and COLOR split across two real vertex streams (both binding orders), all three `SetDataOptions`, source-window selection, consecutive uploads, both index widths |
| `Fna3d_Sampler` | Point vs Linear magnification, Clamp/Wrap/Mirror past u=1, independent U/V addressing, Anisotropic, and the LOD controls |
| `Fna3d_Lifetime` | Invalid levels and regions, undersized destinations, disposed resources, draw-range validation, documented refusals, and resource-before-device teardown |
| `Fna3d_Device_Lifetime` | Resources, render targets, buffers, queries and SpriteBatch survive destruction of their device wrapper safely; post-device use and binds from a sequential replacement device are rejected; environment sampler 1 is exercised through live→null and foreign-device paths; buffer-overflow requests never reach FNA3D |
| `Fna3d_XNA_Oracle` | All 39 XNA oracle scenes render, and each is diffed against the real XNA 4.0 reference image |
| `Fna3d_Compressed` | Hand-built DXT5 blocks uploaded through `IGraphicsRenderer::CreateTexture` decode as the right colour in the right quadrant, including a 6×6 level whose tail blocks are padded; `GetData` matches the driver's real compressed-readback answer; a compressed cube and an out-of-range sampler slot are refused by name |

`CnaTests --gtest_filter=Fna3d*` covers the device-free logic: the `ShaderIndex` arithmetic against
an independently transcribed reference, the presentation layout and its inverse transform, the
enum bridge's range rejection, the stride table, and the per-format transfer byte arithmetic
(including compressed block alignment, partial-tail cases, and a row-pitch × block-rows =
region-count consistency sweep). `Texture2DTest.TransfersAfterDisposeThrowObjectDisposedException`
pins the shared upload/readback contract repaired alongside the renderer audit;
`SpriteBatchSortModeTest.DeferredRetainsTextureRendererThroughEnd` pins queued texture lifetime.
