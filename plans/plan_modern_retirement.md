# Plan: retiring the modern GPU abstraction layer (MRT)

Started 2026-10-13. The public `ComputeShader`, `StorageBuffer`, `StorageTexture2D`,
`Texture2DArray` and `GpuTimer` classes were retired in the 2026-09 scope reduction, but their
renderer interfaces, backend implementations, capability identities, limits, C API routes, tests
and documentation stayed behind. This campaign finishes the retirement vertically: nothing that
exists only to serve compute dispatch, storage buffers/images, the texture-array facade, the public
`GraphicsMemoryBarrier`, indirect drawing over those resources or the GPU-timer abstraction
survives, while ordinary graphics (glTF/PBR/IBL/shadows, `ShaderEffect`, vertex/fragment
`ShaderPackageEXT`, float render targets, instancing, compiled effects, uniform blocks and every
native mechanism those need) is untouched. The living-room-simulator, the one external consumer
of the retired layer, is rewritten against public CNA APIs first.

| Task | Scope | Status |
|---|---|---|
| MRT-001 | living-room-simulator: fragment-pass luminance meter (64x36 RGBA8-packed log-luminance cells, public `GetData` readback, shared `ExposureStatistics`) replaces the compute `ExposureMeter`/`AutoExposureEXT` reductions; local ComputeShader/StorageBuffer/StorageTexture2D/ConstantBuffer/GpuTimer wrappers, GPU timing and every `CNA/Internal` include removed (simulator `develop` 5e3f541, 58ec402, 78301a1) | done |
| MRT-002 | Vulkan family: `VulkanStorageBufferRenderer`, `VulkanComputeShaderRenderer`, `VulkanStorageTexture2DRenderer`, `VulkanTexture2DArrayRenderer`, `VulkanGpuTimerRenderer`, their factories, dispatch, binding state, lifetime tracking, limits and the `Vulkan_Modern*` tests removed | done |
| MRT-003 | DirectX 11 family: `D3D11ComputeShader`, `D3D11IndirectBuffer`, `D3D11StorageTexture2D`, `D3D11Texture2DArray`, the renderer/effect overrides for them and the shared D3D reflection's byte-address/UAV acceptance removed | done |
| MRT-004 | WebGPU family: `WebGPUModern.*`, `WebGPURendererModern.cpp`, `WebGPUModernEffect.cpp` (surviving effect code relocated), renderer state, reflection branches serving only the retired API removed | done |
| MRT-005 | SDL_GPU family: `SdlGpuModern.*`, renderer overrides, compute-only `SdlGpuSpirvBindings` branches, smoke/capability test assertions removed; base instance (honoured only by indirect draws) and the storage-only format-usage map withdrawn | done |
| MRT-006 | EasyGL family: `EasyGLStorageBufferRenderer`, `EasyGLComputeShaderRenderer`, `EasyGLGpuTimerRenderer`, their APIs and capability reporting removed | done |
| MRT-007 | Common contract: `IComputeShaderRenderer`, `IStorageBufferRenderer`, `IStorageTexture2DRenderer`, `ITexture2DArrayRenderer`, `IGpuTimerRenderer`, the retired `IGraphicsRenderer`/`IEffectRenderer` members, `GraphicsMemoryBarrier`, `GraphicsImageAccess`, `IndirectDrawArguments`, `GraphicsDevice` indirect/compute-limit routes, `GraphicsCapability::ComputeShaders`/`IndirectDraw`, the retired `RendererFeature`/`RendererLimit`/`RendererFormatUsage` identities, `ShaderStageEXT::Compute`, the retired `ShaderBindingTypeEXT` kinds | done |
| MRT-008 | C API: capability values/flags, renderer features, limits, format-usage bits, indirect-argument structs and initializers, compute work-group routes removed; specs, generated routes, smoke/ABI tests, coverage inventory, limitations and ABI baseline regenerated; ABI 0.47.0 | done |
| MRT-009 | Tests and tooling: capability truth/snapshot/contract/defaults/profile tests, `ShaderInfrastructureTests`, SDL_GPU contract audit manifest, shader-package generator compute stage | done |
| MRT-010 | Documentation: `docs/vulkan-renderer.md` rewritten against the surviving renderer; `docs/modern-gpu-baseline.md` deleted; capability-profile, WebGPU, diagnostics, parity-ledger and FNA3D documents corrected | done |
| MRT-011 | Validation: HEADLESS/C API, multi-renderer, Vulkan, WebGPU, SDL_GPU, EasyGL and MinGW DirectX 11 builds; glTF/PBR/IBL/shadow, graphics-ext, C API and ABI suites; simulator build, tests and smoke | done |

Ordinal policy: surviving `RendererFeature`, `RendererLimit` and `RendererFormatUsage` values keep
their numbers; the retired ordinals and bits are permanently reserved (as retired renderer
identities are), so no unrelated public or C ABI value is renumbered.

Outcome (2026-10-13): MRT-001 in living-room-simulator `develop` (5e3f541, 58ec402, 78301a1);
MRT-002..006 one commit per renderer family; MRT-007/008/009 one commit, because the C ABI and the
tests must change with the C++ enumerations to keep every tree building; MRT-010 the documentation.
Validation is recorded in the MRT-007/008/009 commit message. Runtime evidence for DirectX 11 is
Wine+DXVK only; Metal (not built here) was never an implementer of the retired layer.
