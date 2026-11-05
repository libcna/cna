// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "CNA/Internal/Graphics/VertexDeclarationFidelity.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"

using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Graphics;

// The guard's existing R3 contract is mathematical containment, not the Refresh
// constructor's wrapped-end comparison: [2147483644,2147483660) is outside [0,16).
TEST(VertexDeclarationFidelityArithmeticTest, WrappedConstructorExtentIsRefusedByRendererGuard)
{
    const VertexDeclaration declaration(16, {
        VertexElement(2147483644, VertexElementFormat::Vector4, VertexElementUsage::Position, 0)});
    EXPECT_EQ(DescribeUnrepresentableVertexDeclaration(
        declaration.GetVertexElements(), 16, 16,
        InferredLayoutForStride(16, UnlistedStrideLayout::RendererRefusesIt)),
        "declared element Position0@2147483644 Vector4 does not fit inside the declared 16-byte record");
}

// R4 compares the still-unvisited second range before R3 examines that element.
// [0,12) and [2147483644,2147483656) are disjoint; then R3 must refuse Normal0.
TEST(VertexDeclarationFidelityArithmeticTest, LaterWrappedExtentDoesNotOverflowOverlapLookahead)
{
    const VertexDeclaration declaration(16, {
        VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        VertexElement(2147483644, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0)});
    EXPECT_EQ(DescribeUnrepresentableVertexDeclaration(
        declaration.GetVertexElements(), 16, 16,
        InferredLayoutForStride(16, UnlistedStrideLayout::RendererRefusesIt)),
        "declared element Normal0@2147483644 Vector3 does not fit inside the declared 16-byte record");
}

// Hand-derived half-open intersections, endpoints evaluated as mathematical integers.
// These internal range checks isolate both additions and also test symmetry/touching.
TEST(VertexDeclarationFidelityArithmeticTest, HighHalfOpenRangesOverlapWithoutSignedOverflow)
{
    using VertexDeclarationFidelityDetail::RangesOverlap;
    // [2147483640,2147483648) intersects [2147483644,2147483660).
    EXPECT_TRUE(RangesOverlap(2147483640, 8, 2147483644, 16));
    EXPECT_TRUE(RangesOverlap(2147483644, 16, 2147483640, 8));
    // [2147483600,2147483604) is disjoint from the high range.
    EXPECT_FALSE(RangesOverlap(2147483600, 4, 2147483644, 16));
    EXPECT_FALSE(RangesOverlap(2147483644, 16, 2147483600, 4));
    // Adjacent ranges meet at 2147483644, but share no byte.
    EXPECT_FALSE(RangesOverlap(2147483640, 4, 2147483644, 16));
    EXPECT_FALSE(RangesOverlap(2147483644, 16, 2147483640, 4));
}
