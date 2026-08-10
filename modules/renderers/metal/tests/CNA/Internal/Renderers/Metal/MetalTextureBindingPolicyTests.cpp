// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>

#include "CNA/Internal/Renderers/Metal/MetalTextureBindingPolicy.hpp"

using namespace CNA::Internal::Renderers::Metal;

TEST(MetalTextureBindingPolicy, BindsCapturedNativeResource)
{
    EXPECT_EQ(DescribeMetalTextureBinding(true, true, true),
              MetalTextureBindingDecision::BindNative);
    EXPECT_EQ(DescribeMetalTextureBinding(true, true, false),
              MetalTextureBindingDecision::BindNative);
}

TEST(MetalTextureBindingPolicy, MissingDiffuseAndSecond2DTextureUseNeutralWhite)
{
    EXPECT_EQ(DescribeMetalTextureBinding(false, false, true),
              MetalTextureBindingDecision::BindNeutralFallback);
}

TEST(MetalTextureBindingPolicy, MissingEnvironmentCubeUsesOwnedNeutralWhiteCube)
{
    EXPECT_EQ(DescribeMetalTextureBinding(false, false, true),
              MetalTextureBindingDecision::BindNeutralFallback);
}

TEST(MetalTextureBindingPolicy, ForeignResourceIsRejectedInsteadOfUsingFallback)
{
    EXPECT_EQ(DescribeMetalTextureBinding(true, false, true),
              MetalTextureBindingDecision::Reject);
    EXPECT_EQ(DescribeMetalTextureBinding(true, false, false),
              MetalTextureBindingDecision::Reject);
}

TEST(MetalTextureBindingPolicy, MissingDrawAfterNativeDrawNeverReusesCarriedSlot)
{
    const MetalTextureBindingDecision sequence[] = {
        DescribeMetalTextureBinding(true, true, true),
        DescribeMetalTextureBinding(false, false, true),
        DescribeMetalTextureBinding(true, true, true),
        DescribeMetalTextureBinding(false, false, true),
    };
    EXPECT_EQ(sequence[0], MetalTextureBindingDecision::BindNative);
    EXPECT_EQ(sequence[1], MetalTextureBindingDecision::BindNeutralFallback);
    EXPECT_EQ(sequence[2], MetalTextureBindingDecision::BindNative);
    EXPECT_EQ(sequence[3], MetalTextureBindingDecision::BindNeutralFallback);
}

TEST(MetalTextureBindingPolicy, EveryStockNullSlotHasTheRequiredNeutralResource)
{
    struct Expected
    {
        MetalStockTextureSlot slot;
        MetalNeutralTextureKind neutral;
    };
    const Expected expected[] = {
        {MetalStockTextureSlot::BasicDiffuse, MetalNeutralTextureKind::White2D},
        {MetalStockTextureSlot::AlphaTestDiffuse, MetalNeutralTextureKind::White2D},
        {MetalStockTextureSlot::DualFirst, MetalNeutralTextureKind::White2D},
        {MetalStockTextureSlot::DualSecond, MetalNeutralTextureKind::White2D},
        {MetalStockTextureSlot::EnvironmentDiffuse, MetalNeutralTextureKind::White2D},
        {MetalStockTextureSlot::EnvironmentCube, MetalNeutralTextureKind::WhiteCube},
        {MetalStockTextureSlot::SkinnedDiffuse, MetalNeutralTextureKind::White2D},
        {MetalStockTextureSlot::PbrBaseColor, MetalNeutralTextureKind::White2D},
        {MetalStockTextureSlot::PbrNormal, MetalNeutralTextureKind::FlatNormal2D},
        {MetalStockTextureSlot::PbrMetallicRoughness, MetalNeutralTextureKind::White2D},
        {MetalStockTextureSlot::PbrEmissive, MetalNeutralTextureKind::White2D},
        {MetalStockTextureSlot::PbrOcclusion, MetalNeutralTextureKind::White2D},
    };
    for (const Expected& item : expected)
    {
        EXPECT_EQ(MetalNeutralTextureForSlot(item.slot), item.neutral);
        EXPECT_EQ(DescribeMetalStockTextureBinding(item.slot, false, false),
                  MetalTextureBindingDecision::BindNeutralFallback);
        EXPECT_EQ(DescribeMetalStockTextureBinding(item.slot, true, false),
                  MetalTextureBindingDecision::Reject);
    }
}

TEST(MetalTextureBindingPolicy, BasicAlphaDualAndEnvironmentTransitionsNeverCarryPriorNativeState)
{
    const MetalStockTextureSlot slots[] = {
        MetalStockTextureSlot::BasicDiffuse,
        MetalStockTextureSlot::AlphaTestDiffuse,
        MetalStockTextureSlot::DualFirst,
        MetalStockTextureSlot::DualSecond,
        MetalStockTextureSlot::EnvironmentDiffuse,
        MetalStockTextureSlot::EnvironmentCube,
    };
    for (const MetalStockTextureSlot slot : slots)
    {
        EXPECT_EQ(DescribeMetalStockTextureBinding(slot, true, true),
                  MetalTextureBindingDecision::BindNative);
        EXPECT_EQ(DescribeMetalStockTextureBinding(slot, false, false),
                  MetalTextureBindingDecision::BindNeutralFallback);
    }
}
