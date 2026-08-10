// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>

#include "CNA/Internal/Renderers/Metal/MetalDepthPolicy.hpp"

using CNA::Internal::Renderers::Metal::MetalEffectiveDepthWriteEnabled;

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
