// SPDX-License-Identifier: MS-PL
#if defined(CNA_RENDERER_SDLGPU) || defined(CNA_RENDERER_PRESENT_SDLGPU)
#include "CNA/Internal/Renderers/SdlGpu/SdlGpuStateMapping.hpp"
#include <gtest/gtest.h>
#include <limits>

using namespace CNA::Internal::Renderers;
using namespace CNA::Internal::Renderers::SdlGpu::Detail;
using Microsoft::Xna::Framework::Matrix;

namespace {
// XNA uses row vectors: (1,0,0,0)*matrix = (1,2,3,4). GLSL multiplies matrix*column,
// so those four values must occupy its first COLUMN (bytes 0..15). The uniform
// stores the transpose as column-major bytes 1..16. World arrays are already packed.
// Non-symmetric values catch an extra transpose that identity would hide.
const Matrix matrix(1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16);
const std::array<float,16> columns{1,5,9,13,2,6,10,14,3,7,11,15,4,8,12,16};
template<std::size_t N> void Assign(float (&out)[N], std::initializer_list<float> values)
{
    std::copy(values.begin(), values.end(), out);
}
GpuDrawParams Params()
{
    GpuDrawParams p{};
    Assign(p.diffuseColor,{17,18,19,20}); Assign(p.ambientColor,{21,22,23});
    Assign(p.light0Dir,{25,26,27}); Assign(p.light0Diffuse,{29,30,31});
    p.lightingEnabled=true; p.textureEnabled=true; p.vertexColorEnabled=true;
    Assign(p.light1Dir,{41,42,43}); Assign(p.light1Diffuse,{45,46,47});
    Assign(p.light2Dir,{49,50,51}); Assign(p.light2Diffuse,{53,54,55});
    Assign(p.emissiveColor,{57,58,59}); Assign(p.eyePositionWorld,{61,62,63});
    Assign(p.light0Specular,{65,66,67}); Assign(p.light1Specular,{69,70,71}); Assign(p.light2Specular,{73,74,75});
    Assign(p.specularColor,{77,78,79}); p.specularPower=80;
    std::copy(columns.begin(),columns.end(),p.worldColMajor);
    Assign(p.fogColor,{81,82,83}); Assign(p.fogVector,{85,86,87,88}); p.fogEnabled=true;
    Assign(p.alphaTest,{89,90,91,92}); p.envMapAmount=93; p.fresnelEnabled=true;
    p.fresnelFactor=94; Assign(p.envMapSpecular,{95,96,97}); p.weightsPerVertex=2;
    return p;
}
template<std::size_t N> std::array<float,N> Poison()
{
    std::array<float,N> out; out.fill(std::numeric_limits<float>::quiet_NaN()); return out;
}
template<std::size_t N> void ExpectBlock(const std::array<float,N>& actual, const std::array<float,N>& expected)
{
    for (std::size_t i=0; i<N; ++i) EXPECT_FLOAT_EQ(actual[i],expected[i]) << "byte offset " << 4*i;
}
}

TEST(SdlGpuUniformPacking, ColoredAndExtendedPrimaryBlocks)
{
    // Shader oracle: colored3d.vert.glsl PC = mat4 + vec4 + (vec3,float)*3, 128 bytes.
    auto colored=Poison<32>(); FillColoredUniforms(colored,matrix);
    std::array<float,32> expected{1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,
        1,1,1,1, 0,0,0,0, 0,0,0,0, 0,0,0,1};
    ExpectBlock(colored,expected);
    auto p=Params(); auto extended=Poison<32>(); FillExtUniforms(extended,matrix,p);
    expected={1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,
        17,18,19,20,21,22,23,1,25,26,27,1,29,30,31,1};
    ExpectBlock(extended,expected);
    p.lightingEnabled=false; p.textureEnabled=false; p.vertexColorEnabled=false;
    FillExtUniforms(extended,matrix,p); expected[23]=expected[27]=expected[31]=0;
    ExpectBlock(extended,expected);
}

TEST(SdlGpuUniformPacking, FogAndAlphaTestBlocks)
{
    // colored3d.vert FogParams: two vec4s. alpha_test3d.vert PC: mat4, diffuse,
    // alphaTest, vertexColorEnabled then seven padding floats.
    auto p=Params(); auto fog=Poison<8>(); FillFogUniforms(fog,p);
    ExpectBlock(fog,std::array<float,8>{81,82,83,1,85,86,87,88});
    p.fogEnabled=false; FillFogUniforms(fog,p);
    ExpectBlock(fog,std::array<float,8>{81,82,83,0,85,86,87,88});
    auto alpha=Poison<32>(); FillAlphaTestUniforms(alpha,matrix,p);
    std::array<float,32> expected{1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,
        17,18,19,20,89,90,91,92,1,0,0,0,0,0,0,0};
    ExpectBlock(alpha,expected);
    p.vertexColorEnabled=false; FillAlphaTestUniforms(alpha,matrix,p); expected[24]=0;
    ExpectBlock(alpha,expected);
}

