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
| `CNA::RendererFeature`, `RendererFeatureSupport`, `RendererFeatureInfo` | `modules/graphics` | 31 atomic feature identities and four-state answers. `Count` is a sentinel, not a query. |
| `CNA::RendererLimit`, `RendererLimitValue` | `modules/graphics` | 22 numeric limit identities with an explicit known bit. Zero may be a known unsupported/unimplemented value. |
| `CNA::RendererFormatUsage`, `RendererFormatSupport` | `modules/graphics` | Thirteen independent per-`SurfaceFormat` usage bits with separate known and supported masks. |
| `CNA::RendererCapabilityProfile` | `modules/graphics` | Cached immutable device snapshot plus a generated English report. |
| `GraphicsDevice` capability/profile/format/limit queries | `modules/graphics` | The public device seam; callers must not infer facts from renderer names. |
| `CNA::Internal::Renderers::ShaderDialectEXT` and `GraphicsDevice::GetShaderDialectEXT` | `modules/graphics` | Existing renderer payload hint (`Unknown`, GLSL families, HLSL, MSL, WGSL, SPIR-V). It has no shader stage, owning bytes or variant-selection contract. |
| `Microsoft::Xna::Framework::Graphics::ShaderEffect` | `modules/graphics` | Existing CNAEXT vertex/fragment payload object. Its two string payloads remain renderer-specific. |
| `CNA::Graphics::ComputeShader` | `modules/graphics-ext` | Existing CNAEXT compute object with renderer-specific string source, scalar setters, buffer/texture/image binding, dispatch and a caller-visible barrier method. |
| `CNA::Graphics::StorageBuffer`, `StorageBufferT<T>` | `modules/graphics-ext` | Existing compute-gated byte buffer and typed wrapper. No declared usage, CPU-access intent, range transfer or `GraphicsResource` tracking. |
| `CNA::Graphics::GpuTimer` | `modules/graphics-ext` | Existing nonblocking timer wrapper; EasyGL implements it, Vulkan/OpenGL4 do not at this baseline. |
| `CNA::GraphicsImageAccess` | `modules/graphics` | Three image-access values used by `ComputeShader::bindImage(Texture2D&)`. |
| `CNA::GraphicsMemoryBarrier` and its bit operators | `modules/graphics` | Portable-looking caller barrier mask. Phase 22 replaces the need for callers to drive normal correctness with renderer-owned usage transitions. |
| `CNA::IndirectDrawArguments`, `IndirectDrawIndexedArguments` | `modules/graphics` | Canonical 16-byte/20-byte GPU argument layouts; both already contain base-instance fields. |
| `GraphicsDevice::DrawPrimitivesIndirectEXT`, `DrawIndexedPrimitivesIndirectEXT` | `modules/graphics` | Existing indirect public routes backed by a `StorageBuffer`; EasyGL implements them, Vulkan/OpenGL4 refuse. |
| `CNA::DisplayColorSpace` and device queries | `modules/graphics` | Existing sRGB/scRGB/HDR10 vocabulary; all measured renderers still expose only sRGB presentation. |

There is no public `ShaderCodeEXT`, `ShaderPackageEXT`, `Texture2DArray`, `StorageTexture2D`,
constant buffer, independently creatable indirect-argument buffer, resource usage/access
descriptor, fence or native image/buffer/view type. Those names must therefore extend the surface
above rather than duplicate a hidden implementation.

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
| `CreateComputeShader`, `CreateStorageBuffer` | `nullptr`. |
| `DispatchCompute`, `MemoryBarrierEXT` | No-op. Public wrappers must gate support before reaching these defaults. |
| `ExecutesShaderEffectSourceEXT`, `SupportsShadowSamplingEXT`, `SupportsImageBasedLightingEXT` | `false`. |
| `SupportsComputeShadersEXT`, `SupportsIndirectDrawEXT`, `SupportsComputeImageBindingEXT`, `SupportsTexture3DSamplingEXT` | `false`. |
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
| 18 `IndirectDraw` | S | U | U |

The `CustomEffects` row does not mean source parity. The detailed profile measured source execution
only on EasyGL. Vulkan accepts `ShaderEffect` objects but its current payload route is SPIR-V, and
OpenGL4 accepts the object while its current source-execution query remains false.

