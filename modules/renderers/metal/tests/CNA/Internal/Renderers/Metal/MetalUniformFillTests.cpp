// SPDX-License-Identifier: MS-PL
//
// plan_metal.md METAL-34-style extraction: FillMetalLitUniforms/FillMetalEnvUniforms/
// FillMetalSkinnedUniforms/FillMetalPbrUniforms/FillMetalSkinnedPbrUniforms only read GpuDrawParams
// and a MetalMat4 (plain C++, zero Objective-C dependency) and write plain float-array uniform
// structs -- the connective tissue between per-draw XNA-effect state and the actual bytes memcpy'd
// into a real MTLBuffer. With dozens of individual field assignments per function, the single
// biggest real risk is a copy-paste mistake (e.g. `light1Diffuse` accidentally reading
// `params.light2Diffuse`, or a light's Specular landing in another light's slot) -- exactly the kind
// of bug that compiles cleanly, never crashes, and only ever shows up as subtly wrong lighting on
// real hardware. No #if defined(CNA_RENDERER_METAL) gate, deliberately.
//
// Every GpuDrawParams field relevant to a given fill function is set to its own distinct numeric
// value in MakeDistinctParams() below, and every expected output value is re-derived independently
// here (by reading GpuDrawParams' field list and each Fill function's real field mapping directly,
// not by copying the implementation) -- so a field landing in the wrong uniform slot, or reading the
// wrong GpuDrawParams field, produces a visibly wrong number rather than passing by coincidence.
#include <gtest/gtest.h>
#include "CNA/Internal/Renderers/Metal/MetalUniformFill.hpp"
#include <cstring>

using namespace CNA::Internal::Renderers::Metal;
using CNA::Internal::Renderers::GpuDrawParams;

namespace
{
    constexpr float kEps = 1e-5f;

    void ExpectVec4Eq(const float* got, float x, float y, float z, float w)
    {
        EXPECT_NEAR(got[0], x, kEps);
        EXPECT_NEAR(got[1], y, kEps);
        EXPECT_NEAR(got[2], z, kEps);
        EXPECT_NEAR(got[3], w, kEps);
    }

    MetalMat4 MakeDistinctWvp()
    {
        MetalMat4 m;
        for (int i = 0; i < 16; ++i) m.m[i] = 100.0f + (float)i;
        return m;
    }

    // Every field this test suite exercises gets its own distinct value so a wrong-field or
    // wrong-light-index mapping bug produces a visibly wrong number, not a coincidental match.
    // worldColMajor is a genuinely invertible, non-uniform-scale + translation matrix (not identity)
    // specifically so the normal-matrix cross-validation below actually exercises the real wiring
    // rather than trivially matching for any (even wrong) input buffer.
    GpuDrawParams MakeDistinctParams()
    {
        GpuDrawParams p;
        p.diffuseColor[0]=1; p.diffuseColor[1]=2; p.diffuseColor[2]=3; p.diffuseColor[3]=4;
        p.ambientColor[0]=5; p.ambientColor[1]=6; p.ambientColor[2]=7;
        p.light0Dir[0]=8; p.light0Dir[1]=9; p.light0Dir[2]=10;
        p.light0Diffuse[0]=11; p.light0Diffuse[1]=12; p.light0Diffuse[2]=13;
        p.light1Dir[0]=14; p.light1Dir[1]=15; p.light1Dir[2]=16;
        p.light1Diffuse[0]=17; p.light1Diffuse[1]=18; p.light1Diffuse[2]=19;
        p.light2Dir[0]=20; p.light2Dir[1]=21; p.light2Dir[2]=22;
        p.light2Diffuse[0]=23; p.light2Diffuse[1]=24; p.light2Diffuse[2]=25;
        const float world[16] = {2,0,0,0, 0,3,0,0, 0,0,4,0, 10,20,30,1};
        std::memcpy(p.worldColMajor, world, sizeof(world));
        p.alphaTest[0]=26; p.alphaTest[1]=27; p.alphaTest[2]=28; p.alphaTest[3]=29;
        p.emissiveColor[0]=30; p.emissiveColor[1]=31; p.emissiveColor[2]=32;
        p.envMapSpecular[0]=33; p.envMapSpecular[1]=34; p.envMapSpecular[2]=35;
        p.eyePositionWorld[0]=36; p.eyePositionWorld[1]=37; p.eyePositionWorld[2]=38;
        p.light0Specular[0]=39; p.light0Specular[1]=40; p.light0Specular[2]=41;
        p.light1Specular[0]=42; p.light1Specular[1]=43; p.light1Specular[2]=44;
        p.light2Specular[0]=45; p.light2Specular[1]=46; p.light2Specular[2]=47;
        p.specularColor[0]=48; p.specularColor[1]=49; p.specularColor[2]=50;
        p.specularPower = 51;
        p.envMapAmount = 52;
        p.fresnelEnabled = true;
        p.fresnelFactor = 53;
        p.weightsPerVertex = 2;
        p.fogEnabled = true;
        p.fogColor[0]=54; p.fogColor[1]=55; p.fogColor[2]=56;
        p.fogVector[0]=57; p.fogVector[1]=58; p.fogVector[2]=59; p.fogVector[3]=60;
        p.vertexColorEnabled = true;
        p.pbrMetallicFactor = 61;
        p.pbrRoughnessFactor = 62;
        p.pbrNormalScale = 63;
        p.pbrOcclusionStrength = 64;
        p.pbrBaseColorTextureIsSrgb = false;
        p.pbrEmissiveTextureIsSrgb = true;
        p.pbrEncodeOutputToSrgb = false;
        p.pbrDielectricF0[0] = 65;
        p.pbrDielectricF0[1] = 66;
        p.pbrDielectricF0[2] = 67;
        p.pbrDielectricF90 = 68;
        for (int row = 0; row < 10; ++row)
            for (int component = 0; component < 4; ++component)
                p.pbrTextureTransformRows[row][component] =
                    static_cast<float>(69 + row * 4 + component);
        p.lightingEnabled = true;
        return p;
    }

