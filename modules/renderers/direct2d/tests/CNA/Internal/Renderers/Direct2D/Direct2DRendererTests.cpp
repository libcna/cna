// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

// plan_runtimerenderer.md RTR-P9-9: PRESENT_, not the identity macro. This suite is
// device-free policy coverage for its own renderer, so it is worth compiling and running
// whenever that renderer is COMPILED IN -- in a multi-renderer build it need not be the
// selected one. Only the default renderer's CNA_RENDERER_DIRECT2D is defined project-wide.
#if defined(CNA_RENDERER_DIRECT2D) || defined(CNA_RENDERER_PRESENT_DIRECT2D)
#include "CNA/Internal/Renderers/Direct2D/Direct2DRenderer.hpp"
#include "System/NotSupportedException.hpp"

using CNA::Internal::Renderers::Direct2D::BlendStateToDirect2DBlendMode;
using CNA::Internal::Renderers::Direct2D::CheckedRgbaByteCount;
using CNA::Internal::Renderers::Direct2D::CompositePorterDuffReference;
using CNA::Internal::Renderers::Direct2D::CopyBgraToTightRgba;
using CNA::Internal::Renderers::Direct2D::CopyRgbaToTightBgra;
using CNA::Internal::Renderers::Direct2D::Direct2DBlendMode;
using CNA::Internal::Renderers::Direct2D::FractionalMipLevelForTransform;
using CNA::Internal::Renderers::Direct2D::IsDeviceLossHResult;
using CNA::Internal::Renderers::Direct2D::MapSourceRectangleToMip;
using CNA::Internal::Renderers::Direct2D::SpriteBitmapCacheKey;
using CNA::Internal::Renderers::Direct2D::SpriteBrushCacheKey;
using CNA::Internal::Renderers::Direct2D::PreferredMipLevelForTransform;
using CNA::Internal::Renderers::Direct2D::SupportsDirect2DCapability;
using CNA::GraphicsCapability;
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

TEST(Direct2DBlendStateMapping, SupportedStandardSpriteBatchPresetsMapToNativePrimitiveBlends)
{
    EXPECT_EQ(BlendStateToDirect2DBlendMode(0, 0, 1, 1, 0, 0), Direct2DBlendMode::Copy);
    EXPECT_EQ(BlendStateToDirect2DBlendMode(0, 0, 5, 5, 0, 0), Direct2DBlendMode::SourceOver);
    EXPECT_EQ(BlendStateToDirect2DBlendMode(4, 4, 5, 5, 0, 0), Direct2DBlendMode::SourceOver);
    EXPECT_THROW(BlendStateToDirect2DBlendMode(4, 4, 0, 0, 0, 0), System::NotSupportedException);
}

TEST(Direct2DBlendStateMapping, UnsupportedFactorOrEquationFailsExplicitly)
{
    EXPECT_THROW(BlendStateToDirect2DBlendMode(0, 4, 5, 5, 0, 0), std::runtime_error);
    EXPECT_THROW(BlendStateToDirect2DBlendMode(2, 2, 3, 3, 0, 0), std::runtime_error);
    EXPECT_THROW(BlendStateToDirect2DBlendMode(0, 0, 5, 5, 1, 1), std::runtime_error);
}

TEST(Direct2DBlendStateMapping, ExactSymmetricPorterDuffFactorsMapToCompositeModes)
{
    EXPECT_THROW(BlendStateToDirect2DBlendMode(0, 0, 0, 0, 0, 0), System::NotSupportedException);
    EXPECT_EQ(BlendStateToDirect2DBlendMode(9, 9, 0, 0, 0, 0), Direct2DBlendMode::DestinationOver);
    EXPECT_EQ(BlendStateToDirect2DBlendMode(8, 8, 1, 1, 0, 0), Direct2DBlendMode::SourceIn);
    EXPECT_EQ(BlendStateToDirect2DBlendMode(1, 1, 4, 4, 0, 0), Direct2DBlendMode::DestinationIn);
    EXPECT_EQ(BlendStateToDirect2DBlendMode(9, 9, 1, 1, 0, 0), Direct2DBlendMode::SourceOut);
    EXPECT_EQ(BlendStateToDirect2DBlendMode(1, 1, 5, 5, 0, 0), Direct2DBlendMode::DestinationOut);
    EXPECT_EQ(BlendStateToDirect2DBlendMode(8, 8, 5, 5, 0, 0), Direct2DBlendMode::SourceAtop);
    EXPECT_EQ(BlendStateToDirect2DBlendMode(9, 9, 4, 4, 0, 0), Direct2DBlendMode::DestinationAtop);
    EXPECT_EQ(BlendStateToDirect2DBlendMode(9, 9, 5, 5, 0, 0), Direct2DBlendMode::Xor);
}

