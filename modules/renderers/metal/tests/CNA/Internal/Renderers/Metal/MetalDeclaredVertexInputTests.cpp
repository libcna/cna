// SPDX-License-Identifier: MS-PL
// plans/plan_apple_m4.md AM4-035: the declaration-to-attribute mapping every built-in Metal
// pipeline's vertex descriptor is now built from.

#include <gtest/gtest.h>

#include "CNA/Internal/Renderers/Metal/MetalDeclaredVertexInput.hpp"

using namespace CNA::Internal::Renderers::Metal;
using CNA::Internal::Renderers::GpuDrawParams;
using Microsoft::Xna::Framework::Graphics::VertexElement;
using F = Microsoft::Xna::Framework::Graphics::VertexElementFormat;
using U = Microsoft::Xna::Framework::Graphics::VertexElementUsage;
using A = MetalVertexAttribKind;
using K = MetalPipelineKind;

namespace
{
    std::vector<MetalDeclaredAttribute> Attrs(std::initializer_list<MetalDeclaredAttribute> list)
    {
        return std::vector<MetalDeclaredAttribute>(list);
    }

    MetalDeclaredVertexInput Canonical(K kind, int stride)
    {
        return BuildMetalDeclaredVertexInput(kind, MetalCanonicalElementsFor(kind), stride);
    }
}

// The canonical declarations must reproduce the fixed stride table byte for byte -- an undeclared
// buffer's draw is unchanged by building its descriptor this way.
TEST(MetalDeclaredVertexInput, CanonicalLayoutsMatchTheFixedStrideTable)
{
    EXPECT_EQ(Canonical(K::Colored16, 16).attributes, Attrs({{0, A::Float3, 0}, {1, A::UChar4Normalized, 12}}));
    EXPECT_EQ(Canonical(K::Textured20, 20).attributes, Attrs({{0, A::Float3, 0}, {1, A::Float2, 12}}));
    EXPECT_EQ(Canonical(K::ColorTex24, 24).attributes,
              Attrs({{0, A::Float3, 0}, {1, A::UChar4Normalized, 12}, {2, A::Float2, 16}}));
    EXPECT_EQ(Canonical(K::LitTex32, 32).attributes,
              Attrs({{0, A::Float3, 0}, {1, A::Float3, 12}, {2, A::Float2, 24}}));
    EXPECT_EQ(Canonical(K::Pbr48, 48).attributes,
              Attrs({{0, A::Float3, 0}, {1, A::Float3, 12}, {2, A::Float4, 24}, {3, A::Float2, 40}}));
    EXPECT_EQ(Canonical(K::Skinned52, 52).attributes,
              Attrs({{0, A::Float3, 0}, {1, A::Float3, 12}, {2, A::Float2, 24}, {3, A::Float4, 32},
                     {4, A::UChar4, 48}}));
    EXPECT_EQ(Canonical(K::Skinned56, 56).attributes,
              Attrs({{0, A::Float3, 0}, {1, A::Float3, 12}, {2, A::Float2, 24}, {3, A::Float4, 32},
                     {4, A::UChar4, 48}, {5, A::UChar4Normalized, 52}}));
    EXPECT_EQ(Canonical(K::SkinnedPbr68, 68).attributes,
              Attrs({{0, A::Float3, 0}, {1, A::Float3, 12}, {2, A::Float4, 24}, {3, A::Float2, 40},
                     {4, A::Float4, 48}, {5, A::UChar4, 64}}));
}

TEST(MetalDeclaredVertexInput, DualTextureWithOneCoordinateSetFeedsItToBothSamplers)
{
    EXPECT_EQ(Canonical(K::DualTex20, 20).attributes,
              Attrs({{0, A::Float3, 0}, {1, A::Float2, 12}, {2, A::Float2, 12}}));
    EXPECT_EQ(Canonical(K::DualTex24Colored, 24).attributes,
              Attrs({{0, A::Float3, 0}, {1, A::UChar4Normalized, 12}, {2, A::Float2, 16}, {3, A::Float2, 16}}));
}

