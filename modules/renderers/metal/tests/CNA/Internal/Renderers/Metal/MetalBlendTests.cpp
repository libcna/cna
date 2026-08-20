// SPDX-License-Identifier: MS-PL
//
// plans/plan_metal.md METAL-19: DescribeMetalBlendFactor() only reads a plain XNA Blend ordinal and
// returns a plain C++ enum -- zero Objective-C dependency, genuinely unit-tested on this Linux
// machine. No #if defined(CNA_RENDERER_METAL) gate, deliberately.
#include <gtest/gtest.h>
#include "CNA/Internal/Renderers/Metal/MetalBlend.hpp"
#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"

using namespace CNA::Internal::Renderers::Metal;
using B = Microsoft::Xna::Framework::Graphics::Blend;

TEST(MetalBlend, OneMapsToOne)
{
    EXPECT_EQ(DescribeMetalBlendFactor(static_cast<int>(B::One)), MetalBlendFactorKind::One);
}

TEST(MetalBlend, ZeroMapsToZero)
{
    EXPECT_EQ(DescribeMetalBlendFactor(static_cast<int>(B::Zero)), MetalBlendFactorKind::Zero);
}

TEST(MetalBlend, SourceColorMapsToSourceColor)
{
    EXPECT_EQ(DescribeMetalBlendFactor(static_cast<int>(B::SourceColor)), MetalBlendFactorKind::SourceColor);
}

TEST(MetalBlend, InverseSourceColorMapsToOneMinusSourceColor)
{
    EXPECT_EQ(DescribeMetalBlendFactor(static_cast<int>(B::InverseSourceColor)), MetalBlendFactorKind::OneMinusSourceColor);
}

TEST(MetalBlend, SourceAlphaMapsToSourceAlpha)
{
    EXPECT_EQ(DescribeMetalBlendFactor(static_cast<int>(B::SourceAlpha)), MetalBlendFactorKind::SourceAlpha);
}

TEST(MetalBlend, InverseSourceAlphaMapsToOneMinusSourceAlpha)
{
    EXPECT_EQ(DescribeMetalBlendFactor(static_cast<int>(B::InverseSourceAlpha)), MetalBlendFactorKind::OneMinusSourceAlpha);
}

TEST(MetalBlend, DestinationColorMapsToDestinationColor)
{
    EXPECT_EQ(DescribeMetalBlendFactor(static_cast<int>(B::DestinationColor)), MetalBlendFactorKind::DestinationColor);
}

TEST(MetalBlend, InverseDestinationColorMapsToOneMinusDestinationColor)
{
    EXPECT_EQ(DescribeMetalBlendFactor(static_cast<int>(B::InverseDestinationColor)), MetalBlendFactorKind::OneMinusDestinationColor);
}

TEST(MetalBlend, DestinationAlphaMapsToDestinationAlpha)
{
    EXPECT_EQ(DescribeMetalBlendFactor(static_cast<int>(B::DestinationAlpha)), MetalBlendFactorKind::DestinationAlpha);
}

TEST(MetalBlend, InverseDestinationAlphaMapsToOneMinusDestinationAlpha)
{
    EXPECT_EQ(DescribeMetalBlendFactor(static_cast<int>(B::InverseDestinationAlpha)), MetalBlendFactorKind::OneMinusDestinationAlpha);
}

TEST(MetalBlend, BlendFactorMapsToBlendColor)
{
    EXPECT_EQ(DescribeMetalBlendFactor(static_cast<int>(B::BlendFactor)), MetalBlendFactorKind::BlendColor);
}

TEST(MetalBlend, InverseBlendFactorMapsToOneMinusBlendColor)
{
    EXPECT_EQ(DescribeMetalBlendFactor(static_cast<int>(B::InverseBlendFactor)), MetalBlendFactorKind::OneMinusBlendColor);
}

TEST(MetalBlend, SourceAlphaSaturationMapsToSourceAlphaSaturated)
{
    EXPECT_EQ(DescribeMetalBlendFactor(static_cast<int>(B::SourceAlphaSaturation)), MetalBlendFactorKind::SourceAlphaSaturated);
}

TEST(MetalBlend, UnknownOrdinalDefaultsToOne)
{
    EXPECT_EQ(DescribeMetalBlendFactor(999), MetalBlendFactorKind::One);
}
