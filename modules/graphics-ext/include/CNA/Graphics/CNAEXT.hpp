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

// ---- Configuration and enumerations -------------------------------------------------------
#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "CNA/Graphics/RenderQuality.hpp"
#include "CNA/Graphics/ShadowQuality.hpp"
#include "CNA/Graphics/TonemappingMode.hpp"

// ---- Post-process infrastructure -----------------------------------------------------------
#include "CNA/Graphics/BlitPass.hpp"
#include "CNA/Graphics/BloomPass.hpp"
#include "CNA/Graphics/FullscreenPass.hpp"
#include "CNA/Graphics/FxaaPass.hpp"
#include "CNA/Graphics/PostProcessChain.hpp"
#include "CNA/Graphics/PostProcessContext.hpp"
#include "CNA/Graphics/PostProcessPass.hpp"
#include "CNA/Graphics/RenderPipeline.hpp"
#include "CNA/Graphics/RenderTargetPool.hpp"
#include "CNA/Graphics/SsaoPass.hpp"
#include "CNA/Graphics/TonemapPass.hpp"

// ---- Shadows ---------------------------------------------------------------------------------
#include "CNA/Graphics/CascadedShadowMap.hpp"
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
#include "CNA/Graphics/LodGroupEXT.hpp"

// ---- Materials -----------------------------------------------------------------------------
#include "CNA/Graphics/GltfMaterialBridge.hpp"
#include "CNA/Graphics/MaterialBinding.hpp"
#include "CNA/Graphics/PbrMaterial.hpp"

// ---- Post-process effects ------------------------------------------------------------------
#include "CNA/Graphics/AsciiPostProcessEffect.hpp"
#include "CNA/Graphics/AsciiQuantizeMode.hpp"
#include "CNA/Graphics/CRTEffect.hpp"
#include "CNA/Graphics/CRTMaskType.hpp"
#include "CNA/Graphics/DepthEffect.hpp"
#include "CNA/Graphics/DepthEffectMode.hpp"
#include "CNA/Graphics/DitherMode.hpp"

#endif // CNA_CNAEXT