TEST(MetalDeclaredVertexInput, DualTextureReadsADeclaredSecondCoordinateSet)
{
    const std::vector<VertexElement> dual{
        VertexElement(0, F::Vector3, U::Position, 0),
        VertexElement(12, F::Color, U::Color, 0),
        VertexElement(16, F::Vector2, U::TextureCoordinate, 0),
        VertexElement(24, F::Vector2, U::TextureCoordinate, 1)};
    const auto input = BuildMetalDeclaredVertexInput(K::DualTex24Colored, dual, 32);
    ASSERT_TRUE(input.IsComplete()) << input.refusal;
    EXPECT_EQ(input.stride, 32);
    EXPECT_EQ(input.attributes,
              Attrs({{0, A::Float3, 0}, {1, A::UChar4Normalized, 12}, {2, A::Float2, 16}, {3, A::Float2, 24}}));
}

// cna-car-simulator's GpuMesh vertex: a lit textured record with a second, unused UV set.
TEST(MetalDeclaredVertexInput, ExtraChannelsTheShaderDoesNotReadAreIgnored)
{
    const std::vector<VertexElement> mesh{
        VertexElement(0, F::Vector3, U::Position, 0),
        VertexElement(12, F::Vector3, U::Normal, 0),
        VertexElement(24, F::Vector2, U::TextureCoordinate, 0),
        VertexElement(32, F::Vector2, U::TextureCoordinate, 1)};
    const auto input = BuildMetalDeclaredVertexInput(K::LitTex32, mesh, 40);
    ASSERT_TRUE(input.IsComplete()) << input.refusal;
    EXPECT_EQ(input.attributes, Attrs({{0, A::Float3, 0}, {1, A::Float3, 12}, {2, A::Float2, 24}}));
    EXPECT_EQ(input.stride, 40);
}

TEST(MetalDeclaredVertexInput, AttributesFollowTheDeclaredOrderNotTheCanonicalOne)
{
    const std::vector<VertexElement> reordered{
        VertexElement(0, F::Vector3, U::Position, 0),
        VertexElement(12, F::Vector2, U::TextureCoordinate, 0),
        VertexElement(20, F::Vector3, U::Normal, 0)};
    const auto input = BuildMetalDeclaredVertexInput(K::LitTex32, reordered, 32);
    ASSERT_TRUE(input.IsComplete()) << input.refusal;
    EXPECT_EQ(input.attributes, Attrs({{0, A::Float3, 0}, {1, A::Float3, 20}, {2, A::Float2, 12}}));
}

TEST(MetalDeclaredVertexInput, AMissingInputIsRefusedByName)
{
    const std::vector<VertexElement> positionOnly{VertexElement(0, F::Vector3, U::Position, 0)};
    const auto input = BuildMetalDeclaredVertexInput(K::LitTex32, positionOnly, 12);
    EXPECT_FALSE(input.IsComplete());
    EXPECT_NE(input.refusal.find("Normal0"), std::string::npos) << input.refusal;
}

TEST(MetalDeclaredVertexInput, IntegerFormatsAreRefusedForFloatInputs)
{
    const std::vector<VertexElement> shortPosition{
        VertexElement(0, F::Short4, U::Position, 0),
        VertexElement(8, F::Color, U::Color, 0)};
    EXPECT_FALSE(BuildMetalDeclaredVertexInput(K::Colored16, shortPosition, 12).IsComplete());
}

TEST(MetalDeclaredVertexInput, BlendIndicesMustBeByte4)
{
    auto elements = MetalCanonicalElementsFor(K::Skinned52);
    EXPECT_TRUE(BuildMetalDeclaredVertexInput(K::Skinned52, elements, 52).IsComplete());
    elements[4] = VertexElement(48, F::Short4, U::BlendIndices, 0);
    EXPECT_FALSE(BuildMetalDeclaredVertexInput(K::Skinned52, elements, 56).IsComplete());
}

