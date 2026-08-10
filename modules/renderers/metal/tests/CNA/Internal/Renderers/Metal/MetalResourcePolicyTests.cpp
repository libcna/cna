// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>

#include <climits>
#include <cstddef>
#include <limits>

#include "CNA/Internal/Renderers/Metal/MetalResourcePolicy.hpp"

using CNA::Internal::Renderers::Metal::TryMetalBufferByteCount;

TEST(MetalResourcePolicy, ComputesSupportedVertexAndIndexBufferSizes)
{
    std::size_t bytes = 0;
    EXPECT_TRUE(TryMetalBufferByteCount(3, 16, bytes));
    EXPECT_EQ(bytes, 48u);
    EXPECT_TRUE(TryMetalBufferByteCount(6, 2, bytes));
    EXPECT_EQ(bytes, 12u);
    EXPECT_TRUE(TryMetalBufferByteCount(6, 4, bytes));
    EXPECT_EQ(bytes, 24u);
}

TEST(MetalResourcePolicy, RejectsZeroNegativeAndOverflowWithoutChangingOutput)
{
    std::size_t bytes = 123;
    EXPECT_FALSE(TryMetalBufferByteCount(0, 4, bytes));
    EXPECT_FALSE(TryMetalBufferByteCount(-1, 4, bytes));
    EXPECT_FALSE(TryMetalBufferByteCount(1, 0, bytes));
    EXPECT_FALSE(TryMetalBufferByteCount(
        INT_MAX, std::numeric_limits<std::size_t>::max(), bytes));
    EXPECT_EQ(bytes, 123u);
}
