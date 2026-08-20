// SPDX-License-Identifier: MS-PL
//
// plans/plan_metal.md METAL-27: BuildMetalVertexDescriptorPlan() is the plain-C++ core of the generic
// VertexElement-driven descriptor builder -- everything except the final MTLVertexDescriptor
// object construction itself, which stays in MetalRenderer.mm. Only reads VertexElement
// (plain XNA framework type) and returns plain C++ structs, zero Objective-C dependency, so this
// is genuinely unit-tested on this Linux machine. No #if defined(CNA_RENDERER_METAL) gate,
// deliberately.
#include <gtest/gtest.h>
#include "CNA/Internal/Renderers/Metal/MetalVertexDescriptorPlan.hpp"

using namespace CNA::Internal::Renderers::Metal;
using Microsoft::Xna::Framework::Graphics::VertexElement;
using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
using Microsoft::Xna::Framework::Graphics::VertexElementUsage;

TEST(MetalVertexDescriptorPlan, EmptyElementListProducesEmptyPlanWithStridePreserved)
{
    std::vector<VertexElement> elements;
    MetalVertexDescriptorPlan plan = BuildMetalVertexDescriptorPlan(24, elements);
    EXPECT_EQ(plan.stride, 24);
    EXPECT_TRUE(plan.attributes.empty());
}

TEST(MetalVertexDescriptorPlan, SingleElementMapsOffsetAndFormatVerbatim)
{
    std::vector<VertexElement> elements = {
        VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
    };
    MetalVertexDescriptorPlan plan = BuildMetalVertexDescriptorPlan(12, elements);
    ASSERT_EQ(plan.attributes.size(), 1u);
    EXPECT_EQ(plan.attributes[0].location, 0);
    EXPECT_EQ(plan.attributes[0].offset, 0);
    EXPECT_EQ(plan.attributes[0].bufferIndex, 0);
    EXPECT_EQ(plan.attributes[0].kind, MetalVertexAttribKind::Float3);
    EXPECT_EQ(plan.stride, 12);
}

TEST(MetalVertexDescriptorPlan, LocationMatchesElementIndexNotOffsetOrder)
{
    // Deliberately out-of-offset-order declaration (color at a later offset than uv, but placed
    // first in the element list) to prove location is derived from list position, not sorted by
    // offset -- matching EasyGLRenderer::ApplyLayout()'s own documented convention exactly
    // ("attribute location = the element's own index within the declaration's element list").
    std::vector<VertexElement> elements = {
        VertexElement(16, VertexElementFormat::Color, VertexElementUsage::Color, 0),          // location 0, offset 16
        VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),      // location 1, offset 0
        VertexElement(12, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0), // location 2, offset 12
    };
    MetalVertexDescriptorPlan plan = BuildMetalVertexDescriptorPlan(20, elements);
    ASSERT_EQ(plan.attributes.size(), 3u);

    EXPECT_EQ(plan.attributes[0].location, 0);
    EXPECT_EQ(plan.attributes[0].offset, 16);
    EXPECT_EQ(plan.attributes[0].kind, MetalVertexAttribKind::UChar4Normalized);

    EXPECT_EQ(plan.attributes[1].location, 1);
    EXPECT_EQ(plan.attributes[1].offset, 0);
    EXPECT_EQ(plan.attributes[1].kind, MetalVertexAttribKind::Float3);

    EXPECT_EQ(plan.attributes[2].location, 2);
    EXPECT_EQ(plan.attributes[2].offset, 12);
    EXPECT_EQ(plan.attributes[2].kind, MetalVertexAttribKind::Float2);
}

TEST(MetalVertexDescriptorPlan, EveryAttributeUsesBufferIndexZero)
{
    // This renderer has no per-instance / multi-stream binding yet (Phase 9's own note: real
    // instancing always goes through a custom Effect not built yet) -- every attribute from a
    // single VertexElement list must come from the same buffer slot.
    std::vector<VertexElement> elements = {
        VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
        VertexElement(24, VertexElementFormat::Vector4, VertexElementUsage::Tangent, 0),
        VertexElement(40, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
    };
    MetalVertexDescriptorPlan plan = BuildMetalVertexDescriptorPlan(48, elements);
    ASSERT_EQ(plan.attributes.size(), 4u);
    for (const auto& attr : plan.attributes)
        EXPECT_EQ(attr.bufferIndex, 0);
}

TEST(MetalVertexDescriptorPlan, MatchesExistingStride48PbrLayoutExactly)
{
    // Cross-validates against MetalRenderer.mm's own existing hand-written stride-48
    // vertexDescriptorForStride() case (position/normal/tangent/uv at offsets 0/12/24/40) -- if a
    // real ShaderEffect ever declared this exact same layout via SetVertexDeclaration(), the
    // generic path must produce an equivalent result to the fixed-stride path, not a divergent one.
    std::vector<VertexElement> elements = {
        VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
        VertexElement(24, VertexElementFormat::Vector4, VertexElementUsage::Tangent, 0),
        VertexElement(40, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
    };
    MetalVertexDescriptorPlan plan = BuildMetalVertexDescriptorPlan(48, elements);
    ASSERT_EQ(plan.attributes.size(), 4u);
    EXPECT_EQ(plan.attributes[0].kind, MetalVertexAttribKind::Float3); EXPECT_EQ(plan.attributes[0].offset, 0);
    EXPECT_EQ(plan.attributes[1].kind, MetalVertexAttribKind::Float3); EXPECT_EQ(plan.attributes[1].offset, 12);
    EXPECT_EQ(plan.attributes[2].kind, MetalVertexAttribKind::Float4); EXPECT_EQ(plan.attributes[2].offset, 24);
    EXPECT_EQ(plan.attributes[3].kind, MetalVertexAttribKind::Float2); EXPECT_EQ(plan.attributes[3].offset, 40);
    EXPECT_EQ(plan.stride, 48);
}

TEST(MetalVertexDescriptorPlan, MatchesExistingStride52SkinnedLayoutExactly)
{
    // Cross-validates against the stride-52 SkinnedEffect case: boneIndices must land on
    // UChar4 (raw, unnormalized), not UChar4Normalized -- the single riskiest format distinction
    // in this whole table (see MetalVertexAttribFormatTests.cpp's own dedicated check for why).
    std::vector<VertexElement> elements = {
        VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
        VertexElement(24, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
        VertexElement(32, VertexElementFormat::Vector4, VertexElementUsage::BlendWeight, 0),
        VertexElement(48, VertexElementFormat::Byte4, VertexElementUsage::BlendIndices, 0),
    };
    MetalVertexDescriptorPlan plan = BuildMetalVertexDescriptorPlan(52, elements);
    ASSERT_EQ(plan.attributes.size(), 5u);
    EXPECT_EQ(plan.attributes[4].kind, MetalVertexAttribKind::UChar4);
    EXPECT_EQ(plan.attributes[4].offset, 48);
}