TEST(MetalDeclaredVertexInput, AnElementPastTheRecordIsRefused)
{
    const std::vector<VertexElement> overrun{
        VertexElement(0, F::Vector3, U::Position, 0),
        VertexElement(12, F::Color, U::Color, 0)};
    EXPECT_FALSE(BuildMetalDeclaredVertexInput(K::Colored16, overrun, 14).IsComplete());
}

TEST(MetalDeclaredVertexInput, LayoutKeyDistinguishesLayouts)
{
    EXPECT_EQ(Canonical(K::LitTex32, 32).LayoutKey(), Canonical(K::LitTex32, 32).LayoutKey());
    EXPECT_NE(Canonical(K::LitTex32, 32).LayoutKey(), Canonical(K::LitTex32, 40).LayoutKey());
    EXPECT_NE(Canonical(K::Textured20, 20).LayoutKey(), Canonical(K::DualTex20, 20).LayoutKey());
    EXPECT_NE(Canonical(K::Textured20, 20).LayoutKey(), 0u);
}

TEST(MetalDeclaredVertexInput, SelectionStrideFollowsTheEffectFamilyThenTheChannels)
{
    const std::vector<VertexElement> pnt = MetalCanonicalElementsFor(K::LitTex32);
    const std::vector<VertexElement> pct = MetalCanonicalElementsFor(K::ColorTex24);
    const std::vector<VertexElement> pt = MetalCanonicalElementsFor(K::Textured20);
    const std::vector<VertexElement> pc = MetalCanonicalElementsFor(K::Colored16);

    EXPECT_EQ(MetalSelectionStrideForDeclaration(pc, nullptr), 16u);
    GpuDrawParams basic;
    EXPECT_EQ(MetalSelectionStrideForDeclaration(pnt, &basic), 32u);
    EXPECT_EQ(MetalSelectionStrideForDeclaration(pct, &basic), 24u);
    EXPECT_EQ(MetalSelectionStrideForDeclaration(pt, &basic), 20u);
    EXPECT_EQ(MetalSelectionStrideForDeclaration(pc, &basic), 16u);

    GpuDrawParams dual;
    dual.dualTexture = true;
    EXPECT_EQ(MetalSelectionStrideForDeclaration(pct, &dual), 24u);
    EXPECT_EQ(MetalSelectionStrideForDeclaration(pt, &dual), 20u);

    GpuDrawParams skinned;
    skinned.skinned = true;
    EXPECT_EQ(MetalSelectionStrideForDeclaration(MetalCanonicalElementsFor(K::Skinned56), &skinned), 56u);
    EXPECT_EQ(MetalSelectionStrideForDeclaration(MetalCanonicalElementsFor(K::Skinned52), &skinned), 52u);

    GpuDrawParams pbr;
    pbr.pbr = true;
    EXPECT_EQ(MetalSelectionStrideForDeclaration(pnt, &pbr), 48u);
    pbr.skinned = true;
    EXPECT_EQ(MetalSelectionStrideForDeclaration(pnt, &pbr), 68u);
}

TEST(MetalDeclaredVertexInput, ALitBasicEffectRefusesToDropAnActiveVertexColour)
{
    const std::vector<VertexElement> pnct{
        VertexElement(0, F::Vector3, U::Position, 0),
        VertexElement(12, F::Vector3, U::Normal, 0),
        VertexElement(24, F::Color, U::Color, 0),
        VertexElement(28, F::Vector2, U::TextureCoordinate, 0)};
    GpuDrawParams basic;
    basic.vertexColorEnabled = true;
    EXPECT_FALSE(MetalDroppedVertexColorRefusal(K::LitTex32, pnct, &basic).empty());
    basic.vertexColorEnabled = false;
    EXPECT_TRUE(MetalDroppedVertexColorRefusal(K::LitTex32, pnct, &basic).empty());
    basic.vertexColorEnabled = true;
    EXPECT_TRUE(MetalDroppedVertexColorRefusal(K::ColorTex24, MetalCanonicalElementsFor(K::ColorTex24),
                                               &basic).empty());
    EXPECT_TRUE(MetalDroppedVertexColorRefusal(K::LitTex32, MetalCanonicalElementsFor(K::LitTex32),
                                               &basic).empty());
}