TEST(SdlGpuUniformPacking, LightingAndSkinningShareLayoutExceptWeightCount)
{
    // lit_textured3d.vert.glsl LitLightParams: 5 vec4s, mat4, 5 vec4s = 224 bytes.
    // skinned3d.vert.glsl uses eyePos.w (byte 156) for weightsPerVertex.
    auto p=Params(); auto lit=Poison<56>(); FillLitLightUniforms(lit,p);
    std::array<float,56> expected{41,42,43,0,45,46,47,0,49,50,51,0,53,54,55,0,57,58,59,0,
        1,5,9,13,2,6,10,14,3,7,11,15,4,8,12,16,
        61,62,63,0,65,66,67,0,69,70,71,0,73,74,75,0,77,78,79,80};
    ExpectBlock(lit,expected);
    p.preferPerPixelLighting=true; FillLitLightUniforms(lit,p); expected[19]=1;
    ExpectBlock(lit,expected);
    auto skin=Poison<56>(); FillSkinnedLightUniforms(skin,p); expected[39]=2;
    ExpectBlock(skin,expected);
    for (int weights : {1,4}) { p.weightsPerVertex=weights; FillSkinnedLightUniforms(skin,p); expected[39]=weights; ExpectBlock(skin,expected); }
}

TEST(SdlGpuUniformPacking, EnvironmentMapPrimaryAndSecondaryBlocks)
{
    // env_map3d.vert.glsl: PC(mat4, diffuse, emissiveAmount), EnvMapParams(mat4, 8 vec4).
    auto p=Params(); auto primary=Poison<24>(); FillEnvMapUniforms(primary,matrix,p);
    ExpectBlock(primary,std::array<float,24>{1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,
        17,18,19,20,57,58,59,93});
    auto secondary=Poison<48>(); FillEnvMapParams(secondary,p);
    std::array<float,48> expected{1,5,9,13,2,6,10,14,3,7,11,15,4,8,12,16,
        61,62,63,1,25,26,27,94,29,30,31,0,41,42,43,0,45,46,47,0,49,50,51,0,53,54,55,0,95,96,97,0};
    ExpectBlock(secondary,expected);
    p.fresnelEnabled=false; FillEnvMapParams(secondary,p); expected[19]=0;
    ExpectBlock(secondary,expected);
}

TEST(SdlGpuUniformPacking, BonePaletteCopiesColumnsAndClearsUnusedTail)
{
    // skinned3d.vert.glsl Bones: 72 mat4s = 4608 bytes, column-major.
    auto p=Params();
    for (int i=0; i<72*16; ++i) p.boneTransforms[i]=static_cast<float>(i+1);
    for (int count : {0,1,2,72}) {
        p.boneCount=count; auto out=Poison<72*16>(); FillSkinnedBoneUniforms(out,p);
        for (int i=0; i<72*16; ++i) EXPECT_FLOAT_EQ(out[i],i<count*16 ? static_cast<float>(i+1) : 0) << count << '/' << i;
    }
}

TEST(SdlGpuUniformPacking, PbrBlockPreservesAllFourteenTextureRows)
{
    // pbr3d.frag.glsl PbrParams: four vec4s followed by 10+4 vec4 affine rows.
    auto p=Params(); p.pbrMetallicFactor=1; p.pbrRoughnessFactor=2;
    p.pbrNormalScale=3; p.pbrOcclusionStrength=4;
    p.pbrBaseColorTextureIsSrgb=true; p.pbrEmissiveTextureIsSrgb=false;
    p.pbrEncodeOutputToSrgb=true; p.pbrSpecularColorTextureIsSrgb=false;
    Assign(p.pbrDielectricF0Unclamped,{13,14,15}); p.pbrSpecularFactor=16;
    for (int row=0; row<10; ++row) for (int c=0; c<4; ++c) p.pbrTextureTransformRows[row][c]=17+row*4+c;
    for (int row=0; row<4; ++row) for (int c=0; c<4; ++c) p.pbrSpecularTextureTransformRows[row][c]=57+row*4+c;
    std::array<float,72> expected{1,2,3,4,89,90,91,92,1,0,1,0,13,14,15,16};
    for (int i=16; i<72; ++i) expected[i]=i+1;
    auto out=Poison<72>(); FillPbrParams(out,p); ExpectBlock(out,expected);
    p.pbrBaseColorTextureIsSrgb=false; p.pbrEmissiveTextureIsSrgb=true;
    p.pbrEncodeOutputToSrgb=false; p.pbrSpecularColorTextureIsSrgb=true;
    expected[8]=0; expected[9]=1; expected[10]=0; expected[11]=1;
    FillPbrParams(out,p); ExpectBlock(out,expected);
}
#endif
