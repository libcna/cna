#include "CNA/Internal/Backends/D3D9/D3D9ShaderDispatch.hpp"

#include <stdexcept>
#include <string>

namespace CNA::Internal::Backends::D3D9
{
    namespace
    {
        [[noreturn]] void ThrowOutOfRange(const char* fn, int shaderIndex, int maxExclusive)
        {
            throw std::out_of_range(
                std::string(fn) + ": shaderIndex " + std::to_string(shaderIndex) +
                " out of range [0, " + std::to_string(maxExclusive) + ")");
        }
    }

    // =============================================================================================
    // BasicEffect -- BasicEffect.cs OnApply() / BasicEffect.fx VSArray[20]/VSIndices[32]/
    // PSArray[10]/PSIndices[32]
    // =============================================================================================

    int ComputeBasicEffectShaderIndex(bool fogEnabled, bool vertexColorEnabled, bool textureEnabled,
                                      bool lightingEnabled, bool preferPerPixelLighting, bool oneLight)
    {
        int shaderIndex = 0;
        if (!fogEnabled) shaderIndex += 1;
        if (vertexColorEnabled) shaderIndex += 2;
        if (textureEnabled) shaderIndex += 4;
        if (lightingEnabled)
        {
            if (preferPerPixelLighting) shaderIndex += 24;
            else if (oneLight) shaderIndex += 16;
            else shaderIndex += 8;
        }
        return shaderIndex;
    }

    namespace
    {
        constexpr const char* kBasicEffectVSArray[20] = {
            "BasicEffect_VSBasic", "BasicEffect_VSBasicNoFog",
            "BasicEffect_VSBasicVc", "BasicEffect_VSBasicVcNoFog",
            "BasicEffect_VSBasicTx", "BasicEffect_VSBasicTxNoFog",
            "BasicEffect_VSBasicTxVc", "BasicEffect_VSBasicTxVcNoFog",
            "BasicEffect_VSBasicVertexLighting", "BasicEffect_VSBasicVertexLightingVc",
            "BasicEffect_VSBasicVertexLightingTx", "BasicEffect_VSBasicVertexLightingTxVc",
            "BasicEffect_VSBasicOneLight", "BasicEffect_VSBasicOneLightVc",
            "BasicEffect_VSBasicOneLightTx", "BasicEffect_VSBasicOneLightTxVc",
            "BasicEffect_VSBasicPixelLighting", "BasicEffect_VSBasicPixelLightingVc",
            "BasicEffect_VSBasicPixelLightingTx", "BasicEffect_VSBasicPixelLightingTxVc",
        };
        constexpr int kBasicEffectVSIndices[32] = {
            0, 1, 2, 3, 4, 5, 6, 7,
            8, 8, 9, 9, 10, 10, 11, 11,
            12, 12, 13, 13, 14, 14, 15, 15,
            16, 16, 17, 17, 18, 18, 19, 19,
        };
        constexpr const char* kBasicEffectPSArray[10] = {
            "BasicEffect_PSBasic", "BasicEffect_PSBasicNoFog",
            "BasicEffect_PSBasicTx", "BasicEffect_PSBasicTxNoFog",
            "BasicEffect_PSBasicVertexLighting", "BasicEffect_PSBasicVertexLightingNoFog",
            "BasicEffect_PSBasicVertexLightingTx", "BasicEffect_PSBasicVertexLightingTxNoFog",
            "BasicEffect_PSBasicPixelLighting", "BasicEffect_PSBasicPixelLightingTx",
        };
        constexpr int kBasicEffectPSIndices[32] = {
            0, 1, 0, 1, 2, 3, 2, 3,
            4, 5, 4, 5, 6, 7, 6, 7,
            4, 5, 4, 5, 6, 7, 6, 7,
            8, 8, 8, 8, 9, 9, 9, 9,
        };
    }

    const char* GetBasicEffectVertexShaderNameEXT(int shaderIndex)
    {
        if (shaderIndex < 0 || shaderIndex >= 32) ThrowOutOfRange("GetBasicEffectVertexShaderNameEXT", shaderIndex, 32);
        return kBasicEffectVSArray[kBasicEffectVSIndices[shaderIndex]];
    }

    const char* GetBasicEffectPixelShaderNameEXT(int shaderIndex)
    {
        if (shaderIndex < 0 || shaderIndex >= 32) ThrowOutOfRange("GetBasicEffectPixelShaderNameEXT", shaderIndex, 32);
        return kBasicEffectPSArray[kBasicEffectPSIndices[shaderIndex]];
    }

    // =============================================================================================
    // AlphaTestEffect -- AlphaTestEffect.cs OnApply() / AlphaTestEffect.fx VSArray[4]/VSIndices[8]/
    // PSArray[4]/PSIndices[8]
    // =============================================================================================

