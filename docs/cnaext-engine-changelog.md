# `CNA::Graphics` engine-layer revisions

`plans/plan_modern.md` **MOD-1904**. `CNA_CNAEXT_ENGINE_VERSION` only earns its place if a reader can find
out what a number means, so this file says what each revision changed.

The number is a **revision marker, not an ABI promise** — see `CNAEXT.md` §9 and `MOD-1905`. It
exists so a build can notice that the header it compiled against and the library it linked to
disagree (`CNA_CNAEXT_ENGINE_VERSION` versus `CNA::Graphics::getEngineLayerVersion()`), and so a
bug report can say which shape of the layer it is about.

**Bump it whenever a consumer could notice the change.** That includes additions: a consumer that
feature-tests on the revision needs the number to move when the feature arrives, not only when
something breaks.

---

## Revision 5 — 2026-09-09

### Dedicated storage-texture resource (`MOD-2227`)

- `StorageTexture2DUsage` declares storage read/write, sampling/filtering and transfer intent.
  Every valid description has at least one storage access; filtering cannot be requested without
  sampling.
- `StorageTexture2DDescriptor` validates dimensions, complete mip chains, formats and usage bits
  without a device. `StorageTexture2D` then requires known live texture/storage-image limits and
  every combined per-format usage before renderer allocation or resource registration.
- Exact mip/rectangle upload and readback share the texture-array rules for native-format byte
  counts, overflow and compressed-block alignment. The tracked public resource owns only a shared
  renderer-neutral record; native images, layouts and barriers remain hidden.

Revision 5 publishes the portable allocation/transfer/lifetime contract. The default factory and
transfers refuse; compute binding is `MOD-2228` and Vulkan allocation is `MOD-2244`.

## Revision 4 — 2026-09-09

### Texture-array transfers and sampled binding (`MOD-2226`)

- `Texture2DArray::setData` and `getData` address one layer, mip and optional rectangle using
  tightly packed native-format bytes. The public layer validates subresource bounds, exact byte
  counts, overflow and compressed-block alignment before the renderer sees the request.
- `ShaderEffect::SetTextureArrayEXT` binds a live array record and
  `ClearTextureArrayEXT` releases it. Renderer work is shared independently of the disposed public
  `GraphicsResource`, and an unimplemented backend refuses instead of discarding the bind.
- Vulkan supplies the first native implementation and oracle. Revision 4 defines the portable
  public contract; `MOD-2243` separately closed the Vulkan-specific native allocation, view,
  retirement and device-fact acceptance criteria on RADV and llvmpipe.

## Revision 3 — 2026-09-09

### New sampled texture-array resource (`MOD-2225`)

- `Texture2DArrayUsage` declares sampled/filterable and transfer intent without exposing a native
  image-layout or barrier vocabulary.
- `Texture2DArrayDescriptor` is immutable and rejects invalid dimensions, mip counts, formats and
  usage masks before a device is needed.
- `Texture2DArray` is a non-copyable, non-movable `GraphicsResource`. Construction validates the
  live device's known limits and exact per-format usage support before renderer allocation or
  resource registration; disposal participates in normal `GraphicsDevice` tracking.

This revision publishes the renderer-neutral resource and deterministic refusal contract only.
Layer/mip transfer and sampled binding arrive in `MOD-2226`; Vulkan native allocation, views and
retirement arrive in `MOD-2243`. A renderer that has not implemented that complete path keeps the
factory null and the maximum-array-layer limit at zero.

## Revision 2 — 2026-08-18

### Incompatible (`MOD-1900`)

Six names moved to `MOD-6`'s lowerCamelCase rule. Only the first four are reachable from outside the
module; the last two are private.

| Was | Is |
|---|---|
| `CNA::Graphics::detail::RequireCapability` | `detail::requireCapability` |
| `CNA::Graphics::detail::NameOfCapability` | `detail::nameOfCapability` |
| `CNA::Graphics::detail::ReportShaderCompileFailure` | `detail::reportShaderCompileFailure` |
| `EngineException::NotSupported` | `EngineException::notSupported` |
| `DepthEffect::EnsurePaletteTextures` (private) | `ensurePaletteTextures` |
| `RenderPipeline::DrawSkybox` (private) | `drawSkybox` |

Nothing else was renamed, and nothing was removed. Fourteen XNA/.NET-shaped names are deliberately
**unchanged** and are listed with their reasons in `scripts/check_cnaext_naming.py` — the `Effect`
and `Texture2DArray` overrides, `PbrMaterial::GetHashCode`/`ToString`, and
`AsciiPostProcessEffect::Draw` /
`GetLastGridDimensions`, which are part of the C ABI surface.

### New types

`EffectPass`, `EngineException`, `RequireCapability` (`detail`), `ScopedRenderTarget`,
`ShaderDiagnostics` (`detail`), `ShaderEffectFactory`, `DepthNormalPrepass`, `AsciiPass`.

### New members on existing types

- `RenderPipeline`: `getStatistics()` and its `FrameStatistics`, `releaseDeviceResourcesEXT()`,
  `getDepthTexture()`/`getNormalTexture()`.
- `RenderPipelineSettings`: `applyRenderQualityPresetEXT()`, `toStringEXT()`/`applyFromStringEXT()`,
  `getFXAAEdgeThresholdEXT()`/`setFXAAEdgeThresholdEXT()`, and the clamping described in `MOD-730`.
- `BloomPass::iterationsForQuality`, `SsaoPass::sampleCountForQuality` /
  `setHalfResolution`/`isHalfResolution`, `FxaaPass::edgeThresholdForQuality` — the quality presets,
  exposed so a caller can ask what a preset will do rather than infer it.
- `PostProcessChain`: `getTargetPool()` (non-const), `contains()`, `clear()`.
- `Skybox`, `ShadowMap`, `CascadedShadowMap`, `SpotShadowMap`, `CubeShadowMap`: `isSupported()` now
  asks the **two-part** question (`MOD-1699`) — the capability *and*
  `ExecutesShaderEffectSourceEXT()`. A renderer that accepts a shader without running it now
  reports `false` instead of rendering a wrong frame. This is a behaviour change, not just an
  addition, and it is the one item here most likely to change what a caller sees.

### Behaviour changes worth knowing

- `RenderPipeline::end()` unbinds the render target before running the post-process chain.
- Both depth encoders clamp to `0.99999994` so a far-plane value does not pack to zeroes
  (`fract(1.0) == 0`).
- `ShadowMap::begin` and `CubeShadowMap::begin` no longer mark the pass open before the bind that
  can refuse, so one refused pass no longer bricks the object.

---

## Revision 1 — 2026-08-18 (`MOD-8`)

The first published shape: `RenderPipeline` and `RenderPipelineSettings`, the post-process chain
(`PostProcessPass`, `PostProcessChain`, `BlitPass`, `BloomPass`, `TonemapPass`, `FxaaPass`,
`SsaoPass`, `FullscreenPass`, `RenderTargetPool`), the shadow casters, `Skybox`,
`EnvironmentProcessor`, the PBR material surface, instancing/LOD/culling, and the compute and
storage-buffer wrappers.