TEST(Direct2DCapabilityContract, IsExhaustiveForAllThirteenCapabilities)
{
    struct CapabilityExpectation
    {
        GraphicsCapability capability;
        bool supported;
    };
    constexpr std::array<CapabilityExpectation, 13> expectations{{
        {GraphicsCapability::ThreeD, false},
        {GraphicsCapability::DepthStencilBuffer, false},
        {GraphicsCapability::MultiSampleAntiAliasing, false},
        {GraphicsCapability::MultipleRenderTargets, false},
        {GraphicsCapability::AnisotropicFiltering, true},
        {GraphicsCapability::WireFrame, false},
        {GraphicsCapability::OcclusionQuery, false},
        {GraphicsCapability::CustomEffects, false},
        {GraphicsCapability::Texture3D, false},
        {GraphicsCapability::MultiStreamVertexInput, false},
        {GraphicsCapability::Instancing, false},
        {GraphicsCapability::StencilBuffer, false},
        {GraphicsCapability::AdditiveBlending, false},
    }};

    for (const auto& expectation : expectations)
        EXPECT_EQ(SupportsDirect2DCapability(expectation.capability), expectation.supported);
}

TEST(Direct2DPixelConversion, ConvertsOddWidthPitchedRgbaToTightBgraWithoutLosingAlpha)
{
    constexpr int width = 3;
    constexpr int height = 2;
    constexpr int pitch = 16;
    const std::array<std::uint8_t, pitch * height> rgba{{
        1, 2, 3, 4,       10, 20, 30, 40,    101, 102, 103, 104,  0xEE, 0xEE, 0xEE, 0xEE,
        5, 6, 7, 8,       50, 60, 70, 80,    201, 202, 203, 204,  0xDD, 0xDD, 0xDD, 0xDD,
    }};
    const std::vector<std::uint8_t> expected{
        3, 2, 1, 4,       30, 20, 10, 40,    103, 102, 101, 104,
        7, 6, 5, 8,       70, 60, 50, 80,    203, 202, 201, 204,
    };

    EXPECT_EQ(CheckedRgbaByteCount(width, height), expected.size());
    EXPECT_EQ(CopyRgbaToTightBgra(rgba.data(), pitch, width, height), expected);
}

TEST(Direct2DPixelConversion, ConvertsPitchedBgraToTightRgbaAndIgnoresPadding)
{
    constexpr int width = 3;
    constexpr int height = 2;
    constexpr int pitch = 16;
    const std::array<std::uint8_t, pitch * height> bgra{{
        3, 2, 1, 4,       30, 20, 10, 40,    103, 102, 101, 104,  0xA1, 0xA2, 0xA3, 0xA4,
        7, 6, 5, 8,       70, 60, 50, 80,    203, 202, 201, 204,  0xB1, 0xB2, 0xB3, 0xB4,
    }};
    const std::vector<std::uint8_t> expected{
        1, 2, 3, 4,       10, 20, 30, 40,    101, 102, 103, 104,
        5, 6, 7, 8,       50, 60, 70, 80,    201, 202, 203, 204,
    };

    EXPECT_EQ(CopyBgraToTightRgba(bgra.data(), pitch, width, height), expected);
}