    int ComputeAlphaTestEffectShaderIndex(bool fogEnabled, bool vertexColorEnabled, bool isEqNe)
    {
        int shaderIndex = 0;
        if (!fogEnabled) shaderIndex += 1;
        if (vertexColorEnabled) shaderIndex += 2;
        if (isEqNe) shaderIndex += 4;
        return shaderIndex;
    }

    namespace
    {
        constexpr const char* kAlphaTestEffectVSArray[4] = {
            "AlphaTestEffect_VSAlphaTest", "AlphaTestEffect_VSAlphaTestNoFog",
            "AlphaTestEffect_VSAlphaTestVc", "AlphaTestEffect_VSAlphaTestVcNoFog",
        };
        constexpr int kAlphaTestEffectVSIndices[8] = {
            0, 1, 2, 3,
            0, 1, 2, 3,
        };
        constexpr const char* kAlphaTestEffectPSArray[4] = {
            "AlphaTestEffect_PSAlphaTestLtGt", "AlphaTestEffect_PSAlphaTestLtGtNoFog",
            "AlphaTestEffect_PSAlphaTestEqNe", "AlphaTestEffect_PSAlphaTestEqNeNoFog",
        };
        constexpr int kAlphaTestEffectPSIndices[8] = {
            0, 1, 0, 1,
            2, 3, 2, 3,
        };
    }

    const char* GetAlphaTestEffectVertexShaderNameEXT(int shaderIndex)
    {
        if (shaderIndex < 0 || shaderIndex >= 8) ThrowOutOfRange("GetAlphaTestEffectVertexShaderNameEXT", shaderIndex, 8);
        return kAlphaTestEffectVSArray[kAlphaTestEffectVSIndices[shaderIndex]];
    }

    const char* GetAlphaTestEffectPixelShaderNameEXT(int shaderIndex)
    {
        if (shaderIndex < 0 || shaderIndex >= 8) ThrowOutOfRange("GetAlphaTestEffectPixelShaderNameEXT", shaderIndex, 8);
        return kAlphaTestEffectPSArray[kAlphaTestEffectPSIndices[shaderIndex]];
    }

    // =============================================================================================
    // DualTextureEffect -- DualTextureEffect.cs OnApply() / DualTextureEffect.fx VSArray[4]
    // (indexed directly by ShaderIndex, no VSIndices table) / PSArray[2]/PSIndices[4]
    // =============================================================================================

    int ComputeDualTextureEffectShaderIndex(bool fogEnabled, bool vertexColorEnabled)
    {
        int shaderIndex = 0;
        if (!fogEnabled) shaderIndex += 1;
        if (vertexColorEnabled) shaderIndex += 2;
        return shaderIndex;
    }

    namespace
    {
        constexpr const char* kDualTextureEffectVSArray[4] = {
            "DualTextureEffect_VSDualTexture", "DualTextureEffect_VSDualTextureNoFog",
            "DualTextureEffect_VSDualTextureVc", "DualTextureEffect_VSDualTextureVcNoFog",
        };
        constexpr const char* kDualTextureEffectPSArray[2] = {
            "DualTextureEffect_PSDualTexture", "DualTextureEffect_PSDualTextureNoFog",
        };
        constexpr int kDualTextureEffectPSIndices[4] = {
            0, 1, 0, 1,
        };
    }

    const char* GetDualTextureEffectVertexShaderNameEXT(int shaderIndex)
    {
        if (shaderIndex < 0 || shaderIndex >= 4) ThrowOutOfRange("GetDualTextureEffectVertexShaderNameEXT", shaderIndex, 4);
        return kDualTextureEffectVSArray[shaderIndex];
    }

    const char* GetDualTextureEffectPixelShaderNameEXT(int shaderIndex)
    {
        if (shaderIndex < 0 || shaderIndex >= 4) ThrowOutOfRange("GetDualTextureEffectPixelShaderNameEXT", shaderIndex, 4);
        return kDualTextureEffectPSArray[kDualTextureEffectPSIndices[shaderIndex]];
    }

    // =============================================================================================
    // EnvironmentMapEffect -- EnvironmentMapEffect.cs OnApply() / EnvironmentMapEffect.fx
    // VSArray[4]/VSIndices[16]/PSArray[4]/PSIndices[16]
    // =============================================================================================

    int ComputeEnvironmentMapEffectShaderIndex(bool fogEnabled, bool fresnelEnabled,
                                               bool specularEnabled, bool oneLight)
    {
        int shaderIndex = 0;
        if (!fogEnabled) shaderIndex += 1;
        if (fresnelEnabled) shaderIndex += 2;
        if (specularEnabled) shaderIndex += 4;
        if (oneLight) shaderIndex += 8;
        return shaderIndex;
    }