    void ExpectWvpAndWorldCopiedVerbatim(const float* tWvp, const float* tWorld, const MetalMat4& wvp, const GpuDrawParams& p)
    {
        for (int i = 0; i < 16; ++i) EXPECT_NEAR(tWvp[i], wvp.m[i], kEps) << "wvp[" << i << "]";
        for (int i = 0; i < 16; ++i) EXPECT_NEAR(tWorld[i], p.worldColMajor[i], kEps) << "world[" << i << "]";
    }

    // Cross-validates the transform's normalCol0/1/2 against a direct, independent call to the
    // already-tested ComputeMetalNormalMatrixCols with the SAME worldColMajor buffer -- proves the
    // fill function actually forwards params.worldColMajor (not some other/uninitialized buffer) and
    // writes the result into the right 3 fields, without re-deriving the cofactor math by hand again.
    void ExpectNormalColsMatchDirectComputation(const float* col0, const float* col1, const float* col2, const GpuDrawParams& p)
    {
        float ec0[4], ec1[4], ec2[4];
        ComputeMetalNormalMatrixCols(p.worldColMajor, ec0, ec1, ec2);
        for (int i = 0; i < 4; ++i) EXPECT_NEAR(col0[i], ec0[i], kEps) << "normalCol0[" << i << "]";
        for (int i = 0; i < 4; ++i) EXPECT_NEAR(col1[i], ec1[i], kEps) << "normalCol1[" << i << "]";
        for (int i = 0; i < 4; ++i) EXPECT_NEAR(col2[i], ec2[i], kEps) << "normalCol2[" << i << "]";
    }
}

TEST(MetalUniformFill, LitUniformsMapEveryFieldCorrectly)
{
    GpuDrawParams p = MakeDistinctParams();
    MetalMat4 wvp = MakeDistinctWvp();
    MetalLitTransform t{};
    MetalLitUniforms lu{};
    FillMetalLitUniforms(t, lu, wvp, p);

    ExpectWvpAndWorldCopiedVerbatim(t.wvp, t.world, wvp, p);
    ExpectNormalColsMatchDirectComputation(t.normalCol0, t.normalCol1, t.normalCol2, p);

    ExpectVec4Eq(lu.diffuseColor, 1,2,3,4);
    ExpectVec4Eq(lu.ambientColor, 5,6,7,0);
    ExpectVec4Eq(lu.light0Dir, 8,9,10,0);
    ExpectVec4Eq(lu.light0Diffuse, 11,12,13,0);
    ExpectVec4Eq(lu.light0Specular, 39,40,41,0);
    ExpectVec4Eq(lu.light1Dir, 14,15,16,0);
    ExpectVec4Eq(lu.light1Diffuse, 17,18,19,0);
    ExpectVec4Eq(lu.light1Specular, 42,43,44,0);
    ExpectVec4Eq(lu.light2Dir, 20,21,22,0);
    ExpectVec4Eq(lu.light2Diffuse, 23,24,25,0);
    ExpectVec4Eq(lu.light2Specular, 45,46,47,0);
    ExpectVec4Eq(lu.specularColorPower, 48,49,50,51);
    ExpectVec4Eq(lu.eyePosition, 36,37,38,0);
    ExpectVec4Eq(lu.emissiveColor, 30,31,32,0);
    ExpectVec4Eq(lu.alphaTest, 26,27,28,29);
    ExpectVec4Eq(lu.fogColorEnabled, 54,55,56,1);
    ExpectVec4Eq(lu.fogVector, 57,58,59,60);
}

