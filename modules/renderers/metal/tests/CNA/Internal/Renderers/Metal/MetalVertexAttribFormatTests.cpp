// SPDX-License-Identifier: MS-PL
//
// plans/plan_metal.md METAL-14: DescribeMetalVertexElementFormat() only reads a plain XNA framework enum
// (VertexElementFormat) and returns a plain C++ struct -- zero Objective-C dependency, so this is
// genuinely unit-tested on this Linux machine without an Apple toolchain. No
// #if defined(CNA_RENDERER_METAL) gate, deliberately.
//
// Covers all 12 real VertexElementFormat values, cross-checked one-by-one against
// EasyGLRenderer::DescribeVertexElementFormat()'s own already-tested component-count column
// (that table's own source was read directly, not assumed from memory) -- and specifically checks
// that Byte4 (raw, unnormalized) and Color (normalized) map to genuinely different
// MetalVertexAttribKind values, since collapsing them would silently break XNA's real
// BLENDINDICES-vs-COLOR distinction the way an integer-vs-normalized mixup would in any graphics API.
#include <gtest/gtest.h>
#include "CNA/Internal/Renderers/Metal/MetalVertexAttribFormat.hpp"
#include <cstddef>
#include <iterator>

using namespace CNA::Internal::Renderers::Metal;
using Microsoft::Xna::Framework::Graphics::VertexElementFormat;

TEST(MetalVertexAttribFormat, SingleIsOneComponentFloat)
{
    EXPECT_EQ(DescribeMetalVertexElementFormat(VertexElementFormat::Single),
              (MetalVertexAttribFormat{MetalVertexAttribKind::Float1, 1}));
}

TEST(MetalVertexAttribFormat, Vector2IsTwoComponentFloat)
{
    EXPECT_EQ(DescribeMetalVertexElementFormat(VertexElementFormat::Vector2),
              (MetalVertexAttribFormat{MetalVertexAttribKind::Float2, 2}));
}

TEST(MetalVertexAttribFormat, Vector3IsThreeComponentFloat)
{
    EXPECT_EQ(DescribeMetalVertexElementFormat(VertexElementFormat::Vector3),
              (MetalVertexAttribFormat{MetalVertexAttribKind::Float3, 3}));
}

TEST(MetalVertexAttribFormat, Vector4IsFourComponentFloat)
{
    EXPECT_EQ(DescribeMetalVertexElementFormat(VertexElementFormat::Vector4),
              (MetalVertexAttribFormat{MetalVertexAttribKind::Float4, 4}));
}

TEST(MetalVertexAttribFormat, ColorIsNormalizedUChar4)
{
    EXPECT_EQ(DescribeMetalVertexElementFormat(VertexElementFormat::Color),
              (MetalVertexAttribFormat{MetalVertexAttribKind::UChar4Normalized, 4}));
}

TEST(MetalVertexAttribFormat, Byte4IsRawUnnormalizedUChar4)
{
    EXPECT_EQ(DescribeMetalVertexElementFormat(VertexElementFormat::Byte4),
              (MetalVertexAttribFormat{MetalVertexAttribKind::UChar4, 4}));
}

TEST(MetalVertexAttribFormat, ColorAndByte4AreDistinctKinds)
{
    // The single most important invariant this table provides: a raw-integer BLENDINDICES-style
    // Byte4 must never be silently treated as a normalized-to-[0,1] Color, or vice versa.
    const auto color = DescribeMetalVertexElementFormat(VertexElementFormat::Color);
    const auto byte4 = DescribeMetalVertexElementFormat(VertexElementFormat::Byte4);
    EXPECT_NE(color.kind, byte4.kind);
}

TEST(MetalVertexAttribFormat, Short2IsTwoComponentNonNormalizedShort)
{
    EXPECT_EQ(DescribeMetalVertexElementFormat(VertexElementFormat::Short2),
              (MetalVertexAttribFormat{MetalVertexAttribKind::Short2, 2}));
}

TEST(MetalVertexAttribFormat, Short4IsFourComponentNonNormalizedShort)
{
    EXPECT_EQ(DescribeMetalVertexElementFormat(VertexElementFormat::Short4),
              (MetalVertexAttribFormat{MetalVertexAttribKind::Short4, 4}));
}

TEST(MetalVertexAttribFormat, NormalizedShort2IsDistinctFromShort2)
{
    const auto plain = DescribeMetalVertexElementFormat(VertexElementFormat::Short2);
    const auto normalized = DescribeMetalVertexElementFormat(VertexElementFormat::NormalizedShort2);
    EXPECT_EQ(normalized, (MetalVertexAttribFormat{MetalVertexAttribKind::Short2Normalized, 2}));
    EXPECT_NE(plain.kind, normalized.kind);
}

TEST(MetalVertexAttribFormat, NormalizedShort4IsDistinctFromShort4)
{
    const auto plain = DescribeMetalVertexElementFormat(VertexElementFormat::Short4);
    const auto normalized = DescribeMetalVertexElementFormat(VertexElementFormat::NormalizedShort4);
    EXPECT_EQ(normalized, (MetalVertexAttribFormat{MetalVertexAttribKind::Short4Normalized, 4}));
    EXPECT_NE(plain.kind, normalized.kind);
}

TEST(MetalVertexAttribFormat, HalfVector2IsTwoComponentHalf)
{
    EXPECT_EQ(DescribeMetalVertexElementFormat(VertexElementFormat::HalfVector2),
              (MetalVertexAttribFormat{MetalVertexAttribKind::Half2, 2}));
}

TEST(MetalVertexAttribFormat, HalfVector4IsFourComponentHalf)
{
    EXPECT_EQ(DescribeMetalVertexElementFormat(VertexElementFormat::HalfVector4),
              (MetalVertexAttribFormat{MetalVertexAttribKind::Half4, 4}));
}

TEST(MetalVertexAttribFormat, AllTwelveRealFormatsProduceDistinctKinds)
{
    // Every one of the 12 real VertexElementFormat values must map to its own distinct
    // MetalVertexAttribKind -- a collision here would mean two semantically different attribute
    // shapes silently sharing one MTLVertexFormat translation.
    const VertexElementFormat all[] = {
        VertexElementFormat::Single, VertexElementFormat::Vector2, VertexElementFormat::Vector3,
        VertexElementFormat::Vector4, VertexElementFormat::Color, VertexElementFormat::Byte4,
        VertexElementFormat::Short2, VertexElementFormat::Short4,
        VertexElementFormat::NormalizedShort2, VertexElementFormat::NormalizedShort4,
        VertexElementFormat::HalfVector2, VertexElementFormat::HalfVector4,
    };
    for (std::size_t i = 0; i < std::size(all); ++i)
        for (std::size_t j = i + 1; j < std::size(all); ++j)
            EXPECT_NE(DescribeMetalVertexElementFormat(all[i]).kind, DescribeMetalVertexElementFormat(all[j]).kind)
                << "index " << i << " vs " << j;
}
