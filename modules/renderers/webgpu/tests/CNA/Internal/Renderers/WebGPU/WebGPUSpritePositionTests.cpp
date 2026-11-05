// SPDX-License-Identifier: MS-PL
// GAP-07b: CPU contracts for the functions QueueSprite and compiled inheritance use.
// Oracle: Refresh SpriteBatch.SetRenderState IL_0104-0188 (full T*P) and
// RenderBatch IL_0022-0055 (custom vertex-stage ownership), corroborated by
// FNA SpriteBatch.cs:1369-1494 / SpriteEffect.fx:22-33. Values are hand-derived.
#include "CNA/Internal/Renderers/WebGPU/WebGPUSpritePosition.hpp"
#include "CNA/Internal/Renderers/WebGPU/WebGPUWgslReflection.hpp"
#include "CNA/Internal/Renderers/WebGPU/webgpu_shaders.hpp"
#include <gtest/gtest.h>

using namespace CNA::Internal::Renderers::WebGPU;
using Microsoft::Xna::Framework::Matrix;

namespace
{
    void PositionIs(const std::array<float, 4>& value, const std::array<float, 4>& expected)
    {
        // All inputs/products here are dyadic floats, so no rounding tolerance is needed.
        for (std::size_t i = 0; i < value.size(); ++i)
            EXPECT_FLOAT_EQ(value[i], expected[i]) << "component " << i;
    }
}

TEST(WebGPUSpritePositionTest, LayerDepthContributesToBothPixelAxes)
{
    Matrix matrix = Matrix::getIdentityProperty();
    matrix.M31 = 32; matrix.M32 = -16;
    // (4,12,.5,1)*T = (4+.5*32,12-.5*16,.5,1).
    PositionIs(ResolveSpritePositionEXT(4,12,.5f,matrix,false), {20,4,.5f,1});
}

TEST(WebGPUSpritePositionTest, DepthAndHomogeneousWReachClippingUndivided)
{
    Matrix matrix = Matrix::getIdentityProperty();
    matrix.M14 = 1.0f/64; matrix.M24 = 1.0f/32; matrix.M34 = 2;
    matrix.M13 = .5f; matrix.M23 = -.25f; matrix.M33 = .5f; matrix.M43 = -1;
    // Z=16*.5-8*.25+.5*.5-1=5.25; W=16/64+8/32+.5*2+1=2.5.
    PositionIs(ResolveSpritePositionEXT(16,8,.5f,matrix,false), {16,8,5.25f,2.5f});
    matrix.M14 = 0; matrix.M24 = 0; matrix.M34 = 0;
    matrix.M44 = 0;
    PositionIs(ResolveSpritePositionEXT(16,8,.5f,matrix,false), {16,8,5.25f,0});
    matrix.M44 = -1;
    PositionIs(ResolveSpritePositionEXT(16,8,.5f,matrix,false), {16,8,5.25f,-1});
}

TEST(WebGPUSpritePositionTest, ProjectiveCornersPreserveReciprocalWInputs)
{
    Matrix matrix = Matrix::getIdentityProperty(); matrix.M14 = 1.0f/64;
    // q corners=(0,0,.25,1),(64,0,.25,2),(0,64,.25,1),(64,64,.25,2).
    // clip=(2X/64-W,W-2Y/64,Z,W), retaining the W needed for smooth UV.
    const std::array<std::array<float,2>,4> corners{{{0,0},{64,0},{0,64},{64,64}}};
    const std::array<std::array<float,4>,4> expected{{{-1,1,.25f,1},{0,2,.25f,2},
                                                   {-1,-1,.25f,1},{0,0,.25f,2}}};
    for (std::size_t i=0; i<corners.size(); ++i)
        PositionIs(ProjectSpritePositionEXT(ResolveSpritePositionEXT(
            corners[i][0],corners[i][1],.25f,matrix,false),64,64),expected[i]);
}

TEST(WebGPUSpritePositionTest, ConstantAndDepthDependentWScaleProjectionOffsets)
{
    Matrix matrix = Matrix::getIdentityProperty(); matrix.M44=2;
    // Full-quad lower-right: (64,64,.5,2) -> (0,0,.5,2), NDC=(0,0,.25).
    const auto constant = ResolveSpritePositionEXT(64,64,.5f,matrix,false);
    PositionIs(ProjectSpritePositionEXT(constant,64,64), {0,0,.5f,2});
    matrix.M44=1; matrix.M34=2;
    PositionIs(ResolveSpritePositionEXT(64,64,.5f,matrix,false),constant);
}

