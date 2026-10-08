// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>

#include "CNA/Internal/Renderers/Metal/MetalDepthPolicy.hpp"

using CNA::Internal::Renderers::Metal::MetalAppliedRenderTargetDepthFormat;
using CNA::Internal::Renderers::Metal::MetalDepthFormatHasDepth;
using CNA::Internal::Renderers::Metal::MetalDepthFormatHasStencil;
using CNA::Internal::Renderers::Metal::MetalDepthStorage;
using CNA::Internal::Renderers::Metal::MetalDepthStorageFor;
using CNA::Internal::Renderers::Metal::MetalEffectiveDepthWriteEnabled;
using Microsoft::Xna::Framework::Graphics::DepthFormat;

TEST(MetalDepthPolicy, DisabledDepthSuppressesStorageEvenWhenWriteWasRequested)
{
    EXPECT_FALSE(MetalEffectiveDepthWriteEnabled(false, true));
    EXPECT_FALSE(MetalEffectiveDepthWriteEnabled(false, false));
}

TEST(MetalDepthPolicy, ReenableRestoresRetainedWriteRequest)
{
    const bool requestedWrite = true;
    EXPECT_FALSE(MetalEffectiveDepthWriteEnabled(false, requestedWrite));
    EXPECT_TRUE(MetalEffectiveDepthWriteEnabled(true, requestedWrite));
}

TEST(MetalDepthPolicy, EnabledDepthHonorsExplicitWriteDisable)
{
    EXPECT_FALSE(MetalEffectiveDepthWriteEnabled(true, false));
}

// plans/plan_apple_m4.md AM4-032: what a render target exposes follows the DepthFormat it was
// created with, although every Metal target stores a Depth32Float_Stencil8 attachment.
TEST(MetalDepthPolicy, NoneTargetReportsNoneAndHasNoPlanes)
{
    const int applied = MetalAppliedRenderTargetDepthFormat(static_cast<int>(DepthFormat::None));
    EXPECT_EQ(applied, static_cast<int>(DepthFormat::None));
    EXPECT_FALSE(MetalDepthFormatHasDepth(applied));
    EXPECT_FALSE(MetalDepthFormatHasStencil(applied));
}

TEST(MetalDepthPolicy, Depth24TargetHasDepthButNoStencil)
{
    const int applied = MetalAppliedRenderTargetDepthFormat(static_cast<int>(DepthFormat::Depth24));
    EXPECT_EQ(applied, static_cast<int>(DepthFormat::Depth24));
    EXPECT_TRUE(MetalDepthFormatHasDepth(applied));
    EXPECT_FALSE(MetalDepthFormatHasStencil(applied));
}

// plans/plan_apple_m4.md AM4-151: Depth16 is stored as 16-bit depth, so it is reported as itself.
TEST(MetalDepthPolicy, Depth16IsStoredAndReportedAsItself)
{
    const int applied = MetalAppliedRenderTargetDepthFormat(static_cast<int>(DepthFormat::Depth16));
    EXPECT_EQ(applied, static_cast<int>(DepthFormat::Depth16));
    EXPECT_TRUE(MetalDepthFormatHasDepth(applied));
    EXPECT_FALSE(MetalDepthFormatHasStencil(applied));
    EXPECT_EQ(MetalDepthStorageFor(applied), MetalDepthStorage::Depth16Unorm);
}

TEST(MetalDepthPolicy, EveryOtherDepthFormatKeepsTheFloatDepthStencilStorage)
{
    for (const DepthFormat format : {DepthFormat::None, DepthFormat::Depth24, DepthFormat::Depth24Stencil8})
        EXPECT_EQ(MetalDepthStorageFor(MetalAppliedRenderTargetDepthFormat(static_cast<int>(format))),
                  MetalDepthStorage::Depth32FloatStencil8)
            << static_cast<int>(format);
}

TEST(MetalDepthPolicy, Depth24Stencil8TargetHasBothPlanes)
{
    const int applied =
        MetalAppliedRenderTargetDepthFormat(static_cast<int>(DepthFormat::Depth24Stencil8));
    EXPECT_EQ(applied, static_cast<int>(DepthFormat::Depth24Stencil8));
    EXPECT_TRUE(MetalDepthFormatHasDepth(applied));
    EXPECT_TRUE(MetalDepthFormatHasStencil(applied));
}
