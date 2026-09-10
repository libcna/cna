# Modern GPU implementation baseline

This document is the implementation re-baseline required by
`plans/plan_modern.md` `MOD-2201`. It records what CNA actually shipped immediately before the
Phase 22 resource/API work started, so later tasks extend the existing seams instead of creating
parallel types or believing native API availability is an implemented CNA feature.

## Baseline identity and method

- Implementation commit examined: `7cad75e9d23a50346347ee3311e4edc172600ebf`.
- Measurement date: 2026-09-09.
- Host stack: Linux, Mesa `25.0.7-2+deb13u1`, Xvfb. Vulkan used llvmpipe
  (`LLVM 19.1.7, 256 bits`); EasyGL created an OpenGL ES 3.2 context and OpenGL4 created a 4.5 core
  context.
- Selected compile-time renderer identities: `OPENGLES3` (the EasyGL renderer family), `VULKAN`
  and `OPENGL4`. These are three separate single-renderer debug configurations, not runtime names
  inferred from a multi-renderer build.
- The checked-in `cna_probe_modern_gpu_capabilities` target constructs a real standalone
  `GraphicsDevice`, prints all 19 append-only `GraphicsCapability` ordinals and then prints the
  public schema-2 `RendererCapabilityProfile`. It is registered only for these three Phase 22
  identities.

Reproduce a configured build's measurement with:

```bash
xvfb-run -a env SDL_VIDEODRIVER=x11 ./<build-dir>/cna_probe_modern_gpu_capabilities
```

For the software Vulkan device, also set
`VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json`. The probe links the narrow
`cna_graphics_core` graph instead of the monolithic `CNA` umbrella; capability measurement does not
need content, audio, media, storage or the engine extension module.

## Shipped public surface relevant to Phase 22

| Public type or entry point | Owner | Baseline meaning |
|---|---|---|
| `CNA::GraphicsCapability` | `modules/graphics` | Legacy 19-value summary. Ordinals 0 through 18 are already public and append-only. |
| `CNA::RendererFeature`, `RendererFeatureSupport`, `RendererFeatureInfo` | `modules/graphics` | 32 atomic feature identities and four-state answers after `MOD-2232`. `Count` is a sentinel, not a query. |
| `CNA::RendererLimit`, `RendererLimitValue` | `modules/graphics` | 22 numeric limit identities with an explicit known bit. Zero may be a known unsupported/unimplemented value. |
| `CNA::RendererFormatUsage`, `RendererFormatSupport` | `modules/graphics` | Thirteen independent per-`SurfaceFormat` usage bits with separate known and supported masks. |
| `CNA::RendererCapabilityProfile` | `modules/graphics` | Cached immutable device snapshot plus a generated English report. |
| `GraphicsDevice` capability/profile/format/limit queries | `modules/graphics` | The public device seam; callers must not infer facts from renderer names. |
| `CNA::Internal::Renderers::ShaderDialectEXT` and `GraphicsDevice::GetShaderDialectEXT` | `modules/graphics` | Existing renderer payload hint (`Unknown`, GLSL families, HLSL, MSL, WGSL, SPIR-V). It has no shader stage, owning bytes or variant-selection contract. |
| `Microsoft::Xna::Framework::Graphics::ShaderEffect` | `modules/graphics` | Existing CNAEXT vertex/fragment payload object. Its two string payloads remain renderer-specific. |
| `CNA::Graphics::ComputeShader` | `modules/graphics-ext` | Existing CNAEXT compute object with renderer-specific string source, scalar setters, buffer/texture/image binding, dispatch and a caller-visible barrier method. |
| `CNA::Graphics::StorageBufferDescriptor`, `StorageBuffer`, `StorageBufferT<T>` | `modules/graphics-ext` | Role-gated tracked byte buffer and typed wrapper with immutable GPU roles, direct CPU-access intent, exact range transfer and GPU-side copy. `Storage` requires compute; `IndirectArguments` requires only indirect drawing. The size-only constructor preserves its compute-gated compatible legacy contract. |
| `CNA::Graphics::GpuTimer` | `modules/graphics-ext` | Existing nonblocking timer wrapper; EasyGL implemented it at this baseline and Vulkan implements the same contract since `MOD-2246`. OpenGL4 remains unsupported. |
| `CNA::GraphicsImageAccess` | `modules/graphics` | Three image-access values used by `ComputeShader::bindImage(Texture2D&)`. |
| `CNA::GraphicsMemoryBarrier` and its bit operators | `modules/graphics` | Portable-looking caller barrier mask. Phase 22 replaces the need for callers to drive normal correctness with renderer-owned usage transitions. |
| `CNA::IndirectDrawArguments`, `IndirectDrawIndexedArguments` | `modules/graphics` | Canonical 16-byte/20-byte GPU argument layouts; both already contain base-instance fields. |
| `GraphicsDevice::DrawPrimitivesIndirectEXT`, `DrawIndexedPrimitivesIndirectEXT` | `modules/graphics` | Existing indirect public routes backed by a role-declared `StorageBuffer`; EasyGL and Vulkan implement them, while OpenGL4 still refuses. |
| `GraphicsDevice::DrawInstancedPrimitivesBaseInstanceEXT` | `modules/graphics` | `MOD-2232` adds a capability-gated first logical instance while retaining XNA indexed geometry, bindings and effect state; Vulkan is the first implementation. |
| `CNA::DisplayColorSpace` and device queries | `modules/graphics` | Existing sRGB/scRGB/HDR10 vocabulary; all measured renderers still expose only sRGB presentation. |