TEST(MetalUniformFill, LitUniformsFogDisabledZeroesFogEnabledFlagOnly)
{
    GpuDrawParams p = MakeDistinctParams();
    p.fogEnabled = false;
    MetalLitTransform t{};
    MetalLitUniforms lu{};
    FillMetalLitUniforms(t, lu, MakeDistinctWvp(), p);
    // fogColor xyz still copied even when disabled -- only the w flag changes.
    ExpectVec4Eq(lu.fogColorEnabled, 54,55,56,0);
}

TEST(MetalUniformFill, LightingDisabledNormalizesBasicLitUniformsLikeEasyGL)
{
    GpuDrawParams p = MakeDistinctParams();
    p.lightingEnabled = false;
    MetalLitTransform transform{};
    MetalLitUniforms uniforms{};
    FillMetalLitUniforms(transform, uniforms, MakeDistinctWvp(), p);

    ExpectVec4Eq(uniforms.ambientColor, 1,1,1,0);
    ExpectVec4Eq(uniforms.light0Dir, 0,-1,0,0);
    ExpectVec4Eq(uniforms.light1Dir, 0,-1,0,0);
    ExpectVec4Eq(uniforms.light2Dir, 0,-1,0,0);
    ExpectVec4Eq(uniforms.light0Diffuse, 0,0,0,0);
    ExpectVec4Eq(uniforms.light1Diffuse, 0,0,0,0);
    ExpectVec4Eq(uniforms.light2Diffuse, 0,0,0,0);
    ExpectVec4Eq(uniforms.light0Specular, 0,0,0,0);
    ExpectVec4Eq(uniforms.light1Specular, 0,0,0,0);
    ExpectVec4Eq(uniforms.light2Specular, 0,0,0,0);
}

TEST(MetalUniformFill, EnvUniformsMapEveryFieldCorrectly)
{
    GpuDrawParams p = MakeDistinctParams();
    MetalMat4 wvp = MakeDistinctWvp();
    MetalEnvTransform t{};
    MetalEnvUniforms eu{};
    FillMetalEnvUniforms(t, eu, wvp, p);

    ExpectWvpAndWorldCopiedVerbatim(t.wvp, t.world, wvp, p);
    ExpectNormalColsMatchDirectComputation(t.normalCol0, t.normalCol1, t.normalCol2, p);

    ExpectVec4Eq(eu.diffuseColor, 1,2,3,4);
    ExpectVec4Eq(eu.emissiveColor, 30,31,32,0);
    ExpectVec4Eq(eu.light0Dir, 8,9,10,0);
    ExpectVec4Eq(eu.light0Diffuse, 11,12,13,0);
    ExpectVec4Eq(eu.light1Dir, 14,15,16,0);
    ExpectVec4Eq(eu.light1Diffuse, 17,18,19,0);
    ExpectVec4Eq(eu.light2Dir, 20,21,22,0);
    ExpectVec4Eq(eu.light2Diffuse, 23,24,25,0);
    ExpectVec4Eq(eu.envMapSpecular, 33,34,35,0);
    ExpectVec4Eq(eu.eyePosition, 36,37,38,0);
    ExpectVec4Eq(eu.envParams, 52,1,53,0);
    ExpectVec4Eq(eu.alphaTest, 26,27,28,29);
    ExpectVec4Eq(eu.fogColorEnabled, 54,55,56,1);
    ExpectVec4Eq(eu.fogVector, 57,58,59,60);
}

