// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"

using Microsoft::Xna::Framework::Graphics::ClearOptions;

TEST(ClearOptionsTest, TargetValue)
{
    EXPECT_EQ(static_cast<int>(ClearOptions::Target), 1);
}

TEST(ClearOptionsTest, DepthBufferValue)
{
    EXPECT_EQ(static_cast<int>(ClearOptions::DepthBuffer), 2);
}

TEST(ClearOptionsTest, StencilValue)
{
    EXPECT_EQ(static_cast<int>(ClearOptions::Stencil), 4);
}

TEST(ClearOptionsTest, OrCombinesFlags)
{
    ClearOptions combined = ClearOptions::Target | ClearOptions::DepthBuffer;
    EXPECT_EQ(static_cast<int>(combined), 3);
}

TEST(ClearOptionsTest, AndMasksFlags)
{
    ClearOptions combined = ClearOptions::Target | ClearOptions::Stencil;
    ClearOptions masked = combined & ClearOptions::Target;
    EXPECT_EQ(masked, ClearOptions::Target);
}

TEST(ClearOptionsTest, AllThreeCombined)
{
    ClearOptions all = ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil;
    EXPECT_EQ(static_cast<int>(all), 7);
}
