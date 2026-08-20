// SPDX-License-Identifier: MS-PL
//
// plans/plan_metal.md METAL-34-style extraction: ComputeMetalPrimitiveVertexCount() only reads a plain
// XNA framework enum and plain ints -- zero Objective-C/Metal-framework dependency, so it compiles
// and runs on any platform CnaTests already builds for, no #if defined(CNA_RENDERER_METAL) gate.
//
// plans/plan_metal.md METAL-13: PointListEXT previously fell through to the *3 (triangle) default -- a
// real, previously-shipping bug, already fixed in the Metal renderer's own source. This test locks
// that fix in against a future regression, and cross-checks every formula against
// EasyGLRenderer's own already-tested equivalent switch (src/CNA/Internal/Renderers/EasyGL/
// EasyGLRenderer.cpp), not just re-derived from memory.
#include <gtest/gtest.h>
#include "CNA/Internal/Renderers/Metal/MetalPrimitiveVertexCount.hpp"

using namespace CNA::Internal::Renderers::Metal;
using Microsoft::Xna::Framework::Graphics::PrimitiveType;

TEST(MetalPrimitiveVertexCount, TriangleListIsThreeTimesPrimitiveCount)
{
    EXPECT_EQ(ComputeMetalPrimitiveVertexCount(PrimitiveType::TriangleList, 1), 3);
    EXPECT_EQ(ComputeMetalPrimitiveVertexCount(PrimitiveType::TriangleList, 5), 15);
    EXPECT_EQ(ComputeMetalPrimitiveVertexCount(PrimitiveType::TriangleList, 0), 0);
}

TEST(MetalPrimitiveVertexCount, TriangleStripIsPrimitiveCountPlusTwo)
{
    // N connected triangles in a strip share vertices pairwise, needing only N+2 total, not 3N.
    EXPECT_EQ(ComputeMetalPrimitiveVertexCount(PrimitiveType::TriangleStrip, 1), 3);
    EXPECT_EQ(ComputeMetalPrimitiveVertexCount(PrimitiveType::TriangleStrip, 2), 4);
    EXPECT_EQ(ComputeMetalPrimitiveVertexCount(PrimitiveType::TriangleStrip, 10), 12);
}

TEST(MetalPrimitiveVertexCount, LineListIsTwoTimesPrimitiveCount)
{
    EXPECT_EQ(ComputeMetalPrimitiveVertexCount(PrimitiveType::LineList, 1), 2);
    EXPECT_EQ(ComputeMetalPrimitiveVertexCount(PrimitiveType::LineList, 4), 8);
}

TEST(MetalPrimitiveVertexCount, LineStripIsPrimitiveCountPlusOne)
{
    EXPECT_EQ(ComputeMetalPrimitiveVertexCount(PrimitiveType::LineStrip, 1), 2);
    EXPECT_EQ(ComputeMetalPrimitiveVertexCount(PrimitiveType::LineStrip, 9), 10);
}

TEST(MetalPrimitiveVertexCount, PointListEXTIsExactlyPrimitiveCount)
{
    // METAL-13's own real bug: this previously fell through to the *3 (triangle) default instead
    // of returning count unchanged. A point primitive needs exactly one vertex per point -- no
    // sharing, no extra vertices, unlike every strip/list-of-multi-vertex-primitive case above.
    EXPECT_EQ(ComputeMetalPrimitiveVertexCount(PrimitiveType::PointListEXT, 1), 1);
    EXPECT_EQ(ComputeMetalPrimitiveVertexCount(PrimitiveType::PointListEXT, 7), 7);
    EXPECT_EQ(ComputeMetalPrimitiveVertexCount(PrimitiveType::PointListEXT, 0), 0);
}
