# Manifest shard: `cna-graphics`

[<- Back to AUDIT_MANIFEST.md](../AUDIT_MANIFEST.md)

Files in this shard: **75**

Grown by `plans/plan_modern.md` `MOD-12`. The five files audited before that plan began were
the whole of `CNA::Graphics` at the time; the engine layer has since become the plan's
main body of work, and the rows below are its work-queue entries. Paths are given in the
logical `include/`+`src/` form this manifest has always used; each report names the
physical `modules/graphics-ext/` location as well.

| # | Path | Status | Audit Report |
|---|------|--------|---------------|
| 1 | `include/CNA/Graphics/AsciiPostProcessEffect.hpp` | PENDING | [AsciiPostProcessEffect.hpp.audit.md](../include/CNA/Graphics/AsciiPostProcessEffect.hpp.audit.md) |
| 2 | `include/CNA/Graphics/AsciiQuantizeMode.hpp` | PENDING | [AsciiQuantizeMode.hpp.audit.md](../include/CNA/Graphics/AsciiQuantizeMode.hpp.audit.md) |
| 3 | `include/CNA/Graphics/AutoExposureEXT.hpp` | PENDING | [AutoExposureEXT.hpp.audit.md](../include/CNA/Graphics/AutoExposureEXT.hpp.audit.md) |
| 4 | `include/CNA/Graphics/BlitPass.hpp` | PENDING | [BlitPass.hpp.audit.md](../include/CNA/Graphics/BlitPass.hpp.audit.md) |
| 5 | `include/CNA/Graphics/BloomPass.hpp` | PENDING | [BloomPass.hpp.audit.md](../include/CNA/Graphics/BloomPass.hpp.audit.md) |
| 6 | `include/CNA/Graphics/CNAEXT.hpp` | PENDING | [CNAEXT.hpp.audit.md](../include/CNA/Graphics/CNAEXT.hpp.audit.md) |
| 7 | `include/CNA/Graphics/CRTEffect.hpp` | PENDING | [CRTEffect.hpp.audit.md](../include/CNA/Graphics/CRTEffect.hpp.audit.md) |
| 8 | `include/CNA/Graphics/CRTMaskType.hpp` | PENDING | [CRTMaskType.hpp.audit.md](../include/CNA/Graphics/CRTMaskType.hpp.audit.md) |
| 9 | `include/CNA/Graphics/CascadedShadowMap.hpp` | PENDING | [CascadedShadowMap.hpp.audit.md](../include/CNA/Graphics/CascadedShadowMap.hpp.audit.md) |
| 10 | `include/CNA/Graphics/ComputeShader.hpp` | PENDING | [ComputeShader.hpp.audit.md](../include/CNA/Graphics/ComputeShader.hpp.audit.md) |
| 11 | `include/CNA/Graphics/CubeShadowMap.hpp` | PENDING | [CubeShadowMap.hpp.audit.md](../include/CNA/Graphics/CubeShadowMap.hpp.audit.md) |
| 12 | `include/CNA/Graphics/DepthEffect.hpp` | PENDING | [DepthEffect.hpp.audit.md](../include/CNA/Graphics/DepthEffect.hpp.audit.md) |
| 13 | `include/CNA/Graphics/DepthEffectMode.hpp` | PENDING | [DepthEffectMode.hpp.audit.md](../include/CNA/Graphics/DepthEffectMode.hpp.audit.md) |
| 14 | `include/CNA/Graphics/DirectionalLightEXT.hpp` | PENDING | [DirectionalLightEXT.hpp.audit.md](../include/CNA/Graphics/DirectionalLightEXT.hpp.audit.md) |
| 15 | `include/CNA/Graphics/DitherMode.hpp` | PENDING | [DitherMode.hpp.audit.md](../include/CNA/Graphics/DitherMode.hpp.audit.md) |
| 16 | `include/CNA/Graphics/EngineException.hpp` | PENDING | [EngineException.hpp.audit.md](../include/CNA/Graphics/EngineException.hpp.audit.md) |
| 17 | `include/CNA/Graphics/EngineLayerVersion.hpp` | PENDING | [EngineLayerVersion.hpp.audit.md](../include/CNA/Graphics/EngineLayerVersion.hpp.audit.md) |
| 18 | `include/CNA/Graphics/EnvironmentProcessor.hpp` | PENDING | [EnvironmentProcessor.hpp.audit.md](../include/CNA/Graphics/EnvironmentProcessor.hpp.audit.md) |
| 19 | `include/CNA/Graphics/FrustumCullerEXT.hpp` | PENDING | [FrustumCullerEXT.hpp.audit.md](../include/CNA/Graphics/FrustumCullerEXT.hpp.audit.md) |
| 20 | `include/CNA/Graphics/FullscreenPass.hpp` | PENDING | [FullscreenPass.hpp.audit.md](../include/CNA/Graphics/FullscreenPass.hpp.audit.md) |
| 21 | `include/CNA/Graphics/FxaaPass.hpp` | PENDING | [FxaaPass.hpp.audit.md](../include/CNA/Graphics/FxaaPass.hpp.audit.md) |
| 22 | `include/CNA/Graphics/GltfMaterialBridge.hpp` | PENDING | [GltfMaterialBridge.hpp.audit.md](../include/CNA/Graphics/GltfMaterialBridge.hpp.audit.md) |
| 23 | `include/CNA/Graphics/InstancedRendererEXT.hpp` | PENDING | [InstancedRendererEXT.hpp.audit.md](../include/CNA/Graphics/InstancedRendererEXT.hpp.audit.md) |
| 24 | `include/CNA/Graphics/LodGroupEXT.hpp` | PENDING | [LodGroupEXT.hpp.audit.md](../include/CNA/Graphics/LodGroupEXT.hpp.audit.md) |
| 25 | `include/CNA/Graphics/MaterialBinding.hpp` | PENDING | [MaterialBinding.hpp.audit.md](../include/CNA/Graphics/MaterialBinding.hpp.audit.md) |
| 26 | `include/CNA/Graphics/PbrMaterial.hpp` | AUDITED | [PbrMaterial.hpp.audit.md](../include/CNA/Graphics/PbrMaterial.hpp.audit.md) |
| 27 | `include/CNA/Graphics/PointLightEXT.hpp` | PENDING | [PointLightEXT.hpp.audit.md](../include/CNA/Graphics/PointLightEXT.hpp.audit.md) |
| 28 | `include/CNA/Graphics/PostProcessChain.hpp` | PENDING | [PostProcessChain.hpp.audit.md](../include/CNA/Graphics/PostProcessChain.hpp.audit.md) |
| 29 | `include/CNA/Graphics/PostProcessContext.hpp` | PENDING | [PostProcessContext.hpp.audit.md](../include/CNA/Graphics/PostProcessContext.hpp.audit.md) |
| 30 | `include/CNA/Graphics/PostProcessPass.hpp` | PENDING | [PostProcessPass.hpp.audit.md](../include/CNA/Graphics/PostProcessPass.hpp.audit.md) |
| 31 | `include/CNA/Graphics/RenderPipeline.hpp` | PENDING | [RenderPipeline.hpp.audit.md](../include/CNA/Graphics/RenderPipeline.hpp.audit.md) |
| 32 | `include/CNA/Graphics/RenderPipelineSettings.hpp` | AUDITED | [RenderPipelineSettings.hpp.audit.md](../include/CNA/Graphics/RenderPipelineSettings.hpp.audit.md) |
| 33 | `include/CNA/Graphics/RenderQuality.hpp` | AUDITED | [RenderQuality.hpp.audit.md](../include/CNA/Graphics/RenderQuality.hpp.audit.md) |
| 34 | `include/CNA/Graphics/RenderTargetPool.hpp` | PENDING | [RenderTargetPool.hpp.audit.md](../include/CNA/Graphics/RenderTargetPool.hpp.audit.md) |
| 35 | `include/CNA/Graphics/RequireCapability.hpp` | PENDING | [RequireCapability.hpp.audit.md](../include/CNA/Graphics/RequireCapability.hpp.audit.md) |
| 36 | `include/CNA/Graphics/ShadowMap.hpp` | PENDING | [ShadowMap.hpp.audit.md](../include/CNA/Graphics/ShadowMap.hpp.audit.md) |
| 37 | `include/CNA/Graphics/ShadowQuality.hpp` | AUDITED | [ShadowQuality.hpp.audit.md](../include/CNA/Graphics/ShadowQuality.hpp.audit.md) |
| 38 | `include/CNA/Graphics/Skybox.hpp` | PENDING | [Skybox.hpp.audit.md](../include/CNA/Graphics/Skybox.hpp.audit.md) |
| 39 | `include/CNA/Graphics/SpotLightEXT.hpp` | PENDING | [SpotLightEXT.hpp.audit.md](../include/CNA/Graphics/SpotLightEXT.hpp.audit.md) |
| 40 | `include/CNA/Graphics/SpotShadowMap.hpp` | PENDING | [SpotShadowMap.hpp.audit.md](../include/CNA/Graphics/SpotShadowMap.hpp.audit.md) |
| 41 | `include/CNA/Graphics/SsaoPass.hpp` | PENDING | [SsaoPass.hpp.audit.md](../include/CNA/Graphics/SsaoPass.hpp.audit.md) |
| 42 | `include/CNA/Graphics/StorageBuffer.hpp` | PENDING | [StorageBuffer.hpp.audit.md](../include/CNA/Graphics/StorageBuffer.hpp.audit.md) |
| 43 | `include/CNA/Graphics/TonemapPass.hpp` | PENDING | [TonemapPass.hpp.audit.md](../include/CNA/Graphics/TonemapPass.hpp.audit.md) |
| 44 | `include/CNA/Graphics/TonemappingMode.hpp` | AUDITED | [TonemappingMode.hpp.audit.md](../include/CNA/Graphics/TonemappingMode.hpp.audit.md) |
| 45 | `src/CNA/Graphics/AsciiPostProcessEffect.cpp` | PENDING | [AsciiPostProcessEffect.cpp.audit.md](../src/CNA/Graphics/AsciiPostProcessEffect.cpp.audit.md) |
| 46 | `src/CNA/Graphics/AutoExposureEXT.cpp` | PENDING | [AutoExposureEXT.cpp.audit.md](../src/CNA/Graphics/AutoExposureEXT.cpp.audit.md) |
| 47 | `src/CNA/Graphics/BlitPass.cpp` | PENDING | [BlitPass.cpp.audit.md](../src/CNA/Graphics/BlitPass.cpp.audit.md) |
| 48 | `src/CNA/Graphics/BloomPass.cpp` | PENDING | [BloomPass.cpp.audit.md](../src/CNA/Graphics/BloomPass.cpp.audit.md) |
| 49 | `src/CNA/Graphics/CRTEffect.cpp` | PENDING | [CRTEffect.cpp.audit.md](../src/CNA/Graphics/CRTEffect.cpp.audit.md) |
| 50 | `src/CNA/Graphics/CascadedShadowMap.cpp` | PENDING | [CascadedShadowMap.cpp.audit.md](../src/CNA/Graphics/CascadedShadowMap.cpp.audit.md) |
| 51 | `src/CNA/Graphics/ComputeShader.cpp` | PENDING | [ComputeShader.cpp.audit.md](../src/CNA/Graphics/ComputeShader.cpp.audit.md) |
| 52 | `src/CNA/Graphics/CubeShadowMap.cpp` | PENDING | [CubeShadowMap.cpp.audit.md](../src/CNA/Graphics/CubeShadowMap.cpp.audit.md) |
| 53 | `src/CNA/Graphics/DepthEffect.cpp` | PENDING | [DepthEffect.cpp.audit.md](../src/CNA/Graphics/DepthEffect.cpp.audit.md) |
| 54 | `src/CNA/Graphics/EngineException.cpp` | PENDING | [EngineException.cpp.audit.md](../src/CNA/Graphics/EngineException.cpp.audit.md) |
| 55 | `src/CNA/Graphics/EngineLayerVersion.cpp` | PENDING | [EngineLayerVersion.cpp.audit.md](../src/CNA/Graphics/EngineLayerVersion.cpp.audit.md) |
| 56 | `src/CNA/Graphics/EnvironmentProcessor.cpp` | PENDING | [EnvironmentProcessor.cpp.audit.md](../src/CNA/Graphics/EnvironmentProcessor.cpp.audit.md) |
| 57 | `src/CNA/Graphics/FrustumCullerEXT.cpp` | PENDING | [FrustumCullerEXT.cpp.audit.md](../src/CNA/Graphics/FrustumCullerEXT.cpp.audit.md) |
| 58 | `src/CNA/Graphics/FullscreenPass.cpp` | PENDING | [FullscreenPass.cpp.audit.md](../src/CNA/Graphics/FullscreenPass.cpp.audit.md) |
| 59 | `src/CNA/Graphics/FxaaPass.cpp` | PENDING | [FxaaPass.cpp.audit.md](../src/CNA/Graphics/FxaaPass.cpp.audit.md) |
| 60 | `src/CNA/Graphics/InstancedRendererEXT.cpp` | PENDING | [InstancedRendererEXT.cpp.audit.md](../src/CNA/Graphics/InstancedRendererEXT.cpp.audit.md) |
| 61 | `src/CNA/Graphics/LodGroupEXT.cpp` | PENDING | [LodGroupEXT.cpp.audit.md](../src/CNA/Graphics/LodGroupEXT.cpp.audit.md) |
| 62 | `src/CNA/Graphics/MaterialBinding.cpp` | PENDING | [MaterialBinding.cpp.audit.md](../src/CNA/Graphics/MaterialBinding.cpp.audit.md) |
| 63 | `src/CNA/Graphics/PbrMaterial.cpp` | AUDITED | [PbrMaterial.cpp.audit.md](../src/CNA/Graphics/PbrMaterial.cpp.audit.md) |
| 64 | `src/CNA/Graphics/PostProcessChain.cpp` | PENDING | [PostProcessChain.cpp.audit.md](../src/CNA/Graphics/PostProcessChain.cpp.audit.md) |
| 65 | `src/CNA/Graphics/PostProcessPass.cpp` | PENDING | [PostProcessPass.cpp.audit.md](../src/CNA/Graphics/PostProcessPass.cpp.audit.md) |
| 66 | `src/CNA/Graphics/RenderPipeline.cpp` | PENDING | [RenderPipeline.cpp.audit.md](../src/CNA/Graphics/RenderPipeline.cpp.audit.md) |
| 67 | `src/CNA/Graphics/RenderPipelineSettings.cpp` | AUDITED | [RenderPipelineSettings.cpp.audit.md](../src/CNA/Graphics/RenderPipelineSettings.cpp.audit.md) |
| 68 | `src/CNA/Graphics/RenderTargetPool.cpp` | PENDING | [RenderTargetPool.cpp.audit.md](../src/CNA/Graphics/RenderTargetPool.cpp.audit.md) |
| 69 | `src/CNA/Graphics/RequireCapability.cpp` | PENDING | [RequireCapability.cpp.audit.md](../src/CNA/Graphics/RequireCapability.cpp.audit.md) |
| 70 | `src/CNA/Graphics/ShadowMap.cpp` | PENDING | [ShadowMap.cpp.audit.md](../src/CNA/Graphics/ShadowMap.cpp.audit.md) |
| 71 | `src/CNA/Graphics/Skybox.cpp` | PENDING | [Skybox.cpp.audit.md](../src/CNA/Graphics/Skybox.cpp.audit.md) |
| 72 | `src/CNA/Graphics/SpotShadowMap.cpp` | PENDING | [SpotShadowMap.cpp.audit.md](../src/CNA/Graphics/SpotShadowMap.cpp.audit.md) |
| 73 | `src/CNA/Graphics/SsaoPass.cpp` | PENDING | [SsaoPass.cpp.audit.md](../src/CNA/Graphics/SsaoPass.cpp.audit.md) |
| 74 | `src/CNA/Graphics/StorageBuffer.cpp` | PENDING | [StorageBuffer.cpp.audit.md](../src/CNA/Graphics/StorageBuffer.cpp.audit.md) |
| 75 | `src/CNA/Graphics/TonemapPass.cpp` | PENDING | [TonemapPass.cpp.audit.md](../src/CNA/Graphics/TonemapPass.cpp.audit.md) |
