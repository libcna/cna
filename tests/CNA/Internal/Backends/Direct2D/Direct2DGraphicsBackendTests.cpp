// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include <limits>

#if defined(CNA_BACKEND_DIRECT2D)
#include "CNA/Internal/Backends/Direct2D/Direct2DGraphicsBackend.hpp"

using CNA::Internal::Backends::Direct2D::BlendStateToDirect2DBlendMode;
using CNA::Internal::Backends::Direct2D::Direct2DBlendMode;
using CNA::Internal::Backends::Direct2D::MapSourceRectangleToMip;
using CNA::Internal::Backends::Direct2D::PreferredMipLevelForTransform;
using Microsoft::Xna::Framework::Matrix;
using XnaRectangle = Microsoft::Xna::Framework::Rectangle;

TEST(Direct2DBlendStateMapping, StandardSpriteBatchPresetsMapToNativePrimitiveBlends)
{
    EXPECT_EQ(BlendStateToDirect2DBlendMode(0, 0, 1, 1, 0, 0), Direct2DBlendMode::Copy);
    EXPECT_EQ(BlendStateToDirect2DBlendMode(0, 0, 5, 5, 0, 0), Direct2DBlendMode::SourceOver);
    EXPECT_EQ(BlendStateToDirect2DBlendMode(4, 4, 5, 5, 0, 0), Direct2DBlendMode::SourceOver);
    EXPECT_EQ(BlendStateToDirect2DBlendMode(4, 4, 0, 0, 0, 0), Direct2DBlendMode::Add);
}

TEST(Direct2DBlendStateMapping, UnsupportedFactorOrEquationFailsExplicitly)
{
    EXPECT_THROW(BlendStateToDirect2DBlendMode(0, 4, 5, 5, 0, 0), std::runtime_error);
    EXPECT_THROW(BlendStateToDirect2DBlendMode(2, 2, 3, 3, 0, 0), std::runtime_error);
    EXPECT_THROW(BlendStateToDirect2DBlendMode(0, 0, 5, 5, 1, 1), std::runtime_error);
}

TEST(Direct2DBlendStateMapping, ExactSymmetricPorterDuffFactorsMapToCompositeModes)
{
    EXPECT_EQ(BlendStateToDirect2DBlendMode(0, 0, 0, 0, 0, 0), Direct2DBlendMode::Add);
    EXPECT_EQ(BlendStateToDirect2DBlendMode(9, 9, 0, 0, 0, 0), Direct2DBlendMode::DestinationOver);
    EXPECT_EQ(BlendStateToDirect2DBlendMode(8, 8, 1, 1, 0, 0), Direct2DBlendMode::SourceIn);
    EXPECT_EQ(BlendStateToDirect2DBlendMode(1, 1, 4, 4, 0, 0), Direct2DBlendMode::DestinationIn);
    EXPECT_EQ(BlendStateToDirect2DBlendMode(9, 9, 1, 1, 0, 0), Direct2DBlendMode::SourceOut);
    EXPECT_EQ(BlendStateToDirect2DBlendMode(1, 1, 5, 5, 0, 0), Direct2DBlendMode::DestinationOut);
    EXPECT_EQ(BlendStateToDirect2DBlendMode(8, 8, 5, 5, 0, 0), Direct2DBlendMode::SourceAtop);
    EXPECT_EQ(BlendStateToDirect2DBlendMode(9, 9, 4, 4, 0, 0), Direct2DBlendMode::DestinationAtop);
    EXPECT_EQ(BlendStateToDirect2DBlendMode(9, 9, 5, 5, 0, 0), Direct2DBlendMode::Xor);
}

TEST(Direct2DMipPolicy, MapsNpotSourceCoordinatesUsingActualLevelDimensions)
{
    const XnaRectangle mapped = MapSourceRectangleToMip(XnaRectangle(5, 2, 5, 8), 10, 10, 2, 2);
    EXPECT_EQ(mapped.X, 1);
    EXPECT_EQ(mapped.Y, 0);
    EXPECT_EQ(mapped.Width, 1);
    EXPECT_EQ(mapped.Height, 2);

    const XnaRectangle negative = MapSourceRectangleToMip(XnaRectangle(-1, -1, 4, 4), 3, 3, 1, 1);
    EXPECT_EQ(negative.X, -1);
    EXPECT_EQ(negative.Y, -1);
    EXPECT_EQ(negative.Width, 2);
    EXPECT_EQ(negative.Height, 2);
}

TEST(Direct2DMipPolicy, RejectsUnrepresentableOrNonPositiveSourceRectangles)
{
    const int minimum = std::numeric_limits<int>::min();
    const int maximum = std::numeric_limits<int>::max();

    EXPECT_THROW(
        MapSourceRectangleToMip(XnaRectangle(maximum - 1, 0, maximum, 1), 1, 1, 1, 1),
        std::exception);
    EXPECT_THROW(
        MapSourceRectangleToMip(XnaRectangle(minimum, 0, maximum, 1), 1, 1, 1, 1),
        std::exception);
    EXPECT_THROW(
        MapSourceRectangleToMip(XnaRectangle(0, 0, 0, 1), 4, 4, 2, 2),
        std::exception);
    EXPECT_THROW(
        MapSourceRectangleToMip(XnaRectangle(0, 0, 1, -1), 4, 4, 2, 2),
        std::exception);
}

TEST(Direct2DMipPolicy, UsesCompleteBatchAndPresentationTransform)
{
    bool minifying = false;
    EXPECT_EQ(PreferredMipLevelForTransform(4, 4, 4, 4, 0.0f, Matrix::getIdentityProperty(),
                                            1.0f, 1.0f, &minifying), 0);
    EXPECT_FALSE(minifying);

    Matrix quarterScale = Matrix::CreateScale(0.25f);
    EXPECT_EQ(PreferredMipLevelForTransform(4, 4, 4, 4, 0.0f, quarterScale,
                                            1.0f, 1.0f, &minifying), 2);
    EXPECT_TRUE(minifying);

    Matrix shearedQuarter = quarterScale;
    shearedQuarter.M12 = 0.25f;
    EXPECT_GE(PreferredMipLevelForTransform(8, 8, 8, 8, 0.4f, shearedQuarter,
                                            0.5f, 1.0f, &minifying), 2);
    EXPECT_TRUE(minifying);

    Matrix singular = Matrix::CreateScale(0.0f);
    EXPECT_EQ(PreferredMipLevelForTransform(4, 4, 4, 4, 0.0f, singular,
                                            1.0f, 1.0f, nullptr), std::numeric_limits<int>::max());
}
#endif