## Measured detailed modern subset

| Observable profile fact | EasyGL `OPENGLES3` | Vulkan | OpenGL4 |
|---|---:|---:|---:|
| Shader-effect source execution | S | U | U |
| Compute shaders + storage-buffer dispatch | S | S | U |
| Compute image binding | U | U | U |
| Indirect drawing | S | U | U |
| GPU timers | S | U | U |
| Shadow sampling | S | U | U |
| Image-based lighting | S | U | U |
| Texture3D sampling | S | S | U |
| Payload dialect query | `Unknown` | `SpirV` | `Unknown` |

The dialect result explains why `MOD-2210` is partial, not supplied: an old enum exists, but EasyGL
and OpenGL4 do not identify their real text languages, SPIR-V has no corresponding detailed
`RendererFeature`, and there is no per-stage language query.

### Numeric limits

All values below are the profile's measured values. Zero is deliberately retained where the CNA
path is unavailable or not yet classified; a large native device value is not published early.

| `RendererLimit` | EasyGL | Vulkan | OpenGL4 |
|---|---:|---:|---:|
| `MaxTextureDimension` | 16384 | 16384 | 16384 |
| `MaxVertexStreams` | 16 | 16 | 1 |
| `MaxComputeWorkGroupCountX/Y/Z` | 65535 / 65535 / 65535 | 65535 / 65535 / 65535 | 0 / 0 / 0 |
| `MaxComputeWorkGroupSizeX/Y/Z` | 1024 / 1024 / 1024 | 1024 / 1024 / 1024 | 0 / 0 / 0 |
| `MaxComputeWorkGroupInvocations` | 1024 | 1024 | 0 |
| `MaxVertexShaderStorageBlocks` | 16 | 0 | 0 |
| `MaxStorageBufferBytes` | 0 | 134217728 | 0 |
| `MaxUniformBufferBytes` | 0 | 4608 | 0 |
| `MaxComputeStorageBufferBindings` | 0 | 1000000 | 0 |
| `MaxTextureArrayLayers` | 0 | 0 | 0 |
| `MaxSampledTexturesPerShaderStage` | 0 | 12 | 0 |
| `MaxStorageImagesPerShaderStage` | 0 | 0 | 0 |
| `MaxVertexInputBindings` | 0 | 16 | 0 |
| `MaxVertexInputAttributes` | 0 | 32 | 0 |
| `MaxColorAttachments` | 0 | 4 | 0 |
| `MinStorageBufferOffsetAlignment` | 0 | 16 | 0 |
| `MinUniformBufferOffsetAlignment` | 0 | 16 | 0 |
| `TimestampPeriodPicoseconds` | 0 | 0 | 0 |

Vulkan deliberately withholds array layers, storage images and timestamp period until the matching
CNA paths land. EasyGL has several working older paths whose newly appended detailed limit fields
are still zero; those are classification gaps, not permission to assume unlimited values.

### Format support

- Vulkan classifies all thirteen usage bits for all 27 `SurfaceFormat` values. Its public
  `RenderTarget2D` construction supports exactly nine measured formats (`Color`, `Rgba64`, the
  three 32-bit float and four half/HDR values); standalone texture storage remains narrower.
- EasyGL and OpenGL4 still populate only the older texture-storage, render-target and color-transfer
  facts. EasyGL reports nine texture-storage formats and eight render-target formats. OpenGL4
  reports only `Color` for texture storage and render targets.
- No measured renderer reports storage-image read/write/atomic support through the detailed format
  profile. Texture-array layer support is also zero on all three. These are the live gaps owned by
  `MOD-2225` through `MOD-2228`, `MOD-2243`/`MOD-2244` and `MOD-2261`.

## Per-task reconciliation

`Supplied` means the present tree already satisfies the row and its permanent evidence. `Partial`
means a named seam or subset exists but the Phase 22 acceptance condition does not. `Absent` means
the public/backend path named by the row does not exist. This classification is about the baseline
tree, not about native API potential.

