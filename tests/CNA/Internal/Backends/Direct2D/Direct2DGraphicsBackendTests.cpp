// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#if defined(CNA_BACKEND_DIRECT2D)
#include "CNA/Internal/Backends/Direct2D/Direct2DGraphicsBackend.hpp"

using CNA::Internal::Backends::Direct2D::BlendStateToDirect2DBlendMode;
using CNA::Internal::Backends::Direct2D::Direct2DBlendMode;
using CNA::Internal::Backends::Direct2D::FractionalMipLevelForTransform;
using CNA::Internal::Backends::Direct2D::IsDeviceLossHResult;
using CNA::Internal::Backends::Direct2D::MapSourceRectangleToMip;
using CNA::Internal::Backends::Direct2D::PreferredMipLevelForTransform;
using Microsoft::Xna::Framework::Matrix;
using XnaRectangle = Microsoft::Xna::Framework::Rectangle;

TEST(Direct2DDeviceLossClassification, RecognizesEveryDocumentedDeviceLossHResult)
{
    EXPECT_TRUE(IsDeviceLossHResult(D2DERR_RECREATE_TARGET));
    EXPECT_TRUE(IsDeviceLossHResult(DXGI_ERROR_DEVICE_REMOVED));
    EXPECT_TRUE(IsDeviceLossHResult(DXGI_ERROR_DEVICE_RESET));
    EXPECT_TRUE(IsDeviceLossHResult(DXGI_ERROR_DEVICE_HUNG));
    EXPECT_TRUE(IsDeviceLossHResult(DXGI_ERROR_DRIVER_INTERNAL_ERROR));
}

TEST(Direct2DDeviceLossClassification, DoesNotFlagOrdinaryFailuresOrSuccess)
{
    EXPECT_FALSE(IsDeviceLossHResult(S_OK));
    EXPECT_FALSE(IsDeviceLossHResult(E_INVALIDARG));
    EXPECT_FALSE(IsDeviceLossHResult(E_OUTOFMEMORY));
    EXPECT_FALSE(IsDeviceLossHResult(DXGI_ERROR_INVALID_CALL));
}

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

// D2D-75: FractionalMipLevelForTransform is PreferredMipLevelForTransform's continuous-LOD
// sibling (D2D-74) but, unlike it, had no unit coverage of its own -- only an indirect exercise
// via the Wine pixel test's single LOD=1.5 mip-blend case. Each case below isolates ONE of the
// axes named by this task (NPOT, scale, rotation, shear, singularity, presentation scale, clamp)
// with an expected value hand-derived independently of ComputeMinificationSigma's own formula.
TEST(Direct2DFractionalMipLod, NpotSourceWithNonUniformDestinationScale)
{
    // source 6x3 (NPOT on both axes) -> destination 3x3 halves only the X axis (scaleX=0.5,
    // scaleY=1.0). Singular values of diag(0.5, 1.0) are 1.0 and 0.5, so sigmaMin=0.5, LOD=1.0.
    bool minifying = false;
    const double lod = FractionalMipLevelForTransform(6, 3, 3, 3, 0.0f, Matrix::getIdentityProperty(),
                                                       1.0f, 1.0f, &minifying);
    EXPECT_TRUE(minifying);
    EXPECT_NEAR(lod, 1.0, 1e-9);
}

TEST(Direct2DFractionalMipLod, PureUniformScaleGivesExactLog2)
{
    bool minifying = false;
    const double lod = FractionalMipLevelForTransform(4, 4, 1, 1, 0.0f, Matrix::getIdentityProperty(),
                                                       1.0f, 1.0f, &minifying);
    EXPECT_TRUE(minifying);
    EXPECT_NEAR(lod, 2.0, 1e-9); // scale 0.25 -> log2(1/0.25) = 2
}

TEST(Direct2DFractionalMipLod, PureRotationAloneNeverMinifies)
{
    // A 90-degree rotation with no scale change (dest == src) must not be mistaken for
    // minification -- both singular values of a true rotation matrix are exactly 1. float
    // rounding of the angle can nudge the computed sigmaMin a hair above or below 1.0, so
    // `minifying`'s boolean is not asserted here; the resulting LOD stays vanishingly close to
    // zero either way (proportional to the rounding error, not to any real minification).
    const double halfPi = std::acos(0.0); // pi/2, without relying on the non-standard M_PI macro
    bool minifying = false;
    const double lod = FractionalMipLevelForTransform(
        4, 4, 4, 4, static_cast<float>(halfPi), Matrix::getIdentityProperty(), 1.0f, 1.0f, &minifying);
    EXPECT_NEAR(lod, 0.0, 1e-5);
}

TEST(Direct2DFractionalMipLod, PureShearMinifiesByGoldenRatio)
{
    // Shear matrix [[1,1],[0,1]] (no rotation, dest == src) has singular values phi and 1/phi
    // (phi = golden ratio) -- a closed form independent of ComputeMinificationSigma's own code.
    Matrix shear = Matrix::getIdentityProperty();
    shear.M12 = 1.0f;
    bool minifying = false;
    const double lod = FractionalMipLevelForTransform(4, 4, 4, 4, 0.0f, shear, 1.0f, 1.0f, &minifying);
    EXPECT_TRUE(minifying);
    const double phi = (1.0 + std::sqrt(5.0)) / 2.0;
    EXPECT_NEAR(lod, std::log2(phi), 1e-9);
}

TEST(Direct2DFractionalMipLod, SingularTransformIsPositiveInfinity)
{
    Matrix singular = Matrix::CreateScale(0.0f);
    bool minifying = false;
    const double lod = FractionalMipLevelForTransform(4, 4, 4, 4, 0.0f, singular, 1.0f, 1.0f, &minifying);
    EXPECT_TRUE(minifying);
    EXPECT_TRUE(std::isinf(lod));
    EXPECT_GT(lod, 0.0);
}

TEST(Direct2DFractionalMipLod, PresentationScaleAloneDrivesMinification)
{
    // dest == src and an identity batch transform -- the halving comes entirely from
    // presentationScaleX, isolating that axis from source/destination/batch-transform scale.
    bool minifying = false;
    const double lod = FractionalMipLevelForTransform(4, 4, 4, 4, 0.0f, Matrix::getIdentityProperty(),
                                                       0.5f, 1.0f, &minifying);
    EXPECT_TRUE(minifying);
    EXPECT_NEAR(lod, 1.0, 1e-9);
}

TEST(Direct2DFractionalMipLod, MagnifyingTransformClampsToZeroNotNegative)
{
    // destination larger than source (4x magnification) must clamp to exactly 0.0, not the
    // negative value log2(1/sigmaMin) would otherwise produce for sigmaMin > 1.
    bool minifying = true;
    const double lod = FractionalMipLevelForTransform(2, 2, 8, 8, 0.0f, Matrix::getIdentityProperty(),
                                                       1.0f, 1.0f, &minifying);
    EXPECT_FALSE(minifying);
    EXPECT_NEAR(lod, 0.0, 1e-9);
}
#endif