There is no constant buffer, fence or native image/buffer/view type. `ShaderCodeEXT` and
`ShaderPackageEXT` now provide explicit portable payloads, while an independently creatable
indirect-argument buffer is represented by the existing `StorageBuffer` with the immutable
`IndirectArguments` role rather than by a duplicate resource type. The texture-array,
storage-texture and storage-buffer descriptors remain portable value types.

## Phase-22-relevant renderer virtuals and their shared defaults

The following inventory covers every existing `IGraphicsRenderer` seam that a Phase 22 task can
reuse or must deliberately supersede. A default is not evidence of backend support.

| Virtual or group | Shared default |
|---|---|
| `GetShaderDialectEXT` | `Unknown`. |
| `ClassifySurfaceFormatEXT`, `ClassifyRenderTargetFormatEXT`, `ClassifyColorTransferFormatEXT` | `Defer` to the framework's older validation. |
| `IsCompressedTransferFormatEXT`, `IsCompressedCubeTransferFormatEXT`, `LoadsCompressedContentNativelyEXT` | `false`. |
| `GetSurfaceFormatUsageSupportEXT` | Both masks zero: every detailed usage is unknown. |
| `CreateRenderTarget2DEXT`, `CreateRenderTargetCubeEXT` | Forward to the Color-era factory and discard the explicit format. |
| `SupportsHalfFloatTextureLinearFilteringEXT` | `false`. |
| `CreateEffectRenderer` | `nullptr`. |
| `CreateComputeShader`, `CreateStorageBuffer`, `CreateStorageBufferEXT` | `nullptr`; the descriptor factory is separately false by default. |
| `DispatchCompute`, `MemoryBarrierEXT` | No-op. Public wrappers must gate support before reaching these defaults. |
| `ExecutesShaderEffectSourceEXT`, `SupportsShadowSamplingEXT`, `SupportsImageBasedLightingEXT` | `false`. |
| `SupportsComputeShadersEXT`, `SupportsIndirectDrawEXT`, `SupportsBaseInstanceDrawingEXT`, `SupportsComputeImageBindingEXT`, `SupportsTexture3DSamplingEXT` | `false`. |
| `GetDisplayColorSpaceEXT`, `SetDisplayColorSpaceEXT` | Reports sRGB; accepts only sRGB. |
| `GetMaxVertexShaderStorageBlocksEXT` | `0`. |
| `BindStorageBufferForDrawEXT` | No-op. |
| `SupportsGpuTimerEXT`, `CreateGpuTimerEXT` | `false`, `nullptr`. |
| `GetMaxComputeWorkGroupCountEXT`, `GetMaxComputeWorkGroupSizeEXT`, `GetMaxComputeWorkGroupInvocationsEXT` | `0`. |
| `GetMaxStorageBufferBytesEXT`, `GetMaxUniformBufferBytesEXT`, `GetMaxComputeStorageBufferBindingsEXT` | `0`. |
| `GetMaxTextureArrayLayersEXT`, `GetMaxSampledTexturesPerShaderStageEXT`, `GetMaxStorageImagesPerShaderStageEXT` | `0`. |
| `GetMaxVertexInputBindingsEXT`, `GetMaxVertexInputAttributesEXT`, `GetMaxColorAttachmentsEXT` | `0`. |
| `GetMinStorageBufferOffsetAlignmentEXT`, `GetMinUniformBufferOffsetAlignmentEXT`, `GetTimestampPeriodPicosecondsEXT` | `0`. |
| `DrawPrimitivesIndirectEXT`, `DrawIndexedPrimitivesIndirectEXT` | Throw unsupported. |
| `SupportsCapability` | Historically true for most unrecognized legacy values; selected derived features are separately false-by-default. New work must not add promises through this catch-all. |
| `GetAdditionalLimitationsTextEXT` | Empty. |
| `GetMaxVertexStreams`, `GetMaxTextureDimension` | `16`, `16384`; these are framework defaults, not native discovery. |