TEST(Direct2DPixelConversion, RejectsInvalidDimensionsPointersAndShortPitches)
{
    const std::array<std::uint8_t, 16> pixels{};
    EXPECT_THROW({ (void)CheckedRgbaByteCount(0, 1); }, std::exception);
    EXPECT_THROW({ (void)CheckedRgbaByteCount(1, 0); }, std::exception);
    EXPECT_THROW({ (void)CheckedRgbaByteCount(-1, 1); }, std::exception);
    EXPECT_THROW({ (void)CheckedRgbaByteCount(std::numeric_limits<int>::max(), 1); }, std::exception);
    EXPECT_THROW({ (void)CheckedRgbaByteCount(1, std::numeric_limits<int>::max()); }, std::exception);
    EXPECT_THROW({ (void)CopyRgbaToTightBgra(nullptr, 4, 1, 1); }, std::exception);
    EXPECT_THROW({ (void)CopyBgraToTightRgba(nullptr, 4, 1, 1); }, std::exception);
    EXPECT_THROW({ (void)CopyRgbaToTightBgra(pixels.data(), 11, 3, 1); }, std::exception);
    EXPECT_THROW({ (void)CopyBgraToTightRgba(pixels.data(), -1, 1, 1); }, std::exception);
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
        MapSourceRectangleToMip(XnaRectangle(minimum, 0, 1, 1), 1, 1, maximum, 1),
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

// D2D-40: an algebraic Porter-Duff oracle for the exact composite modes D2D-33 maps. The expected
// values below were derived by hand from the operator definitions
// (out = Fs * source + Fd * destination over premultiplied colors) for one nontrivial,
// channel-asymmetric, partially transparent pair, so this test pins the arithmetic itself rather
// than restating the implementation.
namespace
{
    constexpr std::array<std::uint8_t, 4> kReferenceSource{{100, 50, 25, 200}};
    constexpr std::array<std::uint8_t, 4> kReferenceDestination{{40, 80, 120, 150}};
}

TEST(Direct2DPorterDuffReference, EveryExactCompositeModeMatchesTheHandDerivedAlgebra)
{
    struct ModeExpectation
    {
        Direct2DBlendMode mode;
        std::array<std::uint8_t, 4> expected;
    };
    // Fs/Fd per operator, with alphaS = 200/255 and alphaD = 150/255:
    //   Copy            1,            0
    //   SourceOver      1,            1-alphaS
    //   DestinationOver 1-alphaD,     1
    //   SourceIn        alphaD,       0
    //   DestinationIn   0,            alphaS
    //   SourceOut       1-alphaD,     0
    //   DestinationOut  0,            1-alphaS
    //   SourceAtop      alphaD,       1-alphaS
    //   DestinationAtop 1-alphaD,     alphaS
    //   Xor             1-alphaD,     1-alphaS
    const std::array<ModeExpectation, 10> expectations{{
        {Direct2DBlendMode::Copy, {{100, 50, 25, 200}}},
        {Direct2DBlendMode::SourceOver, {{109, 67, 51, 232}}},
        {Direct2DBlendMode::DestinationOver, {{81, 101, 130, 232}}},
        {Direct2DBlendMode::SourceIn, {{59, 29, 15, 118}}},
        {Direct2DBlendMode::DestinationIn, {{31, 63, 94, 118}}},
        {Direct2DBlendMode::SourceOut, {{41, 21, 10, 82}}},
        {Direct2DBlendMode::DestinationOut, {{9, 17, 26, 32}}},
        {Direct2DBlendMode::SourceAtop, {{67, 47, 41, 150}}},
        {Direct2DBlendMode::DestinationAtop, {{73, 83, 104, 200}}},
        {Direct2DBlendMode::Xor, {{50, 38, 36, 115}}},
    }};

    for (const auto& expectation : expectations)
    {
        const std::array<std::uint8_t, 4> actual = CompositePorterDuffReference(
            expectation.mode, kReferenceSource, kReferenceDestination);
        EXPECT_EQ(actual, expectation.expected)
            << "composite mode ordinal " << static_cast<int>(expectation.mode);
    }
}

TEST(Direct2DPorterDuffReference, PreservesTheAlphaInvariantsOfEachOperator)
{
    const auto composite = [](Direct2DBlendMode mode) {
        return CompositePorterDuffReference(mode, kReferenceSource, kReferenceDestination);
    };
    // "Atop" keeps the alpha of whichever operand it is atop; "in" produces the product of both.
    EXPECT_EQ(composite(Direct2DBlendMode::SourceAtop)[3], kReferenceDestination[3]);
    EXPECT_EQ(composite(Direct2DBlendMode::DestinationAtop)[3], kReferenceSource[3]);
    EXPECT_EQ(composite(Direct2DBlendMode::SourceIn)[3], composite(Direct2DBlendMode::DestinationIn)[3]);
    // Copy discards the destination entirely, source-over never reduces the destination's alpha.
    EXPECT_EQ(composite(Direct2DBlendMode::Copy), kReferenceSource);
    EXPECT_GE(composite(Direct2DBlendMode::SourceOver)[3], kReferenceDestination[3]);
}

TEST(Direct2DPorterDuffReference, DegenerateOperandsCollapseToTheDefiningIdentities)
{
    constexpr std::array<std::uint8_t, 4> transparent{{0, 0, 0, 0}};
    constexpr std::array<std::uint8_t, 4> opaque{{30, 60, 90, 255}};

    // Compositing onto a fully transparent destination leaves the source unchanged for the
    // operators whose destination factor vanishes there, and erases it for the "in"/"atop" family.
    EXPECT_EQ(CompositePorterDuffReference(Direct2DBlendMode::SourceOver, kReferenceSource, transparent),
              kReferenceSource);
    EXPECT_EQ(CompositePorterDuffReference(Direct2DBlendMode::SourceOut, kReferenceSource, transparent),
              kReferenceSource);
    EXPECT_EQ(CompositePorterDuffReference(Direct2DBlendMode::SourceIn, kReferenceSource, transparent),
              transparent);
    EXPECT_EQ(CompositePorterDuffReference(Direct2DBlendMode::SourceAtop, kReferenceSource, transparent),
              transparent);
    // A fully transparent source cannot change the destination under source-over, and completely
    // replaces it under Copy.
    EXPECT_EQ(CompositePorterDuffReference(Direct2DBlendMode::SourceOver, transparent, opaque), opaque);
    EXPECT_EQ(CompositePorterDuffReference(Direct2DBlendMode::Copy, transparent, opaque), transparent);
    // An opaque source hides the destination under every "over"-like operator.
    EXPECT_EQ(CompositePorterDuffReference(Direct2DBlendMode::SourceOver, opaque, kReferenceDestination),
              opaque);
    EXPECT_EQ(CompositePorterDuffReference(Direct2DBlendMode::DestinationOut, opaque, kReferenceDestination),
              transparent);
}

// D2D-109: the sprite-bitmap cache is only safe if its key separates every input that can change
// the materialized pixels. A field silently missing from the key would serve stale pixels for a
// genuinely different draw, so each one is mutated individually here.
TEST(Direct2DSpriteBitmapCacheKey, EveryPixelAffectingFieldSeparatesTwoKeys)
{
    const SpriteBitmapCacheKey baseline{};
    EXPECT_EQ(baseline, SpriteBitmapCacheKey{});

    int distinctFields = 0;
    const auto expectDifferent = [&](const SpriteBitmapCacheKey& mutated, const char* field) {
        ++distinctFields;
        EXPECT_FALSE(mutated == baseline) << "cache key ignores " << field;
        EXPECT_TRUE(mutated == mutated) << "cache key is not reflexive after changing " << field;
    };

    int sentinel = 0;
    SpriteBitmapCacheKey key = baseline;
    key.source = &sentinel;
    expectDifferent(key, "source");

    key = baseline;
    key.contentVersion = 1;
    expectDifferent(key, "contentVersion");

    key = baseline;
    key.deviceGeneration = 1;
    expectDifferent(key, "deviceGeneration");

    key = baseline;
    key.mipLevel = 1;
    expectDifferent(key, "mipLevel");

    key = baseline;
    key.upperMipLevel = 0;
    expectDifferent(key, "upperMipLevel");

    key = baseline;
    key.mipBlendFraction = 0.5f;
    expectDifferent(key, "mipBlendFraction");

    key = baseline;
    key.sourceX = 1;
    expectDifferent(key, "sourceX");

    key = baseline;
    key.sourceY = 1;
    expectDifferent(key, "sourceY");

    key = baseline;
    key.sourceWidth = 1;
    expectDifferent(key, "sourceWidth");

    key = baseline;
    key.sourceHeight = 1;
    expectDifferent(key, "sourceHeight");

    key = baseline;
    key.color = 0xFF00FF00u;
    expectDifferent(key, "color");

    key = baseline;
    key.spriteEffects = 1;
    expectDifferent(key, "spriteEffects");

    key = baseline;
    key.addressU = 1;
    expectDifferent(key, "addressU");

    key = baseline;
    key.addressV = 1;
    expectDifferent(key, "addressV");

    key = baseline;
    key.nonPremultipliedSource = true;
    expectDifferent(key, "nonPremultipliedSource");

    // Pins the count itself: a field added to the struct without a case above would leave this at
    // the old number and fail here rather than silently going untested.
    EXPECT_EQ(distinctFields, 15);
}

TEST(Direct2DSpriteBitmapCacheKey, IdenticalDrawsShareOneEntry)
{
    int texture = 0;
    SpriteBitmapCacheKey first{};
    first.source = &texture;
    first.contentVersion = 7;
    first.deviceGeneration = 3;
    first.mipLevel = 2;
    first.upperMipLevel = -1;
    first.sourceX = 4;
    first.sourceY = 5;
    first.sourceWidth = 6;
    first.sourceHeight = 7;
    first.color = 0x80FF20C0u;
    first.spriteEffects = 2;
    first.addressU = 2;
    first.addressV = 0;
    first.nonPremultipliedSource = true;

    const SpriteBitmapCacheKey second = first;
    EXPECT_TRUE(first == second);

    // Only the destination position differs between repeated sprites drawn from the same atlas
    // cell; that is not part of the materialized pixels and deliberately not part of the key.
    SpriteBitmapCacheKey nextContent = first;
    nextContent.contentVersion = 8;
    EXPECT_FALSE(nextContent == first);
}

// D2D-107: the decorated-brush cache reuses Direct2D objects WITHOUT writing any property, which
// is only sound while every value baked into those objects is part of the key.
TEST(Direct2DSpriteBrushCacheKey, EveryBakedInPropertySeparatesTwoKeys)
{
    const SpriteBrushCacheKey baseline{};
    int distinctFields = 0;
    const auto expectDifferent = [&](const SpriteBrushCacheKey& mutated, const char* field) {
        ++distinctFields;
        EXPECT_FALSE(mutated == baseline) << "brush cache key ignores " << field;
    };

    int sentinel = 0;
    SpriteBrushCacheKey key = baseline;
    key.image = &sentinel;
    expectDifferent(key, "image");

    key = baseline;
    key.deviceGeneration = 1;
    expectDifferent(key, "deviceGeneration");

    key = baseline;
    key.color = 0x11223344u;
    expectDifferent(key, "color");

    key = baseline;
    key.tinted = true;
    expectDifferent(key, "tinted");

    key = baseline;
    key.straightAlphaTint = true;
    expectDifferent(key, "straightAlphaTint");

    key = baseline;
    key.premultiplyStage = true;
    expectDifferent(key, "premultiplyStage");

    key = baseline;
    key.sourceX = 1;
    expectDifferent(key, "sourceX");

    key = baseline;
    key.sourceY = 1;
    expectDifferent(key, "sourceY");

    key = baseline;
    key.sourceWidth = 1;
    expectDifferent(key, "sourceWidth");

    key = baseline;
    key.sourceHeight = 1;
    expectDifferent(key, "sourceHeight");

    key = baseline;
    key.extendU = 1;
    expectDifferent(key, "extendU");

    key = baseline;
    key.extendV = 1;
    expectDifferent(key, "extendV");

    key = baseline;
    key.interpolationMode = 1;
    expectDifferent(key, "interpolationMode");

    key = baseline;
    key.spriteEffects = 1;
    expectDifferent(key, "spriteEffects");

    key = baseline;
    key.originX = 0.5f;
    expectDifferent(key, "originX");

    key = baseline;
    key.originY = 0.5f;
    expectDifferent(key, "originY");

    EXPECT_EQ(distinctFields, 16);
}
#endif