TEST(MetalUniformFill, EnvUniformsFresnelDisabledZeroesFresnelFlagOnly)
{
    GpuDrawParams p = MakeDistinctParams();
    p.fresnelEnabled = false;
    MetalEnvTransform t{};
    MetalEnvUniforms eu{};
    FillMetalEnvUniforms(t, eu, MakeDistinctWvp(), p);
    // envMapAmount and fresnelFactor still copied even when disabled -- only the y flag changes.
    ExpectVec4Eq(eu.envParams, 52,0,53,0);
}

TEST(MetalUniformFill, SkinnedUniformsMapEveryFieldCorrectly)
{
    GpuDrawParams p = MakeDistinctParams();
    MetalMat4 wvp = MakeDistinctWvp();
    MetalSkinnedTransform t{};
    MetalSkinnedUniforms su{};
    FillMetalSkinnedUniforms(t, su, wvp, p);

    ExpectWvpAndWorldCopiedVerbatim(t.wvp, t.world, wvp, p);
    // SkinnedTransform deliberately has no normalCol0/1/2 -- the skinned shader has no
    // world-normal-matrix step at all, so only skinParams is checked here.
    ExpectVec4Eq(t.skinParams, 2,0,0,0);

    ExpectVec4Eq(su.diffuseColor, 1,2,3,4);
    ExpectVec4Eq(su.emissiveColor, 30,31,32,0);
    ExpectVec4Eq(su.light0Dir, 8,9,10,0);
    ExpectVec4Eq(su.light0Diffuse, 11,12,13,0);
    ExpectVec4Eq(su.light0Specular, 39,40,41,0);
    ExpectVec4Eq(su.light1Dir, 14,15,16,0);
    ExpectVec4Eq(su.light1Diffuse, 17,18,19,0);
    ExpectVec4Eq(su.light1Specular, 42,43,44,0);
    ExpectVec4Eq(su.light2Dir, 20,21,22,0);
    ExpectVec4Eq(su.light2Diffuse, 23,24,25,0);
    ExpectVec4Eq(su.light2Specular, 45,46,47,0);
    ExpectVec4Eq(su.specularColorPower, 48,49,50,51);
    ExpectVec4Eq(su.eyePosition, 36,37,38,0);
    ExpectVec4Eq(su.alphaTest, 26,27,28,29);
    ExpectVec4Eq(su.fogColorEnabled, 54,55,56,1);
    ExpectVec4Eq(su.fogVector, 57,58,59,60);
    ExpectVec4Eq(su.vertexColorEnabled, 1,0,0,0);
}

TEST(MetalUniformFill, SkinnedUniformsVertexColorDisabledAndDifferentWeightsPerVertex)
{
    GpuDrawParams p = MakeDistinctParams();
    p.vertexColorEnabled = false;
    p.weightsPerVertex = 1;
    MetalSkinnedTransform t{};
    MetalSkinnedUniforms su{};
    FillMetalSkinnedUniforms(t, su, MakeDistinctWvp(), p);
    ExpectVec4Eq(su.vertexColorEnabled, 0,0,0,0);
    ExpectVec4Eq(t.skinParams, 1,0,0,0);
}

TEST(MetalUniformFill, PbrUniformsMapEveryFieldCorrectly)
{
    GpuDrawParams p = MakeDistinctParams();
    MetalMat4 wvp = MakeDistinctWvp();
    MetalPbrTransform t{};
    MetalPbrUniforms pu{};
    FillMetalPbrUniforms(t, pu, wvp, p);

    ExpectWvpAndWorldCopiedVerbatim(t.wvp, t.world, wvp, p);
    ExpectNormalColsMatchDirectComputation(t.normalCol0, t.normalCol1, t.normalCol2, p);

    ExpectVec4Eq(pu.diffuseColor, 1,2,3,4);
    ExpectVec4Eq(pu.ambientColor, 5,6,7,0);
    ExpectVec4Eq(pu.emissiveColor, 30,31,32,0);
    ExpectVec4Eq(pu.light0Dir, 8,9,10,0);
    ExpectVec4Eq(pu.light0Diffuse, 11,12,13,0);
    ExpectVec4Eq(pu.light1Dir, 14,15,16,0);
    ExpectVec4Eq(pu.light1Diffuse, 17,18,19,0);
    ExpectVec4Eq(pu.light2Dir, 20,21,22,0);
    ExpectVec4Eq(pu.light2Diffuse, 23,24,25,0);
    ExpectVec4Eq(pu.eyePosition, 36,37,38,0);
    ExpectVec4Eq(pu.pbrFactors, 61,62,63,64);
    ExpectVec4Eq(pu.alphaTest, 26,27,28,29);
    ExpectVec4Eq(pu.fogColorEnabled, 54,55,56,1);
    ExpectVec4Eq(pu.fogVector, 57,58,59,60);
    ExpectVec4Eq(pu.srgbFlags, 0,1,0,0);
    ExpectVec4Eq(pu.dielectricFresnel, 65,66,67,68);
    for (int row = 0; row < 10; ++row)
        for (int component = 0; component < 4; ++component)
            EXPECT_NEAR(pu.textureTransformRows[row][component],
                        69 + row * 4 + component, kEps);
}