Phase 22 must add new creation/binding/transfer seams with false/null/zero/throwing defaults. It
must not reinterpret the existing no-op defaults as successfully completed work.

## Measured legacy capability matrix

`S` means the public device query returned supported and `U` means unsupported. This is what CNA
implemented on the measured devices, not what GLES, Vulkan or desktop OpenGL could theoretically
do.

| Ordinal / `GraphicsCapability` | EasyGL `OPENGLES3` | Vulkan | OpenGL4 |
|---|---:|---:|---:|
| 0 `ThreeD` | S | S | S |
| 1 `DepthStencilBuffer` | S | S | S |
| 2 `MultiSampleAntiAliasing` | S | S | S |
| 3 `MultipleRenderTargets` | S | S | S |
| 4 `AnisotropicFiltering` | S | S | S |
| 5 `WireFrame` | S | S | S |
| 6 `OcclusionQuery` | S | S | S |
| 7 `CustomEffects` | S | S | S |
| 8 `Texture3D` | S | S | S |
| 9 `MultiStreamVertexInput` | S | S | U |
| 10 `Instancing` | S | S | S |
| 11 `StencilBuffer` | S | S | S |
| 12 `AdditiveBlending` | S | S | S |
| 13 `CompiledEffects` | U | U | U |
| 14 `FloatRenderTargets` | S | S | U |
| 15 `HalfFloatRenderTargets` | S | S | U |
| 16 `HalfFloatTextureLinearFiltering` | S | S | U |
| 17 `ComputeShaders` | S | S | U |
| 18 `IndirectDraw` | S | S | U |

The `CustomEffects` row does not mean source parity. Vulkan accepts `ShaderEffect` objects through
its SPIR-V payload route. OpenGL4's existing GLSL 4.10 compiler/execution path was made truthful in
the detailed query by `MOD-2260`; it consumes desktop GLSL vertex and fragment payloads.

## Measured detailed modern subset

| Observable profile fact | EasyGL `OPENGLES3` | Vulkan | OpenGL4 |
|---|---:|---:|---:|
| Shader-effect source execution | S | U | S |
| Compute shaders + storage-buffer dispatch | S | S | U |
| Compute constant-buffer binding | S | S | U |
| Compute image binding | U | S | U |
| Compute sampling of `Texture2D` / `RenderTarget2D` | S | S | U |
| Indirect drawing | S | S | U |
| GPU timers | S | S | U |
| Shadow sampling | S | S | U |
| Image-based lighting | S | S | U |
| Texture3D sampling | S | S | U |
| Payload dialect query | `GlslEs` | `SpirV` | `GlslDesktop` |

`MOD-2210` added an explicit append-only language/stage query. `MOD-2260` completed OpenGL4's
reporting for its already-implemented source path while deliberately leaving compute unsupported
until the compute implementation itself exists.

