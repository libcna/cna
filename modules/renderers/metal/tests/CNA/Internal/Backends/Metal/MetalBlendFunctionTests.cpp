// SPDX-License-Identifier: MS-PL
//
// plan_metal.md METAL-19: DescribeMetalBlendOperation() only reads a plain XNA BlendFunction
// ordinal and returns a plain C++ enum -- zero Objective-C dependency, genuinely unit-tested on
// this Linux machine. No #if defined(CNA_BACKEND_METAL) gate, deliberately.
#include <gtest/gtest.h>
#include "CNA/Internal/Backends/Metal/MetalBlendFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendFunction.hpp"

using namespace CNA::Internal::Backends::Metal;
using BF = Microsoft::Xna::Framework::Graphics::BlendFunction;

TEST(MetalBlendFunction, AddMapsToAdd)
{
    EXPECT_EQ(DescribeMetalBlendOperation(static_cast<int>(BF::Add)), MetalBlendOperationKind::Add);
}

TEST(MetalBlendFunction, SubtractMapsToSubtract)
{
    EXPECT_EQ(DescribeMetalBlendOperation(static_cast<int>(BF::Subtract)), MetalBlendOperationKind::Subtract);
}

TEST(MetalBlendFunction, ReverseSubtractMapsToReverseSubtract)
{
    EXPECT_EQ(DescribeMetalBlendOperation(static_cast<int>(BF::ReverseSubtract)), MetalBlendOperationKind::ReverseSubtract);
}

TEST(MetalBlendFunction, MaxMapsToMax)
{
    EXPECT_EQ(DescribeMetalBlendOperation(static_cast<int>(BF::Max)), MetalBlendOperationKind::Max);
}

TEST(MetalBlendFunction, MinMapsToMin)
{
    EXPECT_EQ(DescribeMetalBlendOperation(static_cast<int>(BF::Min)), MetalBlendOperationKind::Min);
}

TEST(MetalBlendFunction, UnknownOrdinalDefaultsToAdd)
{
    EXPECT_EQ(DescribeMetalBlendOperation(999), MetalBlendOperationKind::Add);
}
