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

## Revision 18 — 2026-09-10

### Typed constant buffers (`MOD-2230`)

- `StorageBufferUsage::Constant` adds an immutable uniform-buffer role to the existing shared
  buffer resource. `ConstantBufferT<T>` accepts only trivially-copyable standard-layout values,
  rounds allocations to the live backend alignment, checks the published byte-range ceiling and
  zeroes padding on every upload.
- `ComputeShader::bindConstantBuffer` binds either that typed wrapper or a role-compatible shared
  `StorageBuffer`. `ShaderBindingTypeEXT::ConstantBuffer` is append-only identity 6; package
  selection currently accepts its portable binding route only for compute stages.
- EasyGL uses uniform-buffer binding points and reports the live GL limits. Vulkan reflects SPIR-V
  uniform `Block` declarations, uses `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER`, tracks read-only resource
  access and retains buffers through deferred dispatch completion.

The C header carries the same engine-layer revision marker. The C ABI remains 0.26.0; typed
constant-buffer construction and binding are currently a C++ surface.

## Revision 17 — 2026-09-10

### Structured shader diagnostics (`MOD-2215`)

- `ShaderDiagnosticEXT` owns a severity, exact or unknown shader stage, optional source label,
  available one-based line/column and non-empty renderer text. Its parser normalizes common GLSL
  compiler locations without inventing positions absent from a SPIR-V or package-selection error.
- `ShaderEffect::GetShaderDiagnosticsEXT()` adds structured inspection without changing the
  existing non-throwing effect construction or removing `GetCompileErrorEXT()`. Explicit portable
  code retains its source labels, including through `Clone`.
- Compute compilation and unavailable package selection throw
  `ShaderCompilationExceptionEXT`. `what()` provides a stable first-error/count summary while
  `getDiagnostics()` preserves every owned record for programmatic handling.
- The engine's pass-level shader logger now formats the structured records, including labels and
  locations, rather than flattening the renderer log before inspection.

The C header carries the same engine-layer revision marker. The C ABI remains 0.26.0; structured
shader diagnostics are currently a C++ surface.

## Revision 16 — 2026-09-10

### Existing shader objects accept portable payloads (`MOD-2214`)

- `ComputeShader` accepts either one compute `ShaderCodeEXT` or a compute-only
  `ShaderPackageEXT`. `ShaderEffect` accepts a same-language vertex/fragment code pair or an
  exactly vertex+fragment package. No parallel shader object hierarchy was introduced.
- Package constructors select exactly once against the live device. Compute programs retain the
  complete chosen code descriptor; effects retain the exact selected text/binary bytes in their
  existing source storage, and both objects expose the selected language for inspection. Legacy
  string constructors continue to report `Unknown` because their language remains implicit.
- The bridge delegates to each renderer's existing string/byte program factory. Those factories
  currently expose only entry point `main`, so another declared entry point is refused explicitly
  instead of being silently ignored. Package refusal carries the complete selection diagnostic.
- The `ShaderEffect` overload implementation lives in graphics-ext, preserving the one-way module
  dependency: graphics-ext may know graphics-core and portable packages, while graphics-core does
  not link back to graphics-ext.

The C header carries the same engine-layer revision marker. The C ABI remains 0.26.0 and keeps its
existing source-string shader entry points.

## Revision 15 — 2026-09-10

### Deterministic live-device shader selection (`MOD-2213`)

- `ShaderPackageEXT::selectFor` asks the live `GraphicsDevice` for every exact language/stage pair;
  it never derives support from a renderer identity or converts one payload form into another.
- Selection prefers SPIR-V, then DXIL, then the textual language identities in their published
  order. Package declaration order cannot change the winner. One exact code value is required for
  every package stage; duplicate language/stage payloads reject that candidate as ambiguous.
- `ShaderPackageSelectionEXT` owns the chosen stage payloads and a deterministic diagnostic.
  Failure lists every language actually present, every source label and every reason considered,
  including incomplete stages, live renderer refusal and unavailable stage/resource capabilities.

The C header carries the same engine-layer revision marker. The C ABI remains 0.26.0; package
selection is currently a C++ engine-layer surface.

## Revision 14 — 2026-09-10

### Owned multi-language shader packages (`MOD-2212`)

- `ShaderPackageEXT` owns one or more `ShaderCodeEXT` variants, an explicit non-empty set of
  required stages and renderer-neutral logical resource-binding requirements. It contains no
  device, renderer or native program and retains declaration order across copies and moves.
- Binding requirements name a non-negative logical slot, one of six currently bindable portable
  resource kinds and the stage that consumes it. Per-stage duplicates and inconsistent shared
  slots are rejected during package construction.
- Every required stage has at least one code variant and every code/binding stage is declared.
  Incomplete and duplicate per-language candidates remain representable so `MOD-2213` can reject
  them with a complete deterministic considered-variant diagnostic.