OpenGL4 also retains an internal `MOD-2260` native-feature snapshot distinct from these public CNA
promises. The required loader and context request remain OpenGL 4.1; optional function groups are
resolved non-fatally. On Mesa 25.0.7 the probe passed both the native 4.6 context and a forced 4.1
context, where compute, SSBO, image, base-instance and full format-query facts were admitted only
through the separately advertised ARB extensions. Float-format queries independently reported
RGBA16F and RGBA32F framebuffer renderability. Public modern features and their numeric limits stay
unsupported/zero until `MOD-2261` supplies the matching operations.

### Numeric limits

All values below are the profile's measured values. Zero is deliberately retained where the CNA
path is unavailable or not yet classified; a large native device value is not published early.

| `RendererLimit` | EasyGL | Vulkan | OpenGL4 |
|---|---:|---:|---:|
| `MaxTextureDimension` | 16384 | 16384 | 16384 |
| `MaxVertexStreams` | 16 | 16 | 1 |
| `MaxComputeWorkGroupCountX/Y/Z` | 2147483646 / 65535 / 65535 | 65535 / 65535 / 65535 | 0 / 0 / 0 |
| `MaxComputeWorkGroupSizeX/Y/Z` | 1024 / 1024 / 1024 | 1024 / 1024 / 1024 | 0 / 0 / 0 |
| `MaxComputeWorkGroupInvocations` | 1024 | 1024 | 0 |
| `MaxVertexShaderStorageBlocks` | 16 | 1000000 | 0 |
| `MaxStorageBufferBytes` | 4035026944 | 134217728 | 0 |
| `MaxUniformBufferBytes` | 4035026944 | 65536 | 0 |
| `MaxComputeStorageBufferBindings` | 0 | 1000000 | 0 |
| `MaxTextureArrayLayers` | 0 | 2048 | 0 |
| `MaxSampledTexturesPerShaderStage` | 0 | 15 | 0 |
| `MaxStorageImagesPerShaderStage` | 0 | 1000000 | 0 |
| `MaxVertexInputBindings` | 0 | 16 | 0 |
| `MaxVertexInputAttributes` | 0 | 32 | 0 |
| `MaxColorAttachments` | 0 | 4 | 0 |
| `MinStorageBufferOffsetAlignment` | 0 | 16 | 0 |
| `MinUniformBufferOffsetAlignment` | 4 | 16 | 0 |
| `TimestampPeriodPicoseconds` | 0 | 1000 | 0 |

The Vulkan values are the llvmpipe reference snapshot; array layers, storage-image descriptors and
timestamp period became publishable with their matching CNA paths. RADV reports 10019 ps rather
than llvmpipe's 1000 ps, which is why the value remains device-origin. The EasyGL compute and
buffer-range/alignment values are the live Mesa OpenGL ES 3.2 values measured on the same host;
they are device/context facts, not portable constants. EasyGL still has several working older paths whose other newly appended
detailed limit fields are zero; those are classification gaps, not permission to assume unlimited
values.

### Format support

- Vulkan classifies all thirteen usage bits for all 27 `SurfaceFormat` values. Its public
  `RenderTarget2D` construction supports exactly nine measured formats (`Color`, `Rgba64`, the
  three 32-bit float and four half/HDR values); standalone texture storage remains narrower.
- EasyGL and OpenGL4 still populate only the older texture-storage, render-target and color-transfer
  facts. EasyGL reports nine texture-storage formats and eight render-target formats. OpenGL4
  reports only `Color` for texture storage and render targets.
- Vulkan reports storage-image read/write support for fifteen exact format-qualified paths. The
  five baseline SPIR-V formats need no optional feature; the other ten require the enabled
  `shaderStorageImageExtendedFormats` guarantee as well as the per-format/complete-usage query.
  Storage atomics remain unsupported. Vulkan also publishes its implemented sampled texture-array
  layer limit. EasyGL and OpenGL4 still report no storage-image or texture-array path; their
  portability work belongs to `MOD-2261`.

## XNA/modern resource interoperability (`MOD-2233`)

The bridge is typed and opt-in. CNA never exposes a native image layout, memory barrier, image
alias, descriptor or buffer handle, and never treats matching byte sizes as permission to
reinterpret one resource class as another. A legal bind validates the live resource, owning
`GraphicsDevice`, immutable usage and (where relevant) exact format before renderer work. An
accepted deferred operation retains the renderer-owned record rather than the public wrapper, so
later rebinding or disposal cannot invalidate work already issued. Vulkan's immutable compute
descriptor cache records only native binding identities. A pending dispatch owns the referenced
records through command recording; when a buffer or image/view then dies, CNA evicts and
fence-retires every matching descriptor snapshot with that native resource. The cache therefore
cannot keep an otherwise-unused resource alive until program destruction, nor can a recycled raw
handle select a stale descriptor set.

