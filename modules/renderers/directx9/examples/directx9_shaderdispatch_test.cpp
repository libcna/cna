// SPDX-License-Identifier: MS-PL
// plans/plan_dx9.md Phase D9-8 (D9-80): unit checks for D3D9ShaderDispatch's transcribed
// ShaderIndex/VSIndices/PSIndices tables. No device/window/GPU needed -- these are pure
// functions, verified directly against the vendored .fx files' own row comments and the FNA
// .cs sources' own ShaderIndex formulas (both cited in D3D9ShaderDispatch.cpp).
//
// Each effect gets an EXHAUSTIVE, exact-match sweep over every valid shaderIndex, against a
// SECOND, independently-typed expected-name array in this file (not merely a prefix check --
// a prefix-only sweep was tried first and mutation-tested; it missed a deliberately-corrupted
// single VSIndices entry because the wrong-but-still-real name it produced also matched the
// effect's own prefix. Every entry below is checked for an EXACT string match instead), plus a
// few hand-computed `ShaderIndex` formula checks cross-referenced against the .fx file's own row
// comments, and an out-of-range check on both sides of the valid range.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "CNA/Internal/Renderers/DirectX9/D3D9ShaderDispatch.hpp"

#include <cstdio>
#include <cstring>
#include <stdexcept>

using namespace CNA::Internal::Renderers::DirectX9;

namespace
{
    int passCount = 0;
    int totalCount = 0;

    void check(bool ok, const char* label)
    {
        ++totalCount;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount;
    }

    template <typename Fn>
    bool ThrowsOutOfRange(Fn&& fn)
    {
        try { fn(); return false; }
        catch (const std::out_of_range&) { return true; }
    }

    /// Runs an exhaustive, exact-match sweep of `getVs`/`getPs` against two independently-typed
    /// expected-name arrays of length `count`. Returns true only if every one of the 2*count
    /// entries matches exactly; prints the first mismatch found (if any) for debuggability.
    template <typename GetVs, typename GetPs>
    bool ExhaustiveExactSweep(const char* effectLabel, int count,
                              GetVs&& getVs, GetPs&& getPs,
                              const char* const* expectedVs, const char* const* expectedPs)
    {
        for (int i = 0; i < count; ++i)
        {
            const char* actualVs = getVs(i);
            const char* actualPs = getPs(i);
            if (std::strcmp(actualVs, expectedVs[i]) != 0)
            {
                std::printf("    MISMATCH %s VS shaderIndex=%d: got \"%s\", expected \"%s\"\n",
                           effectLabel, i, actualVs, expectedVs[i]);
                return false;
            }
            if (std::strcmp(actualPs, expectedPs[i]) != 0)
            {
                std::printf("    MISMATCH %s PS shaderIndex=%d: got \"%s\", expected \"%s\"\n",
                           effectLabel, i, actualPs, expectedPs[i]);
                return false;
            }
        }
        return true;
    }
}