TEST(WebGPUSpritePositionTest, CompiledVertexStageReceivesRawPosition)
{
    Matrix matrix=Matrix::CreateTranslation(256,256,0);
    matrix.M31=32; matrix.M32=-16; matrix.M14=1.0f/64; matrix.M34=2;
    // An explicit VS owns its Transform; Begin's poisonous matrix must not touch XYZ.
    PositionIs(ResolveSpritePositionEXT(4,12,.5f,matrix,true), {4,12,.5f,1});
}

TEST(WebGPUSpritePositionTest, PixelOnlyStageInheritsBeginTimesProjection)
{
    Matrix matrix=Matrix::getIdentityProperty();
    matrix.M31=32; matrix.M32=-16; matrix.M14=1.0f/64; matrix.M34=2;
    const Matrix result=SpriteMatrixTransformEXT(matrix,64,64);
    // P has M11=1/32,M22=-1/32,M33=M44=1,M41=-1,M42=1.
    // T*P rows: (1/64,1/64,0,1/64),(0,-1/32,0,0),
    //           (-1,5/2,1,2),(-1,1,0,1).
    const float actual[16]={result.M11,result.M12,result.M13,result.M14,
                            result.M21,result.M22,result.M23,result.M24,
                            result.M31,result.M32,result.M33,result.M34,
                            result.M41,result.M42,result.M43,result.M44};
    const float expected[16]={1.0f/64,1.0f/64,0,1.0f/64, 0,-1.0f/32,0,0,
                              -1,2.5f,1,2, -1,1,0,1};
    for (int i=0;i<16;++i) EXPECT_FLOAT_EQ(actual[i],expected[i]) << "matrix slot " << i;
}

TEST(WebGPUSpritePositionTest, DescriptorRetainsDepthAndWWhileReconstructingPixelNumerators)
{
    // Inverse projection: X=(clipX+W)*width/2, Y=(W-clipY)*height/2.
    PositionIs(DescriptorSpritePositionEXT({-1,.5f,.25f,2},64,64), {32,48,.25f,2});
    PositionIs(DescriptorSpritePositionEXT({.25f,-.5f,.25f,0},64,64), {8,16,.25f,0});
    PositionIs(DescriptorSpritePositionEXT({.25f,-.5f,.25f,-1},64,64), {-24,-16,.25f,-1});
}

TEST(WebGPUSpritePositionTest, IdentityAndAffineControlHaveLiteralClipValues)
{
    const Matrix identity=Matrix::getIdentityProperty();
    PositionIs(ProjectSpritePositionEXT(ResolveSpritePositionEXT(16,48,.5f,identity,false),64,64),
               {-.5f,-.5f,.5f,1});
    const Matrix affine=Matrix::CreateScale(.5f,.5f,1)*Matrix::CreateTranslation(8,16,0);
    PositionIs(ResolveSpritePositionEXT(16,32,.5f,affine,false),{16,32,.5f,1});
    PositionIs(DescriptorSpritePositionEXT({-.5f,0,.5f,1},64,64), {16,32,.5f,1});
}

TEST(WebGPUSpritePositionTest, StockVertexShaderConsumesFourPositionComponents)
{
    // WGSL reflection is independent of CPU packing: vec3 input would default missing W to 1.
    const auto reflection=ReflectWgsl(webgpu_shaders::kSprite);
    ASSERT_TRUE(reflection.ok) << reflection.error;
    const auto* vertex=reflection.FindEntryPoint(WGPUShaderStage_Vertex);
    ASSERT_NE(vertex,nullptr);
    ASSERT_FALSE(vertex->vertexInputs.empty());
    EXPECT_EQ(vertex->vertexInputs[0].location,0u);
    // vec4f and vec4<f32> are the same WGSL float4 type; reflection retains aliases.
    const auto& type=vertex->vertexInputs[0].type;
    EXPECT_TRUE(type=="vec4f" || type=="vec4<f32>") << type;
}
