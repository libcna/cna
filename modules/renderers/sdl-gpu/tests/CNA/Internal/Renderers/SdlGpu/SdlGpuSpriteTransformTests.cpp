// SPDX-License-Identifier: MS-PL
// GAP-07a: device-free tests of the position calculation used by QueueSprite.
#include "../../../../../src/SdlGpuSpriteTransform.hpp"

#include <gtest/gtest.h>

namespace
{
    using Microsoft::Xna::Framework::Matrix;
    using CNA::Internal::Renderers::SdlGpu::Detail::TransformSpritePositionEXT;
    using Position = std::array<float, 4>;

    TEST(SdlGpuSpriteTransform, IdentityPreservesRawDepthAndUnitW)
    {
        // (2,3,.5,1)*I is unchanged. Inputs/results are exactly representable float32.
        EXPECT_EQ(TransformSpritePositionEXT(2, 3, .5f, Matrix::getIdentityProperty(), false),
                  (Position{2, 3, .5f, 1}));
    }

    TEST(SdlGpuSpriteTransform, AllSixteenCoefficientsContributeBeforePerspectiveDivide)
    {
        // XNA Refresh SpriteBatch.SetRenderState IL_0176-0188 retains the complete T*P;
        // FNA StockEffects/HLSL/SpriteEffect.fx:22-26 multiplies all four components.
        Matrix m = Matrix::getIdentityProperty();
        m.M11 = 2; m.M21 = 4; m.M31 = 8; m.M41 = 16;
        m.M12 = 3; m.M22 = -2; m.M32 = -4; m.M42 = 8;
        m.M13 = .25f; m.M23 = .5f; m.M33 = .5f; m.M43 = .125f;
        m.M14 = .5f; m.M24 = .25f; m.M34 = 2; m.M44 = 1;
        // For (2,3,.5,1): X=4+12+4+16=36, Y=6-6-2+8=6,
        // Z=.5+1.5+.25+.125=2.375, W=1+.75+1+1=3.75. No divide here.
        EXPECT_EQ(TransformSpritePositionEXT(2, 3, .5f, m, false),
                  (Position{36, 6, 2.375f, 3.75f}));
    }

    TEST(SdlGpuSpriteTransform, LayerDepthMovesBothScreenAxes)
    {
        // Refresh SetRenderState IL_017c-0188 / FNA SpriteBatch.cs:1477,1481.
        Matrix m = Matrix::getIdentityProperty();
        m.M31 = 32; m.M32 = 16;
        // .5*32=16 and .5*16=8; destination (4,6) becomes (20,14).
        EXPECT_EQ(TransformSpritePositionEXT(4, 6, .5f, m, false),
                  (Position{20, 14, .5f, 1}));
    }

    TEST(SdlGpuSpriteTransform, VaryingWRemainsAttachedToEachCorner)
    {
        // Hand-derived homogeneous corners of the shared four-band UV pixel fixture:
        // W=1+x/64. The GPU must receive W=1 and 2 at left/right, not divided XYZ with W=1.
        Matrix m = Matrix::getIdentityProperty();
        m.M14 = 1.0f / 64.0f;
        EXPECT_EQ(TransformSpritePositionEXT(0, 0, .25f, m, false),
                  (Position{0, 0, .25f, 1}));
        EXPECT_EQ(TransformSpritePositionEXT(64, 0, .25f, m, false),
                  (Position{64, 0, .25f, 2}));
        EXPECT_EQ(TransformSpritePositionEXT(0, 64, .25f, m, false),
                  (Position{0, 64, .25f, 1}));
        EXPECT_EQ(TransformSpritePositionEXT(64, 64, .25f, m, false),
                  (Position{64, 64, .25f, 2}));
    }

    TEST(SdlGpuSpriteTransform, DepthContributesToHomogeneousW)
    {
        // (.5 depth)*M34(2)+M44(1)=2. XYZ is still (8,12,.5).
        Matrix m = Matrix::getIdentityProperty();
        m.M34 = 2;
        EXPECT_EQ(TransformSpritePositionEXT(8, 12, .5f, m, false),
                  (Position{8, 12, .5f, 2}));
    }

    TEST(SdlGpuSpriteTransform, ZeroAndNegativeWArePreservedForHardwareClipping)
    {
        // Homogeneous clipping must happen before divide. Do not fabricate W or zero Z.
        // Refresh SetRenderState IL_0128-013b keeps Z/W; FNA SpriteEffect.fx:24-26.
        Matrix m = Matrix::getIdentityProperty();
        m.M44 = 0;
        EXPECT_EQ(TransformSpritePositionEXT(8, 12, .25f, m, false),
                  (Position{8, 12, .25f, 0}));
        m.M44 = -1;
        EXPECT_EQ(TransformSpritePositionEXT(8, 12, -.25f, m, false),
                  (Position{8, 12, -.25f, -1}));
    }

    TEST(SdlGpuSpriteTransform, CompiledVertexInputsBypassBeginTransform)
    {
        // Refresh SetRenderState IL_018d-01a3 then RenderBatch IL_0022-0055 applies
        // the custom pass after stock VS. FNA SpriteBatch.cs:1369-1450 submits raw XYZ.
        Matrix poison = Matrix::CreateTranslation(256, 256, 8);
        poison.M31 = 32; poison.M32 = 16; poison.M14 = .5f; poison.M44 = 0;
        EXPECT_EQ(TransformSpritePositionEXT(2, 3, .5f, poison, true),
                  (Position{2, 3, .5f, 1}));
    }
}
