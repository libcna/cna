// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNAEXT.hpp
 * @brief Master include for the CNA extended graphics layer (`namespace CNA::Graphics`).
 *
 * Including this single header pulls in every public type of the extension layer that sits above
 * the XNA 4.0 API: the render-pipeline configuration bag, the material description, the quality
 * and tonemapping enumerations, and the renderer-neutral post-process effects.
 *
 * The whole layer is opt-in. Without the `CNA_CNAEXT` compile definition (CMake option
 * `-DCNA_CNAEXT=ON`) every header below is empty, so including this file costs nothing and
 * declares nothing. Note that this gate applies only to the `CNA::Graphics` engine layer --
 * the `CNAEXT`-marked extension members inside `Microsoft::Xna::Framework::Graphics` (PbrEffect,
 * ShaderEffect, morph targets, ...) are always compiled and are not affected by this option.
 *
 * @see CNAEXT.md for the design of this layer, and `plan_modern.md` for its task backlog.
 */

#ifdef CNA_CNAEXT

/**
 * @defgroup cnaext_engine CNA Engine Layer
 * @brief The opt-in `CNA::Graphics` layer above the XNA 4.0 API.
 *
 * Everything here is compiled only when the `CNA_CNAEXT` CMake option is on, which it is not by
 * default. The layer covers the HDR pipeline and its post-process chain, shadows, sky and
 * image-based lighting, materials, instancing with LOD and culling, and compute; `CNAEXT.md`
 * describes the design and `plan_modern.md` tracks the work.
 * @{
 */

// ---- Foundation ------------------------------------------------------------------------------
#include "CNA/Graphics/EngineException.hpp"
#include "CNA/Graphics/EngineLayerVersion.hpp"
#include "CNA/Graphics/RequireCapability.hpp"

// ---- Configuration and enumerations -------------------------------------------------------
#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "CNA/Graphics/RenderQuality.hpp"
#include "CNA/Graphics/ShadowQuality.hpp"
#include "CNA/Graphics/TonemappingMode.hpp"

// ---- Post-process infrastructure -----------------------------------------------------------
#include "CNA/Graphics/AtmosphericSky.hpp"
#include "CNA/Graphics/BlitPass.hpp"
#include "CNA/Graphics/ColorGradePass.hpp"
#include "CNA/Graphics/ChromaticAberrationPass.hpp"
#include "CNA/Graphics/FilmGrainPass.hpp"
#include "CNA/Graphics/HeightFogPass.hpp"
#include "CNA/Graphics/LensFlarePass.hpp"
#include "CNA/Graphics/LightShaftPass.hpp"
#include "CNA/Graphics/VolumetricFogPass.hpp"
#include "CNA/Graphics/MotionBlurPass.hpp"
#include "CNA/Graphics/DepthNormalPrepass.hpp"
#include "CNA/Graphics/DepthOfFieldPass.hpp"
#include "CNA/Graphics/EffectPass.hpp"
#include "CNA/Graphics/BloomPass.hpp"
#include "CNA/Graphics/FullscreenPass.hpp"
#include "CNA/Graphics/FxaaPass.hpp"
#include "CNA/Graphics/PostProcessChain.hpp"
#include "CNA/Graphics/PostProcessContext.hpp"
#include "CNA/Graphics/PostProcessPass.hpp"
#include "CNA/Graphics/RenderPipeline.hpp"
#include "CNA/Graphics/RenderTargetPool.hpp"
#include "CNA/Graphics/ScopedRenderTarget.hpp"
#include "CNA/Graphics/ShaderDiagnostics.hpp"
#include "CNA/Graphics/ShaderEffectFactory.hpp"
#include "CNA/Graphics/SpatialUpscalePass.hpp"
#include "CNA/Graphics/SsaoPass.hpp"
#include "CNA/Graphics/SsrPass.hpp"
#include "CNA/Graphics/ThinFilmIridescence.hpp"
#include "CNA/Graphics/TonemapPass.hpp"

// ---- Shadows ---------------------------------------------------------------------------------
#include "CNA/Graphics/CascadedShadowMap.hpp"
#include "CNA/Graphics/ClusteredForwardEffect.hpp"
#include "CNA/Graphics/ClusteredLightAssignment.hpp"
#include "CNA/Graphics/ClusteredLightEXT.hpp"
#include "CNA/Graphics/ClusteredLightBuffer.hpp"
#include "CNA/Graphics/ClusteredLightCompute.hpp"
#include "CNA/Graphics/ClusteredLightGrid.hpp"
#include "CNA/Graphics/ClusteredLightSetEXT.hpp"
#include "CNA/Graphics/ClusteredLightType.hpp"
#include "CNA/Graphics/ClusteredShadowPolicyEXT.hpp"
#include "CNA/Graphics/CubeShadowMap.hpp"
#include "CNA/Graphics/DirectionalLightEXT.hpp"
#include "CNA/Graphics/PointLightEXT.hpp"
#include "CNA/Graphics/ShadowMap.hpp"
#include "CNA/Graphics/SpotLightEXT.hpp"
#include "CNA/Graphics/SpotShadowMap.hpp"

// ---- Sky and environment ---------------------------------------------------------------------
#include "CNA/Graphics/EnvironmentProcessor.hpp"
#include "CNA/Graphics/Skybox.hpp"

// ---- Compute -----------------------------------------------------------------------------
#include "CNA/Graphics/AutoExposureEXT.hpp"
#include "CNA/Graphics/ComputeShader.hpp"
#include "CNA/Graphics/StorageBuffer.hpp"

// ---- Instancing, LOD and culling -------------------------------------------------------------
#include "CNA/Graphics/FrustumCullerEXT.hpp"
#include "CNA/Graphics/InstancedRendererEXT.hpp"
#include "CNA/Graphics/GpuInstanceCuller.hpp"
#include "CNA/Graphics/LightProbeBaker.hpp"
#include "CNA/Graphics/LightProbeEXT.hpp"
#include "CNA/Graphics/LightProbeVolumeEXT.hpp"
#include "CNA/Graphics/LodGroupEXT.hpp"

// ---- Materials -----------------------------------------------------------------------------
#include "CNA/Graphics/GltfMaterialBridge.hpp"
#include "CNA/Graphics/MaterialBinding.hpp"
#include "CNA/Graphics/PbrMaterial.hpp"
#include "CNA/Graphics/PbrMaterialExtensions.hpp"

// ---- Post-process effects ------------------------------------------------------------------
#include "CNA/Graphics/AreaLightBrdfTable.hpp"
#include "CNA/Graphics/AreaLightShading.hpp"
#include "CNA/Graphics/AsciiPass.hpp"
#include "CNA/Graphics/AsciiPostProcessEffect.hpp"
#include "CNA/Graphics/AsciiQuantizeMode.hpp"
#include "CNA/Graphics/CRTEffect.hpp"
#include "CNA/Graphics/CRTMaskType.hpp"
#include "CNA/Graphics/DepthEffect.hpp"
#include "CNA/Graphics/DepthEffectMode.hpp"
#include "CNA/Graphics/DitherMode.hpp"

/** @} */ // end of cnaext_engine

#endif // CNA_CNAEXT
