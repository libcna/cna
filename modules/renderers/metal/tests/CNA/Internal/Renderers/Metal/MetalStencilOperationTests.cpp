// SPDX-License-Identifier: MS-PL
//
// plans/plan_metal.md METAL-19: DescribeMetalStencilOperation() only reads a plain XNA StencilOperation
// ordinal and returns a plain C++ enum -- zero Objective-C dependency, genuinely unit-tested on
// this Linux machine. No #if defined(CNA_RENDERER_METAL) gate, deliberately.
#include <gtest/gtest.h>
#include "CNA/Internal/Renderers/Metal/MetalStencilOperation.hpp"
#include "Microsoft/Xna/Framework/Graphics/StencilOperation.hpp"

using namespace CNA::Internal::Renderers::Metal;
using SO = Microsoft::Xna::Framework::Graphics::StencilOperation;

TEST(MetalStencilOperation, KeepMapsToKeep)
{
    EXPECT_EQ(DescribeMetalStencilOperation(static_cast<int>(SO::Keep)), MetalStencilOperationKind::Keep);
}

TEST(MetalStencilOperation, ZeroMapsToZero)
{
    EXPECT_EQ(DescribeMetalStencilOperation(static_cast<int>(SO::Zero)), MetalStencilOperationKind::Zero);
}

TEST(MetalStencilOperation, ReplaceMapsToReplace)
{
    EXPECT_EQ(DescribeMetalStencilOperation(static_cast<int>(SO::Replace)), MetalStencilOperationKind::Replace);
}

TEST(MetalStencilOperation, IncrementMapsToIncrementWrap)
{
    // XNA's Increment wraps (D3DSTENCILOP_INCR), matching Metal's IncrementWrap, not IncrementClamp.
    EXPECT_EQ(DescribeMetalStencilOperation(static_cast<int>(SO::Increment)), MetalStencilOperationKind::IncrementWrap);
}

TEST(MetalStencilOperation, DecrementMapsToDecrementWrap)
{
    EXPECT_EQ(DescribeMetalStencilOperation(static_cast<int>(SO::Decrement)), MetalStencilOperationKind::DecrementWrap);
}

TEST(MetalStencilOperation, IncrementSaturationMapsToIncrementClamp)
{
    // XNA's IncrementSaturation clamps (D3DSTENCILOP_INCRSAT), matching Metal's IncrementClamp.
    EXPECT_EQ(DescribeMetalStencilOperation(static_cast<int>(SO::IncrementSaturation)), MetalStencilOperationKind::IncrementClamp);
}

TEST(MetalStencilOperation, DecrementSaturationMapsToDecrementClamp)
{
    EXPECT_EQ(DescribeMetalStencilOperation(static_cast<int>(SO::DecrementSaturation)), MetalStencilOperationKind::DecrementClamp);
}

TEST(MetalStencilOperation, InvertMapsToInvert)
{
    EXPECT_EQ(DescribeMetalStencilOperation(static_cast<int>(SO::Invert)), MetalStencilOperationKind::Invert);
}

TEST(MetalStencilOperation, UnknownOrdinalDefaultsToKeep)
{
    EXPECT_EQ(DescribeMetalStencilOperation(999), MetalStencilOperationKind::Keep);
}
