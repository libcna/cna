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

    // drawMetal3D builds an undeclared buffer's input this way: its canonical type carries no
    // colour a lit draw would take (AM4-081), so colour is not a permutation input there.
    MetalDeclaredVertexInput Canonical(K kind, int stride)
    {
        GpuDrawParams params{};
        params.vertexColorEnabled = false;
        return BuildMetalDeclaredVertexInput(kind, MetalCanonicalElementsFor(kind), stride, &params);
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
    // AM4-081: the lit functions also read COLOR0, which VertexPositionNormalTexture lacks.
    EXPECT_EQ(Canonical(K::LitTex32, 32).attributes,
              Attrs({{0, A::Float3, 0}, {1, A::Float3, 12}, {2, A::Float2, 24},
                     {3, A::Float4, kMetalConstantAttributeOneOffset, true}}));
    // AM4-084: the PBR functions read COLOR_0, which the stride-48 and stride-68 records lack.
    EXPECT_EQ(Canonical(K::Pbr48, 48).attributes,
              Attrs({{0, A::Float3, 0}, {1, A::Float3, 12}, {2, A::Float4, 24}, {3, A::Float2, 40},
                     {4, A::Float4, kMetalConstantAttributeOneOffset, true}}));
    EXPECT_EQ(Canonical(K::Skinned52, 52).attributes,
              Attrs({{0, A::Float3, 0}, {1, A::Float3, 12}, {2, A::Float2, 24}, {3, A::Float4, 32},
                     {4, A::UChar4, 48}}));
    EXPECT_EQ(Canonical(K::Skinned56, 56).attributes,
              Attrs({{0, A::Float3, 0}, {1, A::Float3, 12}, {2, A::Float2, 24}, {3, A::Float4, 32},
                     {4, A::UChar4, 48}, {5, A::UChar4Normalized, 52}}));
    EXPECT_EQ(Canonical(K::SkinnedPbr68, 68).attributes,
              Attrs({{0, A::Float3, 0}, {1, A::Float3, 12}, {2, A::Float4, 24}, {3, A::Float2, 40},
                     {4, A::Float4, 48}, {5, A::UChar4, 64},
                     {6, A::Float4, kMetalConstantAttributeOneOffset, true}}));
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
    GpuDrawParams lit{};
    lit.lightingEnabled = true;
    lit.textureEnabled = true;
    lit.vertexColorEnabled = false;
    const auto input = BuildMetalDeclaredVertexInput(K::LitTex32, mesh, 40, &lit);
    ASSERT_TRUE(input.IsComplete()) << input.refusal;
    EXPECT_EQ(input.attributes, Attrs({{0, A::Float3, 0}, {1, A::Float3, 12}, {2, A::Float2, 24},
                                       {3, A::Float4, kMetalConstantAttributeOneOffset, true}}));
    EXPECT_EQ(input.stride, 40);
}