The C header carries the same engine-layer revision marker. The C ABI remains 0.26.0; no package
handle has been published at the C boundary.

## Revision 13 — 2026-09-10

### Owned explicit shader code (`MOD-2211`)

- `ShaderCodeEXT` owns one exact language, stage, entry point, diagnostic source label and either
  source text or binary bytes. Copying or moving the value never retains caller storage.
- Text construction accepts only the three GLSL dialects, HLSL, MSL and WGSL; binary construction
  accepts only SPIR-V and DXIL. Unknown, sentinel and future identity values are refused rather
  than classified from their bytes.
- Empty entry points/payloads and SPIR-V sizes that are not complete 32-bit words fail during value
  construction, before a renderer or native compiler can see the payload.

The C header carries the same engine-layer revision marker. The C ABI remains 0.26.0; no shader
payload handle has been published at the C boundary.

## Revision 12 — 2026-09-10

### Explicit shader language and stage support (`MOD-2210`)

- `ShaderLanguageEXT` publishes append-only identities for desktop GLSL, GLSL ES, Vulkan GLSL,
  HLSL, MSL, WGSL, SPIR-V and DXIL. The three GLSL dialects remain distinct so selection cannot
  send source to an incompatible compiler; `ShaderStageEXT` independently identifies vertex,
  fragment and compute payloads.
- `GraphicsDevice::SupportsShaderLanguageEXT` asks the live renderer whether its implemented path
  consumes one exact language/stage pair. Unknown, invalid and newly appended values are refused by
  the shared renderer default rather than inferred from a renderer name.
- EasyGL declares GLSL graphics stages and runtime-gated GLSL compute, and now reports its exact
  desktop-versus-ES legacy dialect. Vulkan declares SPIR-V graphics stages and device-gated SPIR-V
  compute while explicitly refusing GLSL source.

The C header carries the same engine-layer revision marker. The C ABI remains 0.26.0; portable
shader-package C routes have not been published and remain within the binding plan's boundary.

## Revision 11 — 2026-09-10

### Complete Vulkan storage-image bridges (`MOD-2244`)

- Vulkan now derives storage-image support from one exact CNA → Vulkan → SPIR-V format table.
  Fifteen uncompressed formats are eligible; extended SPIR-V formats additionally require CNA to
  enable `shaderStorageImageExtendedFormats`, and every allocation still intersects its complete
  usage with device image-format properties. Unsupported channel semantics, compression, sRGB and
  packed formats are refused rather than substituted.
- An ordinary Vulkan `Texture2D` conditionally receives a mip-zero storage view when the immutable
  sampled/transfer allocation legally supports storage. `ComputeShader::bindImage` validates the
  reflected format/access contract and keeps later uploads, compute and ordinary sampling in one
  deferred order. Eligible `RenderTarget2D` formats reuse their attachment image the same way.
- Render-target readback dependency closure now traverses compute-image inputs as well as sampled
  graphics inputs. The permanent oracle proves `A` render → compute copy to `B` → direct `B`
  readback without an intervening `A.GetData()` or `Present()` on RADV and llvpipe.

The C header carries the same engine-layer revision marker. The C ABI remains 0.26.0: no C
declaration or layout changed, but existing capability/format queries can now report the completed
device-qualified Vulkan paths.

## Revision 10 — 2026-09-10

### Vulkan GPU timing and structured debug integration (`MOD-2246`)

- `GpuTimer` now uses native Vulkan timestamp queries whenever the selected graphics queue exposes
  timestamp bits and the device publishes a positive `timestampPeriod`. Results remain
  asynchronous: `end()` records no wait, `poll()` asks for availability, and ordinary timing adds
  no `vkDeviceWaitIdle` or `vkQueueWaitIdle`.
- Each Vulkan timer owns one two-slot query pool and recycles it across samples. Query reset and
  writes are recorded in the same ordered command buffer as the clear/SpriteBatch/3D work being
  measured, while destruction retires a submitted pool on the consuming frame fence.
- `VK_EXT_debug_utils`, when present, labels recorded render-pass regions and string markers even
  without validation enabled. Validation/debug messages enter `CNA::Logger` exactly once with GPU
  category and severity; the renderer's captured-message diagnostics remain available without a
  second stderr copy.

The C header carries the same engine-layer revision marker. The C ABI remains 0.26.0: the existing
timer and capability routes changed only from an honest Vulkan refusal/zero to device-derived
support.

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
  optional extended storage formats and legacy XNA texture/render-target bridges were subsequently
  completed by `MOD-2244` in revision 11.

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
and sampled binding; revision 11 completes broader Vulkan format support and legal XNA-resource
bridges through `MOD-2244`.

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