| Public resource | Graphics sampling/drawing | Compute use | Copy/alias contract |
|---|---|---|---|
| XNA `Texture2D` | Existing SpriteBatch, stock-effect and `ShaderEffect` sampling. | `ComputeShader::bindTexture` samples it on EasyGL and Vulkan. `bindImage` is a separate storage-image bridge and works only when `isImageBindingSupported()` and that exact format/allocation permit the requested access. | XNA CPU `SetData`/`GetData` only; no GPU copy or buffer alias. A full ordinary-texture `SetData` may replace its shared renderer record for content-cache isolation, so perform it before binding or bind again afterward. |
| XNA `RenderTarget2D` | Existing render-target output and texture sampling. | `bindTexture` samples it on EasyGL and Vulkan. Vulkan also accepts exact storage-capable targets through `bindImage`; queued attachment producers are found transitively before compute. | No public image alias or GPU-copy API. The existing render-target readback remains format-aware. |
| XNA `VertexBuffer` / `DynamicVertexBuffer` | XNA vertex input only. | No compute binding overload. | No alias or GPU copy to/from `StorageBuffer`. |
| XNA `IndexBuffer` / `DynamicIndexBuffer` | XNA indexed drawing only. | No compute binding overload. | No alias or GPU copy to/from `StorageBuffer`. |
| `Texture2DArray` | `ShaderEffect::SetTextureArrayEXT` when its immutable descriptor declares sampling. | No compute binding overload in the current portable contract. | Exact declared CPU layer/mip/rectangle transfers only; no alias or GPU copy. |
| `StorageTexture2D` | `ShaderEffect::SetStorageTextureEXT` only when `Sampled` was declared. | `bindStorageTexture` uses the declared read/write subset and exact format-qualified storage image. It is intentionally not accepted by `bindTexture`. | Exact declared CPU transfers only; no image alias or GPU-copy method. |
| `StorageBuffer` / `StorageBufferT<T>` / `ConstantBufferT<T>` | Higher-level GPU-driven paths may bind it as shader storage; indirect draw accepts only `IndirectArguments`. `Vertex`/`Index` remain allocation intents until an explicit typed draw bridge exists. | `bindStorageBuffer` requires the `Storage` role; `bindConstantBuffer` requires the independent `Constant` role. The typed constant wrapper owns this same shared resource rather than a second allocator. | `copyTo` accepts only another same-device `StorageBuffer` with matching transfer roles and valid non-overlapping ranges. It never aliases an XNA vertex/index buffer. |

The listed Vulkan transitions are internal uses in one ordered command stream: attachment/transfer
writes become sampled reads, and compute/transfer writes become their next declared consumers at
the consuming command. The caller does not invoke `ComputeShader::barrier` for any bridge in this
table. That older method remains available for renderer-specific legacy code, but it is not part of
the correctness contract above.

The shared `ModernResourceInteropTypeTest` compile-time matrix pins which public types can cross
each overload. `ComputeTest.PortableComputeSamplesTexture2DAndRetainsItsDeferredLifetime`,
`PortableComputeSamplesDeferredRenderTargetWithoutPublicBarriers` and
`TextureInteropRejectsDisposedForeignAndInvalidAccessBeforeBackendWork` use one generated package
on EasyGL and Vulkan. `VulkanRejectsAnIntegerSamplerBeforeItCanAliasAFloatTexture` additionally
pins the SPIR-V sampled-component contract; Vulkan runs pass on both RADV and llvmpipe with Khronos
validation enabled.

## Typed constant buffers (`MOD-2230`)

`ConstantBufferT<T>` is a constrained typed view over `StorageBuffer`, not another graphics
resource family. `T` must be trivially copyable and standard-layout. Construction asks the live
device for `MaxUniformBufferBytes` and `MinUniformBufferOffsetAlignment`, rounds the allocation up
without overflow, declares only `StorageBufferUsage::Constant` plus CPU write access, and refuses
an unknown, zero or insufficient contract before native allocation. `setData` copies the exact
object representation and clears every alignment-padding byte; field order and shader-language
block padding remain the caller's explicit responsibility.

