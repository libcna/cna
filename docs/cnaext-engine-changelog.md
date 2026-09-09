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

## Revision 9 — 2026-09-09

### Device-gated Vulkan indirect drawing (`MOD-2245`)

- Vulkan now consumes both canonical indirect command layouts with `vkCmdDrawIndirect` and
  `vkCmdDrawIndexedIndirect`. `GraphicsCapability::IndirectDraw` and
  `RendererFeature::IndirectDrawing` report support only when the selected device exposes and CNA
  enables `drawIndirectFirstInstance`, so the promised layout includes working non-zero
  `BaseInstance` rather than a silent zero-only subset.
- Deferred draws retain the internal argument-buffer record through command recording and retire
  its native allocation on the consuming frame fence. Complete bounded geometry snapshots preserve
  vertex-binding, first-vertex, first-index, base-vertex and per-instance offsets without reading a
  GPU-produced command back to the CPU.
- Vulkan inserts the host/transfer/compute-write to indirect-command-read dependency automatically.
  The live oracle covers CPU- and compute-generated commands, disposal before render-target flush,
  and non-zero argument offsets and base instance on both RADV and llvmpipe with no validation
  messages.

The C header carries the same engine-layer revision marker. The C ABI remains 0.26.0 because no C
declaration, layout, identity or exported route changed; only the result of the existing device
capability query can now report Vulkan support.

## Revision 8 — 2026-09-09

### Capability-gated base-instance drawing (`MOD-2232`)

- `GraphicsDevice::DrawInstancedPrimitivesBaseInstanceEXT` reuses the complete XNA indexed,
  vertex-stream and effect contract while adding one non-negative `firstInstance` operand.
- `RendererFeature::BaseInstanceDrawing` and its false-by-default renderer probe prevent an older
  backend from accepting the new route accidentally. The C ABI appends the same detailed feature
  identity without changing any legacy graphics-capability value.
- Vulkan retains the first instance in its deferred draw record, validates every per-instance
  stream against the shifted range and passes the value to `vkCmdDrawIndexed`. Its native pixel
  oracle proves that `firstInstance=1` selects instance one while leaving instance zero untouched.

The C header carries the same engine-layer revision marker.

## Revision 7 — 2026-09-09

### Immutable storage-buffer roles, ranges and tracking (`MOD-2229`)

- `StorageBufferDescriptor` separates six immutable GPU roles from direct CPU read/write intent.
  The original size-only constructor remains source-compatible and declares the storage, two-way
  transfer, indirect and CPU read/write behavior available through the legacy public contract.
- `StorageBuffer` is now a non-copyable, non-movable tracked `GraphicsResource`. Exact range
  upload/readback and GPU-side copy reject pointer, access, usage, device, overlap and overflow
  errors before renderer work; a CPU-none buffer cannot be silently mapped.
- The renderer contract has a separate false-by-default descriptor factory. Vulkan maps every
  declared role to the exact `VkBufferUsageFlags`, leaves CPU-none allocations unmapped and proves
  the upload-staging → GPU-only compute/copy → readback-staging path byte-exactly. EasyGL preserves
  the legacy constructor's new range/copy surface.

The C header carries the same revision marker. Its existing storage-buffer routes retain the
compatible constructor behavior; descriptor-specific C ABI additions remain separate binding work.

## Revision 6 — 2026-09-09

### Storage-texture compute and sampled binding (`MOD-2228`)

- `ComputeShader::bindStorageTexture` binds the tracked `StorageTexture2D` record with explicit
  read-only, write-only or read-write intent. It validates disposal, device ownership and immutable
  usage before the backend validates the SPIR-V slot, image format and access qualifier.
- `ShaderEffect::SetStorageTextureEXT` and `ClearStorageTextureEXT` expose the later sampled-read
  half without leaking a native view or image layout. The binding retains only renderer-owned work,
  so disposal of the public wrapper cannot invalidate an accepted deferred operation.
- Renderer interfaces refuse both bindings by default. Vulkan implements format-qualified
  `rgba8` storage images, descriptor reflection, upload/readback and compute-to-sampling visibility;
  optional extended storage formats and legacy XNA texture/render-target bridges remain bounded by
  `MOD-2244`.

The C header's mirrored revision marker is also synchronized from its stale value 2 to 6; the
existing compile-time assertion and pure-C runtime check keep the two public version reports equal.

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

Revision 5 published the portable allocation/transfer/lifetime contract. Revision 6 adds compute
and sampled binding; broader Vulkan format support and legal XNA-resource bridges remain
`MOD-2244`.

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