TEST(MetalDeclaredVertexInput, AttributesFollowTheDeclaredOrderNotTheCanonicalOne)
{
    const std::vector<VertexElement> reordered{
        VertexElement(0, F::Vector3, U::Position, 0),
        VertexElement(12, F::Vector2, U::TextureCoordinate, 0),
        VertexElement(20, F::Vector3, U::Normal, 0)};
    GpuDrawParams lit{};
    lit.lightingEnabled = true;
    lit.textureEnabled = true;
    lit.vertexColorEnabled = false;
    const auto input = BuildMetalDeclaredVertexInput(K::LitTex32, reordered, 32, &lit);
    ASSERT_TRUE(input.IsComplete()) << input.refusal;
    EXPECT_EQ(input.attributes, Attrs({{0, A::Float3, 0}, {1, A::Float3, 20}, {2, A::Float2, 12},
                                       {3, A::Float4, kMetalConstantAttributeOneOffset, true}}));
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

// plans/plan_apple_m4.md AM4-081: the lit functions read COLOR0, so a lit BasicEffect with
// VertexColorEnabled takes the declared colour instead of being refused for dropping it.
TEST(MetalDeclaredVertexInput, ALitBasicEffectReadsAnActiveVertexColour)
{
    const std::vector<VertexElement> pnct{
        VertexElement(0, F::Vector3, U::Position, 0),
        VertexElement(12, F::Vector3, U::Normal, 0),
        VertexElement(24, F::Color, U::Color, 0),
        VertexElement(28, F::Vector2, U::TextureCoordinate, 0)};
    GpuDrawParams basic;
    basic.lightingEnabled = true;
    basic.vertexColorEnabled = true;
    EXPECT_TRUE(MetalDroppedVertexColorRefusal(K::LitTex32, pnct, &basic).empty());
    const auto input = BuildMetalDeclaredVertexInput(K::LitTex32, pnct, 36, &basic);
    ASSERT_TRUE(input.IsComplete()) << input.refusal;
    EXPECT_EQ(input.attributes, Attrs({{0, A::Float3, 0}, {1, A::Float3, 12}, {2, A::Float2, 28},
                                       {3, A::UChar4Normalized, 24}}));
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
    untextured.vertexColorEnabled = false;   // BasicEffect's default
    const auto input = BuildMetalDeclaredVertexInput(K::LitTex32, positionNormal, 24, &untextured);
    ASSERT_TRUE(input.IsComplete()) << input.refusal;
    EXPECT_TRUE(input.UsesConstantAttributes());
    EXPECT_EQ(input.attributes, Attrs({{0, A::Float3, 0}, {1, A::Float3, 12},
                                       {2, A::Float2, kMetalConstantAttributeZeroOffset, true},
                                       {3, A::Float4, kMetalConstantAttributeOneOffset, true}}));
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
    textured.vertexColorEnabled = false;
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

// plans/plan_apple_m4.md AM4-084: the glTF PBR records other renderers bind for an undeclared buffer.
TEST(MetalDeclaredVertexInput, ThePbrKindsCarryTheGltfRecordsOfTheirOtherStrides)
{
    const auto pbr48 = MetalCanonicalElementsFor(K::Pbr48);
    auto pbr60 = pbr48;
    pbr60.push_back(VertexElement(48, F::Vector2, U::TextureCoordinate, 1));
    pbr60.push_back(VertexElement(56, F::Color, U::Color, 0));
    EXPECT_EQ(MetalCanonicalElementsFor(K::Pbr48, 48), pbr48);
    EXPECT_EQ(MetalCanonicalElementsFor(K::Pbr48, 60), pbr60);

    const auto skinned68 = MetalCanonicalElementsFor(K::SkinnedPbr68);
    auto skinned76 = skinned68;
    skinned76.push_back(VertexElement(68, F::Vector2, U::TextureCoordinate, 1));
    auto skinned80 = skinned76;
    skinned80.push_back(VertexElement(76, F::Color, U::Color, 0));
    EXPECT_EQ(MetalCanonicalElementsFor(K::SkinnedPbr68, 68), skinned68);
    EXPECT_EQ(MetalCanonicalElementsFor(K::SkinnedPbr68, 76), skinned76);
    EXPECT_EQ(MetalCanonicalElementsFor(K::SkinnedPbr68, 80), skinned80);

    // The stride says nothing more for any other kind.
    EXPECT_EQ(MetalCanonicalElementsFor(K::LitTex32, 60), MetalCanonicalElementsFor(K::LitTex32));
}

// glTF 2.0 3.9.2: COLOR_0 multiplies PBR base colour when the effect enables it, and is the identity
// otherwise -- never a refusal, because PbrEffect's flag is what separates an authored colour from the
// importer's opaque-white filler.
TEST(MetalDeclaredVertexInput, APbrDrawReadsColour0OnlyWhenTheEffectEnablesIt)
{
    GpuDrawParams enabled{};
    enabled.pbr = true;
    enabled.vertexColorEnabled = true;
    GpuDrawParams disabled = enabled;
    disabled.vertexColorEnabled = false;
    const MetalDeclaredAttribute white{4, A::Float4, kMetalConstantAttributeOneOffset, true};

    const auto rigid60 = MetalCanonicalElementsFor(K::Pbr48, 60);
    const auto read = BuildMetalDeclaredVertexInput(K::Pbr48, rigid60, 60, &enabled);
    ASSERT_TRUE(read.IsComplete()) << read.refusal;
    EXPECT_EQ(read.attributes.back(), (MetalDeclaredAttribute{4, A::UChar4Normalized, 56}));
    EXPECT_EQ(BuildMetalDeclaredVertexInput(K::Pbr48, rigid60, 60, &disabled).attributes.back(), white);
    EXPECT_EQ(BuildMetalDeclaredVertexInput(K::Pbr48, rigid60, 60).attributes.back(), white);
    // A record without COLOR_0 is glTF's absent colour, whatever the switch says.
    const auto rigid48 = BuildMetalDeclaredVertexInput(K::Pbr48, MetalCanonicalElementsFor(K::Pbr48), 48, &enabled);
    ASSERT_TRUE(rigid48.IsComplete()) << rigid48.refusal;
    EXPECT_EQ(rigid48.attributes.back(), white);
    EXPECT_NE(read.LayoutKey(), BuildMetalDeclaredVertexInput(K::Pbr48, rigid60, 60, &disabled).LayoutKey());

    GpuDrawParams skinned = enabled;
    skinned.skinned = true;
    const auto skin80 = BuildMetalDeclaredVertexInput(
        K::SkinnedPbr68, MetalCanonicalElementsFor(K::SkinnedPbr68, 80), 80, &skinned);
    ASSERT_TRUE(skin80.IsComplete()) << skin80.refusal;
    EXPECT_EQ(skin80.attributes.back(), (MetalDeclaredAttribute{6, A::UChar4Normalized, 76}));
    const auto skin76 = BuildMetalDeclaredVertexInput(
        K::SkinnedPbr68, MetalCanonicalElementsFor(K::SkinnedPbr68, 76), 76, &skinned);
    ASSERT_TRUE(skin76.IsComplete()) << skin76.refusal;
    EXPECT_EQ(skin76.attributes.back(), (MetalDeclaredAttribute{6, A::Float4, kMetalConstantAttributeOneOffset, true}));
}