`ShaderBindingTypeEXT::ConstantBuffer` is append-only value 6. Package selection accepts its
portable route only for a compute stage with a published uniform range and compute support.
`ComputeShader::bindConstantBuffer` accepts either the typed wrapper or a compatible shared buffer,
and validates the slot, lifetime, owning device and immutable role before renderer work. EasyGL
uses `GL_UNIFORM_BUFFER` binding points and live GL size/alignment queries. Vulkan reflects set-zero
SPIR-V `Uniform` + `Block` declarations, allocates `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER` descriptors,
tracks the buffer as a shader read and retains it through deferred dispatch completion.

The generated `constant_buffer` package contains equivalent GLSL ES and SPIR-V compute programs.
`ComputeTest.PortableConstantBufferExecutesRetainsLifetimeAndObservesUpdates` copies a real `vec4`
from the constant buffer to an SSBO, destroys the first public wrapper before readback, then proves
a replacement binding on the next dispatch. It passes on Mesa EasyGL, Vulkan llvmpipe and RADV;
both Vulkan runs use Khronos validation and emit no message. Mock contracts separately pin every
public method, constraint, padding byte, refusal and the renderer-neutral false default.

## Per-task reconciliation

`Supplied` means the present tree already satisfies the row and its permanent evidence. `Partial`
means a named seam or subset exists but the Phase 22 acceptance condition does not. `Absent` means
the public/backend path named by the row does not exist. This classification is about the baseline
tree, not about native API potential.