// plans/plan_apple_m4.md AM4-080: Metal's lit function reads TEXCOORD0 for the textured and the
// untextured BasicEffect alike. Untextured, XNA's permutation does not read it, so a
// VertexPositionNormal declaration is accepted and the coordinate comes from the zero constant.
TEST(MetalDeclaredVertexInput, AnInputTheEffectDoesNotUseComesFromAConstant)
{
    const std::vector<VertexElement> positionNormal{
        VertexElement(0, F::Vector3, U::Position, 0), VertexElement(12, F::Vector3, U::Normal, 0)};
    GpuDrawParams untextured{};
    untextured.lightingEnabled = true;
    untextured.textureEnabled = false;
    const auto input = BuildMetalDeclaredVertexInput(K::LitTex32, positionNormal, 24, &untextured);
    ASSERT_TRUE(input.IsComplete()) << input.refusal;
    EXPECT_TRUE(input.UsesConstantAttributes());
    EXPECT_EQ(input.attributes, Attrs({{0, A::Float3, 0}, {1, A::Float3, 12},
                                       {2, A::Float2, kMetalConstantAttributeZeroOffset, true}}));
    EXPECT_NE(input.LayoutKey(), BuildMetalDeclaredVertexInput(K::LitTex32, MetalCanonicalElementsFor(K::LitTex32),
                                                               24).LayoutKey());
}

TEST(MetalDeclaredVertexInput, AnInputTheEffectUsesStillRefusesWhenMissing)
{
    const std::vector<VertexElement> positionNormal{
        VertexElement(0, F::Vector3, U::Position, 0), VertexElement(12, F::Vector3, U::Normal, 0)};
    GpuDrawParams textured{};
    textured.lightingEnabled = true;
    textured.textureEnabled = true;
    const auto input = BuildMetalDeclaredVertexInput(K::LitTex32, positionNormal, 24, &textured);
    EXPECT_FALSE(input.IsComplete());
    EXPECT_NE(input.refusal.find("TextureCoordinate0"), std::string::npos);
    // Without effect parameters every input stays required, as before.
    EXPECT_FALSE(BuildMetalDeclaredVertexInput(K::LitTex32, positionNormal, 24).IsComplete());
}

TEST(MetalDeclaredVertexInput, AnUnusedColourIsOpaqueWhiteAndAUsedOneRefuses)
{
    const std::vector<VertexElement> positionOnly{VertexElement(0, F::Vector3, U::Position, 0)};
    GpuDrawParams noColour{};
    noColour.vertexColorEnabled = false;
    const auto input = BuildMetalDeclaredVertexInput(K::Colored16, positionOnly, 12, &noColour);
    ASSERT_TRUE(input.IsComplete()) << input.refusal;
    EXPECT_EQ(input.attributes, Attrs({{0, A::Float3, 0}, {1, A::Float4, kMetalConstantAttributeOneOffset, true}}));
    EXPECT_FLOAT_EQ(kMetalConstantAttributeBlock[kMetalConstantAttributeOneOffset / 4], 1.0f);
    EXPECT_FLOAT_EQ(kMetalConstantAttributeBlock[kMetalConstantAttributeZeroOffset / 4], 0.0f);

    GpuDrawParams colour{};
    colour.vertexColorEnabled = true;
    EXPECT_FALSE(BuildMetalDeclaredVertexInput(K::Colored16, positionOnly, 12, &colour).IsComplete());
}