| Task | Baseline state | Evidence / required update |
|---|---|---|
| MOD-2200 | Supplied | The obsolete draft was reconciled into the live plan. |
| MOD-2201 | Supplied by this audit | This document and the repeatable three-renderer probe are the evidence. |
| MOD-2202 | Partial | Ordering/lifetime rules are scattered across renderer docs; there is no portable cross-API ADR and current Vulkan docs still allow unsafe in-flight destruction. |
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
| MOD-2225 | Absent | No `Texture2DArray` public resource or descriptor. |
| MOD-2226 | Absent | No array layer/mip transfer or sampled-binding API. |
| MOD-2227 | Absent | No dedicated storage texture resource. |
| MOD-2228 | Partial | `ComputeShader::bindImage(Texture2D&)` exists; the storage-texture overload, retention and portable path do not. |
| MOD-2229 | Partial | Basic compute-gated `StorageBuffer` exists without usage/access intent, ranges, staging policy or resource tracking. |
| MOD-2230 | Absent | No typed constant-buffer wrapper or binding. |
| MOD-2231 | Partial | Canonical argument structs and indirect draw calls exist, but argument storage cannot be created independently of compute support. |
| MOD-2232 | Partial | Indirect layouts contain `BaseInstance`; no capability-gated direct base-instance draw extension exists. |
| MOD-2233 | Partial | A few ad-hoc XNA texture/buffer bridges exist; no documented/tested interoperability and lifetime matrix exists. |
| MOD-2240 | Supplied | Vulkan discovery separates supported and enabled facts and records its ordered queue. |
| MOD-2241 | Supplied | Vulkan implements the existing compute/storage-buffer baseline. |
| MOD-2242 | Supplied | Vulkan reflects bounded SSBO/push-constant bindings and reuses descriptors. |
| MOD-2243 | Absent | No Vulkan array image/view/transfer/sampling implementation. |
| MOD-2244 | Partial | Vulkan has XNA images and refuses reflected image descriptors; it has no legal storage-image bridge. |
| MOD-2245 | Partial | Public indirect routes and native feature discovery exist; Vulkan truthfully reports unsupported and submits no indirect command. |
| MOD-2246 | Partial | Shared `GpuTimer` exists; Vulkan has no timestamp-query implementation and publishes zero period. |
| MOD-2247 | Partial | XNA Vulkan work has ordered submission, but the current compute slice uses a separate synchronous submission boundary. |
| MOD-2248 | Absent | No internal logical resource-usage tracker. |
| MOD-2249 | Partial | The current buffer compute path emits host/compute barriers, but routine dispatch completion still waits synchronously. |
| MOD-2250 | Absent | No compute-write to graphics/indirect Vulkan dependency path. |
| MOD-2251 | Absent | No two-way render-target/storage-image transition path. |
| MOD-2252 | Partial | XNA `GraphicsResource` tracking exists; modern buffers/shaders are not tracked and there is no fence-retirement queue. |
| MOD-2253 | Partial | Compute descriptor reuse is bounded; broader modern allocators and removal of routine global waits remain open. |
| MOD-2254 | Partial | Several Vulkan validation gates exist, but the mandatory array/image/indirect/timer/order/disposal matrix is incomplete. |
| MOD-2260 | Partial | OpenGL4 has a 4.1-floor renderer and measured 4.5 context, but does not independently discover modern subsets. |
| MOD-2261 | Partial | OpenGL4's XNA baseline is substantial; the Phase 22 float/array/compute/image/indirect/timer contracts are unimplemented. |
| MOD-2262 | Partial | The capability probe is shared; the functional modern-GPU contract suite does not yet exist. |
| MOD-2263 | Partial | Existing CNAEXT-off, headless and module gates predate the new APIs and must be retaken after implementation. |
| MOD-2264 | Partial | Capability reports and renderer docs exist; packaging, implicit synchronization and final portability guidance do not. |
| MOD-2265 | Partial | Existing performance notes/timer support do not cover all Phase 22 paths or three measured backends. |
| MOD-2266 | Absent | Mandatory Vulkan paths, applicable OpenGL4 portability, shared gates and no-stall evidence are not complete. |

The next dependency is `MOD-2202`. Public resource descriptors must not be frozen until their
ordering, retention, disposal, readback and rejection semantics have one portable definition.