| Task | Baseline state | Evidence / required update |
|---|---|---|
| MOD-2200 | Supplied | The obsolete draft was reconciled into the live plan. |
| MOD-2201 | Supplied by this audit | This document and the repeatable three-renderer probe are the evidence. |
| MOD-2202 | Supplied | Accepted ADR 0001 defines portable ordering, retention, disposal, synchronization and refusal semantics across immediate and deferred APIs. |
| MOD-2203 | Supplied | Immutable schema-2 feature/limit/format profile and C ABI are tested. |
| MOD-2210 | Partial | `ShaderDialectEXT` exists, but not public language/stage values or stage-specific false-by-default queries. |
| MOD-2211 | Absent | No owning validated text/binary shader payload. |
| MOD-2212 | Absent | No multi-variant shader package. |
| MOD-2213 | Absent | No deterministic live-renderer variant selector. |
| MOD-2214 | Partial | Existing compute/effect string constructors exist; package/code overloads do not. |
| MOD-2215 | Partial | Compile-error strings and one-shot logging exist; structured owned diagnostics do not. |
| MOD-2216 | Absent | No declared reproducible package build tool. |
| MOD-2217 | Absent | No one-package multi-backend selection oracle. |
| MOD-2220 | Supplied | All 22 limit identities and immutable snapshot plumbing exist. |
| MOD-2221 | Supplied | All 13 usage identities and known/supported masks exist. |
| MOD-2222 | Supplied | Vulkan derives implemented format/limit promises from physical-device facts. |
| MOD-2223 | Supplied | Vulkan has exact float/HDR `RenderTarget2D` storage, rendering and readback. |
| MOD-2224 | Supplied | Default and Vulkan constructor/format/limit contract suites are permanent. |
| MOD-2225 | Supplied | Immutable `Texture2DArrayDescriptor`, usage mask and tracked `Texture2DArray` facade validate cached live limits/format usages before a false-by-default renderer factory. No native handle is exposed. |
| MOD-2226 | Supplied | Exact layer/mip/rectangle upload and readback validate native-format bytes and compressed blocks before dispatch. `ShaderEffect` array binding retains only the internal record and refuses unsupported renderers; Vulkan samples bindings 16..18. |
| MOD-2227 | Supplied | Immutable `StorageTexture2DDescriptor`, declared storage/sampling/transfer usage and tracked `StorageTexture2D` validate live limits plus combined format facts before a false-by-default renderer factory; exact mip/rectangle transfers expose no native handle or barrier. |
| MOD-2228 | Supplied | `ComputeShader::bindStorageTexture` validates explicit access/device/lifetime and forwards the retained internal record through a false-by-default renderer seam. Vulkan reflects exact SPIR-V image slot/format/access metadata; the live oracle proves compute write to byte-exact readback and a sampled draw. |
| MOD-2229 | Supplied | Immutable usage/CPU-access descriptors, tracked lifetime, overflow-safe ranges and GPU copies are public. Vulkan allocates exact role flags, keeps CPU-none memory unmapped and proves staging-through-copy on two devices. |
| MOD-2230 | Supplied | `ConstantBufferT<T>` reuses the tracked shared-buffer core, validates live range/alignment and zeroes padding. Compute/package slots bind real GL uniform buffers and reflected Vulkan uniform descriptors; one generated package proves output, replacement and deferred lifetime on EasyGL, RADV and llvmpipe. |
| MOD-2231 | Supplied | `StorageBufferUsage::IndirectArguments` is independently creatable on an indirect-capable device without compute or a storage-buffer byte limit; mixed compute-written arguments explicitly require both roles. |
| MOD-2232 | Supplied | Engine revision 8 adds the direct, detailed-feature-gated base-instance draw; Vulkan retains the shifted instance range and proves instance one independently on RADV and llvmpipe. |
| MOD-2233 | Supplied | The typed matrix above defines every legal XNA/new-resource bridge and explicit non-alias. One generated compute package proves Texture2D and deferred RenderTarget2D sampling, retained lifetime, automatic ordering and deterministic validation on EasyGL plus Vulkan/RADV/llvmpipe. Vulkan compute snapshots retain native identities, not shared resource ownership; dying buffers/views evict and fence-retire matching sets so resource lifetime ends after accepted work without stale-handle reuse. |
| MOD-2234 | Supplied | Vulkan's exact nine-format render-target table now reaches `RenderTargetCube`: cube-compatible allocation, face views, passes, framebuffer keys, mips and MSAA preserve the requested format. The live oracle constructs every advertised format and reads six distinct unclamped RGBA16F faces exactly on RADV and llvmpipe. |
| MOD-2235 | Supplied | Vulkan's stock rigid and skinned PBR paths consume the existing three-product IBL bundle with EasyGL-equivalent split-sum shading, roughness-selected cube mips, ambient-only occlusion and descriptor-safe deferred lifetime. The shared oracle passes on RADV and llvmpipe with validation. |
| MOD-2236 | Supplied | Vulkan's four stock receiver families use EasyGL-equivalent directional/cascade/point/spot equations through one common descriptor/UBO layout. A caster-independent pixel oracle passes 11/11 on EasyGL, RADV and llvmpipe; `MOD-2237` supplies the independently owned generation half. |
| MOD-2237 | Supplied | Every engine shadow caster now selects a reproducibly generated GLSL ES/desktop GLSL/SPIR-V package. Vulkan's matrix UBO and vertex-input reflection preserve per-object worlds and rigid/skinned declarations; directional, cascade and punctual applications plus all 31 visibility checks pass on RADV and llvmpipe with validation. |
| MOD-2238 | Supplied | `Skybox` selects one reproducibly generated GLSL ES/desktop GLSL/SPIR-V package. Its existing custom-effect matrix/vector/scalar slots and cube unit 1 carry the complete contract without renderer changes; the shared 15-case pixel suite and 3-case application oracle pass identically on EasyGL, RADV and llvmpipe. |
| MOD-2240 | Supplied | Vulkan discovery separates supported and enabled facts and records its ordered queue. |
| MOD-2241 | Supplied | Vulkan implements the existing compute/storage-buffer baseline. |
| MOD-2242 | Supplied | Vulkan reflects bounded SSBO/push-constant bindings and reuses descriptors. |
| MOD-2243 | Supplied | Vulkan allocation, full-array views, subresource transfers, sampled descriptors and retirement pass both the functional oracle and an independent 27-format × 5-usage raw-device/factory/lifetime matrix on RADV and llvmpipe. |
| MOD-2244 | Supplied | Vulkan has fifteen exactly mapped/query-qualified dedicated storage-image formats, conditionally storage-capable ordinary `Texture2D` allocations, and zero-copy bridges for exact eligible `RenderTarget2D` formats. Reflected format/access validation, deferred uploads, sampled transitions, cross-target compute/readback closure and the `MOD-2252` lifetime matrix cover the complete path. |
| MOD-2245 | Supplied | Vulkan gates on enabled `drawIndirectFirstInstance`, executes both canonical commands without CPU readback, preserves every geometry/instance offset, retains deferred argument lifetime and inserts the automatic indirect-read dependency. The six-leg oracle passes on RADV and llvmpipe. |
| MOD-2246 | Supplied | Vulkan recycles two-slot timestamp pools, converts with the selected device period, polls without blocking and integrates optional debug-utils labels/messages with one logger copy. The eight-leg native oracle passes on RADV and llvmpipe. |
| MOD-2247 | Supplied | Immutable compute, buffer-copy and image-upload records share the existing XNA ordering/segment domain; routine dispatch/copy add no separate submission or wait. |
| MOD-2248 | Supplied | Per-buffer and per-image-subresource logical usage derives exact Vulkan barriers internally and elides compatible repeated reads. |
| MOD-2249 | Supplied | Storage-image uploads are immutable ordered records; one requested readback records upload/compute/upload plus copy in one synchronous submission. |
| MOD-2250 | Supplied | Reflected readonly set-2 storage buffers feed vertex and fragment stages from immutable retained draw snapshots. A compute-authored transform, fragment value and indirect count prove fresh left/right/zero frames with exact dependencies on RADV and llvmpipe. |
| MOD-2251 | Supplied | Canonical RGBA8 `Color` render targets share one tracked image across attachment, compute and sampling uses. The two-driver oracle image-loads a rendered texel, rewrites it in compute, samples it through ordinary SpriteBatch, observes exactly two barriers and emits no validation message. Dedicated storage-image sampling also transitions in the consuming command buffer without an eager wait. |
| MOD-2252 | Supplied | A 10-leg two-driver native matrix covers buffers, compute programs, dedicated/RT storage images, arrays and timestamp pools across presented work, pre-present destruction, real swapchain recreation and explicit device-first teardown. Its post-recording expiry checks also prevent compute descriptor caches from silently extending buffer/image/target lifetime. All handles are released and owners disconnected before retained records outlive the device. |
| MOD-2253 | Supplied | One-time work waits its own submission fence rather than the queue; off-screen readback records the exact producer closure, transitions, copy and restore in one submit/fence. A 2,048-frame matrix with 32 real resize requests keeps descriptor/pipeline counts constant, staging at four live and retirement at 15 buckets/29 handles, then drains every transient resource on RADV and llvmpipe. |
| MOD-2254 | Supplied | The two-driver validation matrix now includes acquire-out-of-date retention plus present-suboptimal/out-of-date recovery with exact modern output and stable handles. Recovery, complete modern lifetime and the 2,048-frame allocation stress also pass under ASan+UBSan on RADV and llvmpipe. |
| MOD-2260 | Partial | OpenGL4 has a 4.1-floor renderer and measured 4.5 context, but does not independently discover modern subsets. |
| MOD-2261 | Partial | OpenGL4's XNA baseline is substantial; the Phase 22 float/array/compute/image/indirect/timer contracts are unimplemented. |
| MOD-2262 | Partial | The capability probe is shared; the functional modern-GPU contract suite does not yet exist. |
| MOD-2263 | Partial | Existing CNAEXT-off, headless and module gates predate the new APIs and must be retaken after implementation. |
| MOD-2264 | Partial | Capability reports and renderer docs exist; packaging, implicit synchronization and final portability guidance do not. |
| MOD-2265 | Partial | Existing performance notes/timer support do not cover all Phase 22 paths or three measured backends. |
| MOD-2266 | Absent | Mandatory Vulkan paths, applicable OpenGL4 portability, shared gates and no-stall evidence are not complete. |

The portable contract prerequisite is supplied by `MOD-2202`; `MOD-2226` supplies the texture-array
transfer/binding surface and functional oracle, and `MOD-2243` independently closes Vulkan's raw
limit/factory/device-teardown audit. EasyGL/OpenGL4 portability remains `MOD-2261` work.