int main()
{
    // ---- BasicEffect (32 shaderIndex values, .fx VSIndices[32]/PSIndices[32]) ----
    {
        // shaderIndex 0: fog on, no vertex color, no texture, no lighting -- "basic".
        check(ComputeBasicEffectShaderIndex(true, false, false, false, false, false) == 0,
              "BasicEffect: fog+novc+notex+nolight -> shaderIndex 0 (\"basic\")");
        // shaderIndex 31: no fog, vertex color, texture, pixel lighting -- the .fx file's own last row.
        check(ComputeBasicEffectShaderIndex(false, true, true, true, true, false) == 31,
              "BasicEffect: nofog+vc+tex+pixellight -> shaderIndex 31 (last VSIndices/PSIndices row)");
        // shaderIndex 17: no fog, no vc, no tex, one-light lighting -- proves oneLight branch (+16).
        check(ComputeBasicEffectShaderIndex(false, false, false, true, false, true) == 17,
              "BasicEffect: nofog+onelight -> shaderIndex 17 (1 for nofog + 16 for oneLight)");

        // Independently transcribed from BasicEffect.fx's own VSIndices[32]/PSIndices[32] row
        // comments -- a second, separate typing of the same source, not a copy of the production
        // arrays, so a transcription error in either place is caught by a mismatch, not masked.
        static const char* kExpectedVs[32] = {
            "BasicEffect_VSBasic", "BasicEffect_VSBasicNoFog",
            "BasicEffect_VSBasicVc", "BasicEffect_VSBasicVcNoFog",
            "BasicEffect_VSBasicTx", "BasicEffect_VSBasicTxNoFog",
            "BasicEffect_VSBasicTxVc", "BasicEffect_VSBasicTxVcNoFog",
            "BasicEffect_VSBasicVertexLighting", "BasicEffect_VSBasicVertexLighting",
            "BasicEffect_VSBasicVertexLightingVc", "BasicEffect_VSBasicVertexLightingVc",
            "BasicEffect_VSBasicVertexLightingTx", "BasicEffect_VSBasicVertexLightingTx",
            "BasicEffect_VSBasicVertexLightingTxVc", "BasicEffect_VSBasicVertexLightingTxVc",
            "BasicEffect_VSBasicOneLight", "BasicEffect_VSBasicOneLight",
            "BasicEffect_VSBasicOneLightVc", "BasicEffect_VSBasicOneLightVc",
            "BasicEffect_VSBasicOneLightTx", "BasicEffect_VSBasicOneLightTx",
            "BasicEffect_VSBasicOneLightTxVc", "BasicEffect_VSBasicOneLightTxVc",
            "BasicEffect_VSBasicPixelLighting", "BasicEffect_VSBasicPixelLighting",
            "BasicEffect_VSBasicPixelLightingVc", "BasicEffect_VSBasicPixelLightingVc",
            "BasicEffect_VSBasicPixelLightingTx", "BasicEffect_VSBasicPixelLightingTx",
            "BasicEffect_VSBasicPixelLightingTxVc", "BasicEffect_VSBasicPixelLightingTxVc",
        };
        static const char* kExpectedPs[32] = {
            "BasicEffect_PSBasic", "BasicEffect_PSBasicNoFog",
            "BasicEffect_PSBasic", "BasicEffect_PSBasicNoFog",
            "BasicEffect_PSBasicTx", "BasicEffect_PSBasicTxNoFog",
            "BasicEffect_PSBasicTx", "BasicEffect_PSBasicTxNoFog",
            "BasicEffect_PSBasicVertexLighting", "BasicEffect_PSBasicVertexLightingNoFog",
            "BasicEffect_PSBasicVertexLighting", "BasicEffect_PSBasicVertexLightingNoFog",
            "BasicEffect_PSBasicVertexLightingTx", "BasicEffect_PSBasicVertexLightingTxNoFog",
            "BasicEffect_PSBasicVertexLightingTx", "BasicEffect_PSBasicVertexLightingTxNoFog",
            "BasicEffect_PSBasicVertexLighting", "BasicEffect_PSBasicVertexLightingNoFog",
            "BasicEffect_PSBasicVertexLighting", "BasicEffect_PSBasicVertexLightingNoFog",
            "BasicEffect_PSBasicVertexLightingTx", "BasicEffect_PSBasicVertexLightingTxNoFog",
            "BasicEffect_PSBasicVertexLightingTx", "BasicEffect_PSBasicVertexLightingTxNoFog",
            "BasicEffect_PSBasicPixelLighting", "BasicEffect_PSBasicPixelLighting",
            "BasicEffect_PSBasicPixelLighting", "BasicEffect_PSBasicPixelLighting",
            "BasicEffect_PSBasicPixelLightingTx", "BasicEffect_PSBasicPixelLightingTx",
            "BasicEffect_PSBasicPixelLightingTx", "BasicEffect_PSBasicPixelLightingTx",
        };
        check(ExhaustiveExactSweep("BasicEffect", 32, GetBasicEffectVertexShaderNameEXT,
                                   GetBasicEffectPixelShaderNameEXT, kExpectedVs, kExpectedPs),
              "BasicEffect: exhaustive exact-match sweep -- all 32 shaderIndex values resolve to "
              "the exact independently-transcribed name");

        check(ThrowsOutOfRange([] { (void)GetBasicEffectVertexShaderNameEXT(-1); }) &&
              ThrowsOutOfRange([] { (void)GetBasicEffectVertexShaderNameEXT(32); }) &&
              ThrowsOutOfRange([] { (void)GetBasicEffectPixelShaderNameEXT(-1); }) &&
              ThrowsOutOfRange([] { (void)GetBasicEffectPixelShaderNameEXT(32); }),
              "BasicEffect: out-of-range shaderIndex (-1 and 32) throws std::out_of_range on both stages");
    }

    // ---- AlphaTestEffect (8 shaderIndex values, .fx VSIndices[8]/PSIndices[8]) ----
    {
        check(ComputeAlphaTestEffectShaderIndex(true, false, false) == 0,
              "AlphaTestEffect: fog+novc+ltgt -> shaderIndex 0 (\"lt/gt\")");
        // isEqNe true selects the Eq/Ne pixel shader half of the table (+4).
        check(ComputeAlphaTestEffectShaderIndex(true, false, true) == 4,
              "AlphaTestEffect: fog+novc+eqne -> shaderIndex 4 (\"eq/ne\")");

        static const char* kExpectedVs[8] = {
            "AlphaTestEffect_VSAlphaTest", "AlphaTestEffect_VSAlphaTestNoFog",
            "AlphaTestEffect_VSAlphaTestVc", "AlphaTestEffect_VSAlphaTestVcNoFog",
            "AlphaTestEffect_VSAlphaTest", "AlphaTestEffect_VSAlphaTestNoFog",
            "AlphaTestEffect_VSAlphaTestVc", "AlphaTestEffect_VSAlphaTestVcNoFog",
        };
        static const char* kExpectedPs[8] = {
            "AlphaTestEffect_PSAlphaTestLtGt", "AlphaTestEffect_PSAlphaTestLtGtNoFog",
            "AlphaTestEffect_PSAlphaTestLtGt", "AlphaTestEffect_PSAlphaTestLtGtNoFog",
            "AlphaTestEffect_PSAlphaTestEqNe", "AlphaTestEffect_PSAlphaTestEqNeNoFog",
            "AlphaTestEffect_PSAlphaTestEqNe", "AlphaTestEffect_PSAlphaTestEqNeNoFog",
        };
        check(ExhaustiveExactSweep("AlphaTestEffect", 8, GetAlphaTestEffectVertexShaderNameEXT,
                                   GetAlphaTestEffectPixelShaderNameEXT, kExpectedVs, kExpectedPs),
              "AlphaTestEffect: exhaustive exact-match sweep -- all 8 shaderIndex values resolve "
              "to the exact independently-transcribed name");

        check(ThrowsOutOfRange([] { (void)GetAlphaTestEffectVertexShaderNameEXT(8); }) &&
              ThrowsOutOfRange([] { (void)GetAlphaTestEffectPixelShaderNameEXT(-1); }),
              "AlphaTestEffect: out-of-range shaderIndex throws std::out_of_range");
    }

    // ---- DualTextureEffect (4 shaderIndex values, VSArray indexed directly, PSIndices[4]) ----
    {
        check(ComputeDualTextureEffectShaderIndex(true, false) == 0,
              "DualTextureEffect: fog+novc -> shaderIndex 0 (\"basic\")");
        check(ComputeDualTextureEffectShaderIndex(false, true) == 3,
              "DualTextureEffect: nofog+vc -> shaderIndex 3 (\"vertex color, no fog\")");

        static const char* kExpectedVs[4] = {
            "DualTextureEffect_VSDualTexture", "DualTextureEffect_VSDualTextureNoFog",
            "DualTextureEffect_VSDualTextureVc", "DualTextureEffect_VSDualTextureVcNoFog",
        };
        static const char* kExpectedPs[4] = {
            "DualTextureEffect_PSDualTexture", "DualTextureEffect_PSDualTextureNoFog",
            "DualTextureEffect_PSDualTexture", "DualTextureEffect_PSDualTextureNoFog",
        };
        check(ExhaustiveExactSweep("DualTextureEffect", 4, GetDualTextureEffectVertexShaderNameEXT,
                                   GetDualTextureEffectPixelShaderNameEXT, kExpectedVs, kExpectedPs),
              "DualTextureEffect: exhaustive exact-match sweep -- all 4 shaderIndex values resolve "
              "to the exact independently-transcribed name");

        check(ThrowsOutOfRange([] { (void)GetDualTextureEffectVertexShaderNameEXT(4); }) &&
              ThrowsOutOfRange([] { (void)GetDualTextureEffectPixelShaderNameEXT(4); }),
              "DualTextureEffect: out-of-range shaderIndex throws std::out_of_range");
    }

    // ---- EnvironmentMapEffect (16 shaderIndex values, .fx VSIndices[16]/PSIndices[16]) ----
    {
        check(ComputeEnvironmentMapEffectShaderIndex(true, false, false, false) == 0,
              "EnvironmentMapEffect: fog+nofresnel+nospec+notonelight -> shaderIndex 0 (\"basic\")");
        // fresnel + specular, no fog -- .fx row comment "// fresnel + specular, no fog" == index 7.
        check(ComputeEnvironmentMapEffectShaderIndex(false, true, true, false) == 7,
              "EnvironmentMapEffect: nofog+fresnel+specular -> shaderIndex 7");
        // one light, fresnel + specular, no fog -- the .fx file's own last row (index 15).
        check(ComputeEnvironmentMapEffectShaderIndex(false, true, true, true) == 15,
              "EnvironmentMapEffect: nofog+fresnel+specular+onelight -> shaderIndex 15 (last row)");

        static const char* kExpectedVs[16] = {
            "EnvironmentMapEffect_VSEnvMap", "EnvironmentMapEffect_VSEnvMap",
            "EnvironmentMapEffect_VSEnvMapFresnel", "EnvironmentMapEffect_VSEnvMapFresnel",
            "EnvironmentMapEffect_VSEnvMap", "EnvironmentMapEffect_VSEnvMap",
            "EnvironmentMapEffect_VSEnvMapFresnel", "EnvironmentMapEffect_VSEnvMapFresnel",
            "EnvironmentMapEffect_VSEnvMapOneLight", "EnvironmentMapEffect_VSEnvMapOneLight",
            "EnvironmentMapEffect_VSEnvMapOneLightFresnel", "EnvironmentMapEffect_VSEnvMapOneLightFresnel",
            "EnvironmentMapEffect_VSEnvMapOneLight", "EnvironmentMapEffect_VSEnvMapOneLight",
            "EnvironmentMapEffect_VSEnvMapOneLightFresnel", "EnvironmentMapEffect_VSEnvMapOneLightFresnel",
        };
        static const char* kExpectedPs[16] = {
            "EnvironmentMapEffect_PSEnvMap", "EnvironmentMapEffect_PSEnvMapNoFog",
            "EnvironmentMapEffect_PSEnvMap", "EnvironmentMapEffect_PSEnvMapNoFog",
            "EnvironmentMapEffect_PSEnvMapSpecular", "EnvironmentMapEffect_PSEnvMapSpecularNoFog",
            "EnvironmentMapEffect_PSEnvMapSpecular", "EnvironmentMapEffect_PSEnvMapSpecularNoFog",
            "EnvironmentMapEffect_PSEnvMap", "EnvironmentMapEffect_PSEnvMapNoFog",
            "EnvironmentMapEffect_PSEnvMap", "EnvironmentMapEffect_PSEnvMapNoFog",
            "EnvironmentMapEffect_PSEnvMapSpecular", "EnvironmentMapEffect_PSEnvMapSpecularNoFog",
            "EnvironmentMapEffect_PSEnvMapSpecular", "EnvironmentMapEffect_PSEnvMapSpecularNoFog",
        };
        check(ExhaustiveExactSweep("EnvironmentMapEffect", 16, GetEnvironmentMapEffectVertexShaderNameEXT,
                                   GetEnvironmentMapEffectPixelShaderNameEXT, kExpectedVs, kExpectedPs),
              "EnvironmentMapEffect: exhaustive exact-match sweep -- all 16 shaderIndex values "
              "resolve to the exact independently-transcribed name");

        check(ThrowsOutOfRange([] { (void)GetEnvironmentMapEffectVertexShaderNameEXT(16); }) &&
              ThrowsOutOfRange([] { (void)GetEnvironmentMapEffectPixelShaderNameEXT(-1); }),
              "EnvironmentMapEffect: out-of-range shaderIndex throws std::out_of_range");
    }

    // ---- SkinnedEffect (18 shaderIndex values, .fx VSIndices[18]/PSIndices[18]) ----
    {
        check(ComputeSkinnedEffectShaderIndex(true, 1, false, false) == 0,
              "SkinnedEffect: fog+1weight+vertexlight -> shaderIndex 0 (\"vertex lighting, one bone\")");
        check(ComputeSkinnedEffectShaderIndex(true, 4, false, false) == 4,
              "SkinnedEffect: fog+4weights+vertexlight -> shaderIndex 4 (\"vertex lighting, four bones\")");
        // pixel lighting, four bones, no fog -- the .fx file's own last row (index 17).
        check(ComputeSkinnedEffectShaderIndex(false, 4, true, false) == 17,
              "SkinnedEffect: nofog+4weights+pixellight -> shaderIndex 17 (last row)");

        static const char* kExpectedVs[18] = {
            "SkinnedEffect_VSSkinnedVertexLightingOneBone", "SkinnedEffect_VSSkinnedVertexLightingOneBone",
            "SkinnedEffect_VSSkinnedVertexLightingTwoBones", "SkinnedEffect_VSSkinnedVertexLightingTwoBones",
            "SkinnedEffect_VSSkinnedVertexLightingFourBones", "SkinnedEffect_VSSkinnedVertexLightingFourBones",
            "SkinnedEffect_VSSkinnedOneLightOneBone", "SkinnedEffect_VSSkinnedOneLightOneBone",
            "SkinnedEffect_VSSkinnedOneLightTwoBones", "SkinnedEffect_VSSkinnedOneLightTwoBones",
            "SkinnedEffect_VSSkinnedOneLightFourBones", "SkinnedEffect_VSSkinnedOneLightFourBones",
            "SkinnedEffect_VSSkinnedPixelLightingOneBone", "SkinnedEffect_VSSkinnedPixelLightingOneBone",
            "SkinnedEffect_VSSkinnedPixelLightingTwoBones", "SkinnedEffect_VSSkinnedPixelLightingTwoBones",
            "SkinnedEffect_VSSkinnedPixelLightingFourBones", "SkinnedEffect_VSSkinnedPixelLightingFourBones",
        };
        static const char* kExpectedPs[18] = {
            "SkinnedEffect_PSSkinnedVertexLighting", "SkinnedEffect_PSSkinnedVertexLightingNoFog",
            "SkinnedEffect_PSSkinnedVertexLighting", "SkinnedEffect_PSSkinnedVertexLightingNoFog",
            "SkinnedEffect_PSSkinnedVertexLighting", "SkinnedEffect_PSSkinnedVertexLightingNoFog",
            "SkinnedEffect_PSSkinnedVertexLighting", "SkinnedEffect_PSSkinnedVertexLightingNoFog",
            "SkinnedEffect_PSSkinnedVertexLighting", "SkinnedEffect_PSSkinnedVertexLightingNoFog",
            "SkinnedEffect_PSSkinnedVertexLighting", "SkinnedEffect_PSSkinnedVertexLightingNoFog",
            "SkinnedEffect_PSSkinnedPixelLighting", "SkinnedEffect_PSSkinnedPixelLighting",
            "SkinnedEffect_PSSkinnedPixelLighting", "SkinnedEffect_PSSkinnedPixelLighting",
            "SkinnedEffect_PSSkinnedPixelLighting", "SkinnedEffect_PSSkinnedPixelLighting",
        };
        check(ExhaustiveExactSweep("SkinnedEffect", 18, GetSkinnedEffectVertexShaderNameEXT,
                                   GetSkinnedEffectPixelShaderNameEXT, kExpectedVs, kExpectedPs),
              "SkinnedEffect: exhaustive exact-match sweep -- all 18 shaderIndex values resolve "
              "to the exact independently-transcribed name");

        check(ThrowsOutOfRange([] { (void)GetSkinnedEffectVertexShaderNameEXT(18); }) &&
              ThrowsOutOfRange([] { (void)GetSkinnedEffectPixelShaderNameEXT(-1); }),
              "SkinnedEffect: out-of-range shaderIndex throws std::out_of_range");
    }

    std::printf("=== %d/%d PASS ===\n", passCount, totalCount);
    return (passCount == totalCount) ? 0 : 1;
}