TEST(MetalUniformFill, SkinnedPbrUniformsDelegatesFragmentFieldsAndFillsOwnTransform)
{
    GpuDrawParams p = MakeDistinctParams();
    MetalMat4 wvp = MakeDistinctWvp();

    // The fragment-side uniforms must come out byte-for-byte identical to plain PbrEffect's own
    // fill, since SkinnedPbrEffect delegates to FillMetalPbrUniforms internally -- proves the
    // delegation itself, not just that PbrUniforms has the right values in isolation.
    MetalPbrTransform referenceT{};
    MetalPbrUniforms referencePu{};
    FillMetalPbrUniforms(referenceT, referencePu, wvp, p);

    MetalSkinnedPbrTransform t{};
    MetalPbrUniforms pu{};
    FillMetalSkinnedPbrUniforms(t, pu, wvp, p);

    for (int i = 0; i < 4; ++i) {
        EXPECT_NEAR(pu.diffuseColor[i], referencePu.diffuseColor[i], kEps);
        EXPECT_NEAR(pu.ambientColor[i], referencePu.ambientColor[i], kEps);
        EXPECT_NEAR(pu.pbrFactors[i], referencePu.pbrFactors[i], kEps);
        EXPECT_NEAR(pu.fogColorEnabled[i], referencePu.fogColorEnabled[i], kEps);
        EXPECT_NEAR(pu.srgbFlags[i], referencePu.srgbFlags[i], kEps);
        EXPECT_NEAR(pu.dielectricFresnel[i], referencePu.dielectricFresnel[i], kEps);
    }
    for (int row = 0; row < 10; ++row)
        for (int component = 0; component < 4; ++component)
            EXPECT_NEAR(pu.textureTransformRows[row][component],
                        referencePu.textureTransformRows[row][component], kEps);

    // SkinnedPbrTransform's own fields: wvp/world and the world inverse-transpose copied from the
    // rigid PBR fill, plus skinParams (which plain PbrTransform has no equivalent of).
    ExpectWvpAndWorldCopiedVerbatim(t.wvp, t.world, wvp, p);
    ExpectVec4Eq(t.normalCol0, referenceT.normalCol0[0], referenceT.normalCol0[1], referenceT.normalCol0[2], referenceT.normalCol0[3]);
    ExpectVec4Eq(t.normalCol1, referenceT.normalCol1[0], referenceT.normalCol1[1], referenceT.normalCol1[2], referenceT.normalCol1[3]);
    ExpectVec4Eq(t.normalCol2, referenceT.normalCol2[0], referenceT.normalCol2[1], referenceT.normalCol2[2], referenceT.normalCol2[3]);
    ExpectVec4Eq(t.skinParams, 2,0,0,0);
}

TEST(MetalUniformFill, LightingDisabledNormalizesPbrAmbientAndDirectionalDiffuse)
{
    GpuDrawParams p = MakeDistinctParams();
    p.lightingEnabled = false;
    MetalPbrTransform transform{};
    MetalPbrUniforms uniforms{};
    FillMetalPbrUniforms(transform, uniforms, MakeDistinctWvp(), p);

    ExpectVec4Eq(uniforms.ambientColor, 1,1,1,0);
    ExpectVec4Eq(uniforms.light0Dir, 0,-1,0,0);
    ExpectVec4Eq(uniforms.light1Dir, 0,-1,0,0);
    ExpectVec4Eq(uniforms.light2Dir, 0,-1,0,0);
    ExpectVec4Eq(uniforms.light0Diffuse, 0,0,0,0);
    ExpectVec4Eq(uniforms.light1Diffuse, 0,0,0,0);
    ExpectVec4Eq(uniforms.light2Diffuse, 0,0,0,0);
}