    namespace
    {
        constexpr const char* kEnvironmentMapEffectVSArray[4] = {
            "EnvironmentMapEffect_VSEnvMap", "EnvironmentMapEffect_VSEnvMapFresnel",
            "EnvironmentMapEffect_VSEnvMapOneLight", "EnvironmentMapEffect_VSEnvMapOneLightFresnel",
        };
        constexpr int kEnvironmentMapEffectVSIndices[16] = {
            0, 0, 1, 1, 0, 0, 1, 1,
            2, 2, 3, 3, 2, 2, 3, 3,
        };
        constexpr const char* kEnvironmentMapEffectPSArray[4] = {
            "EnvironmentMapEffect_PSEnvMap", "EnvironmentMapEffect_PSEnvMapNoFog",
            "EnvironmentMapEffect_PSEnvMapSpecular", "EnvironmentMapEffect_PSEnvMapSpecularNoFog",
        };
        constexpr int kEnvironmentMapEffectPSIndices[16] = {
            0, 1, 0, 1, 2, 3, 2, 3,
            0, 1, 0, 1, 2, 3, 2, 3,
        };
    }

    const char* GetEnvironmentMapEffectVertexShaderNameEXT(int shaderIndex)
    {
        if (shaderIndex < 0 || shaderIndex >= 16) ThrowOutOfRange("GetEnvironmentMapEffectVertexShaderNameEXT", shaderIndex, 16);
        return kEnvironmentMapEffectVSArray[kEnvironmentMapEffectVSIndices[shaderIndex]];
    }

    const char* GetEnvironmentMapEffectPixelShaderNameEXT(int shaderIndex)
    {
        if (shaderIndex < 0 || shaderIndex >= 16) ThrowOutOfRange("GetEnvironmentMapEffectPixelShaderNameEXT", shaderIndex, 16);
        return kEnvironmentMapEffectPSArray[kEnvironmentMapEffectPSIndices[shaderIndex]];
    }

    // =============================================================================================
    // SkinnedEffect -- SkinnedEffect.cs OnApply() / SkinnedEffect.fx VSArray[9]/VSIndices[18]/
    // PSArray[3]/PSIndices[18]
    // =============================================================================================

    int ComputeSkinnedEffectShaderIndex(bool fogEnabled, int weightsPerVertex,
                                        bool preferPerPixelLighting, bool oneLight)
    {
        int shaderIndex = 0;
        if (!fogEnabled) shaderIndex += 1;
        if (weightsPerVertex == 2) shaderIndex += 2;
        else if (weightsPerVertex == 4) shaderIndex += 4;
        if (preferPerPixelLighting) shaderIndex += 12;
        else if (oneLight) shaderIndex += 6;
        return shaderIndex;
    }

    namespace
    {
        constexpr const char* kSkinnedEffectVSArray[9] = {
            "SkinnedEffect_VSSkinnedVertexLightingOneBone", "SkinnedEffect_VSSkinnedVertexLightingTwoBones",
            "SkinnedEffect_VSSkinnedVertexLightingFourBones",
            "SkinnedEffect_VSSkinnedOneLightOneBone", "SkinnedEffect_VSSkinnedOneLightTwoBones",
            "SkinnedEffect_VSSkinnedOneLightFourBones",
            "SkinnedEffect_VSSkinnedPixelLightingOneBone", "SkinnedEffect_VSSkinnedPixelLightingTwoBones",
            "SkinnedEffect_VSSkinnedPixelLightingFourBones",
        };
        constexpr int kSkinnedEffectVSIndices[18] = {
            0, 0, 1, 1, 2, 2,
            3, 3, 4, 4, 5, 5,
            6, 6, 7, 7, 8, 8,
        };
        constexpr const char* kSkinnedEffectPSArray[3] = {
            "SkinnedEffect_PSSkinnedVertexLighting", "SkinnedEffect_PSSkinnedVertexLightingNoFog",
            "SkinnedEffect_PSSkinnedPixelLighting",
        };
        constexpr int kSkinnedEffectPSIndices[18] = {
            0, 1, 0, 1, 0, 1,
            0, 1, 0, 1, 0, 1,
            2, 2, 2, 2, 2, 2,
        };
    }

    const char* GetSkinnedEffectVertexShaderNameEXT(int shaderIndex)
    {
        if (shaderIndex < 0 || shaderIndex >= 18) ThrowOutOfRange("GetSkinnedEffectVertexShaderNameEXT", shaderIndex, 18);
        return kSkinnedEffectVSArray[kSkinnedEffectVSIndices[shaderIndex]];
    }

    const char* GetSkinnedEffectPixelShaderNameEXT(int shaderIndex)
    {
        if (shaderIndex < 0 || shaderIndex >= 18) ThrowOutOfRange("GetSkinnedEffectPixelShaderNameEXT", shaderIndex, 18);
        return kSkinnedEffectPSArray[kSkinnedEffectPSIndices[shaderIndex]];
    }
}
