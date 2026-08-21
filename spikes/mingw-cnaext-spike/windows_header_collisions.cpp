// SPDX-License-Identifier: MS-PL
//
// plans/plan_modern.md MOD-1719. A D3D renderer's translation unit includes <windows.h> before it
// includes anything of ours, and <windows.h> is a macro minefield: `near` and `far` are object-like
// macros in windef.h, and `GetObject`, `DrawText` and friends are #defined to their A/W variants.
// Any of those turns a perfectly good declaration in this layer into a syntax error only on Windows
// -- `begin(..., float near, float far)` would be the obvious way to write the depth prepass, and it
// would not compile for a single Windows user.
//
// This file is deliberately hostile: it includes <windows.h> with none of the usual defensive
// defines, then every public engine-layer header.
//
// Compile-only probe -- there is no main(), and check.sh only ever passes -fsyntax-only.

#ifdef CNA_CNAEXT

#include <windows.h>

#include "CNA/Graphics/AsciiPass.hpp"
#include "CNA/Graphics/AsciiPostProcessEffect.hpp"
#include "CNA/Graphics/AsciiQuantizeMode.hpp"
#include "CNA/Graphics/AutoExposureEXT.hpp"
#include "CNA/Graphics/BlitPass.hpp"
#include "CNA/Graphics/BloomPass.hpp"
#include "CNA/Graphics/CNAEXT.hpp"
#include "CNA/Graphics/CRTEffect.hpp"
#include "CNA/Graphics/CRTMaskType.hpp"
#include "CNA/Graphics/CascadedShadowMap.hpp"
#include "CNA/Graphics/ComputeShader.hpp"
#include "CNA/Graphics/CubeShadowMap.hpp"
#include "CNA/Graphics/DepthEffect.hpp"
#include "CNA/Graphics/DepthEffectMode.hpp"
#include "CNA/Graphics/DepthNormalPrepass.hpp"
#include "CNA/Graphics/DirectionalLightEXT.hpp"
#include "CNA/Graphics/DitherMode.hpp"
#include "CNA/Graphics/EffectPass.hpp"
#include "CNA/Graphics/EngineException.hpp"
#include "CNA/Graphics/EngineLayerVersion.hpp"
#include "CNA/Graphics/EnvironmentProcessor.hpp"
#include "CNA/Graphics/FrustumCullerEXT.hpp"
#include "CNA/Graphics/FullscreenPass.hpp"
#include "CNA/Graphics/FxaaPass.hpp"
#include "CNA/Graphics/GltfMaterialBridge.hpp"
#include "CNA/Graphics/InstancedRendererEXT.hpp"
#include "CNA/Graphics/LodGroupEXT.hpp"
#include "CNA/Graphics/MaterialBinding.hpp"
#include "CNA/Graphics/PbrMaterial.hpp"
#include "CNA/Graphics/PointLightEXT.hpp"
#include "CNA/Graphics/PostProcessChain.hpp"
#include "CNA/Graphics/PostProcessContext.hpp"
#include "CNA/Graphics/PostProcessPass.hpp"
#include "CNA/Graphics/RenderPipeline.hpp"
#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "CNA/Graphics/RenderQuality.hpp"
#include "CNA/Graphics/RenderTargetPool.hpp"
#include "CNA/Graphics/RequireCapability.hpp"
#include "CNA/Graphics/ScopedRenderTarget.hpp"
#include "CNA/Graphics/ShaderDiagnostics.hpp"
#include "CNA/Graphics/ShaderEffectFactory.hpp"
#include "CNA/Graphics/ShadowMap.hpp"
#include "CNA/Graphics/ShadowQuality.hpp"
#include "CNA/Graphics/Skybox.hpp"
#include "CNA/Graphics/SpotLightEXT.hpp"
#include "CNA/Graphics/SpotShadowMap.hpp"
#include "CNA/Graphics/SsaoPass.hpp"
#include "CNA/Graphics/StorageBuffer.hpp"
#include "CNA/Graphics/TonemapPass.hpp"
#include "CNA/Graphics/TonemappingMode.hpp"

// Prove the macros really are live in this translation unit, so the check above is not passing
// because something quietly defined NOMINMAX or WIN32_LEAN_AND_MEAN behind our backs.
#ifndef near
#error "MOD-1719: <windows.h> did not define near -- this probe is not testing what it claims to"
#endif
#ifndef far
#error "MOD-1719: <windows.h> did not define far -- this probe is not testing what it claims to"
#endif
#ifndef GetObject
#error "MOD-1719: <windows.h> did not define GetObject -- this probe is not testing what it claims to"
#endif

#endif // CNA_CNAEXT
