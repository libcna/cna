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
    // AM4-084/085: the PBR functions read COLOR_0, which the stride-48 and stride-68 records lack,
    // and TEXCOORD_1, which a single-coordinate record supplies from TEXCOORD_0.
    EXPECT_EQ(Canonical(K::Pbr48, 48).attributes,
              Attrs({{0, A::Float3, 0}, {1, A::Float3, 12}, {2, A::Float4, 24}, {3, A::Float2, 40},
                     {4, A::Float4, kMetalConstantAttributeOneOffset, true}, {5, A::Float2, 40}}));
    EXPECT_EQ(Canonical(K::Skinned52, 52).attributes,
              Attrs({{0, A::Float3, 0}, {1, A::Float3, 12}, {2, A::Float2, 24}, {3, A::Float4, 32},
                     {4, A::UChar4, 48}}));
    EXPECT_EQ(Canonical(K::Skinned56, 56).attributes,
              Attrs({{0, A::Float3, 0}, {1, A::Float3, 12}, {2, A::Float2, 24}, {3, A::Float4, 32},
                     {4, A::UChar4, 48}, {5, A::UChar4Normalized, 52}}));
    EXPECT_EQ(Canonical(K::SkinnedPbr68, 68).attributes,
              Attrs({{0, A::Float3, 0}, {1, A::Float3, 12}, {2, A::Float4, 24}, {3, A::Float2, 40},
                     {4, A::Float4, 48}, {5, A::UChar4, 64},
                     {6, A::Float4, kMetalConstantAttributeOneOffset, true}, {7, A::Float2, 40}}));
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
    // AM4-098: with VertexColorEnabled off XNA's BasicEffect ignores a declared COLOR0, so the lit
    // functions read the constant opaque white rather than the record's colour -- both lit variants.
    for (const K lit : {K::LitTex32, K::LitTex32VertexLit})
    {
        const auto ignored = BuildMetalDeclaredVertexInput(lit, pnct, 36, &basic);
        ASSERT_TRUE(ignored.IsComplete()) << ignored.refusal;
        EXPECT_EQ(ignored.attributes, Attrs({{0, A::Float3, 0}, {1, A::Float3, 12}, {2, A::Float2, 28},
                                             {3, A::Float4, kMetalConstantAttributeOneOffset, true}}));
    }
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
    EXPECT_EQ(read.attributes.at(4), (MetalDeclaredAttribute{4, A::UChar4Normalized, 56}));
    EXPECT_EQ(BuildMetalDeclaredVertexInput(K::Pbr48, rigid60, 60, &disabled).attributes.at(4), white);
    EXPECT_EQ(BuildMetalDeclaredVertexInput(K::Pbr48, rigid60, 60).attributes.at(4), white);
    // A record without COLOR_0 is glTF's absent colour, whatever the switch says.
    const auto rigid48 = BuildMetalDeclaredVertexInput(K::Pbr48, MetalCanonicalElementsFor(K::Pbr48), 48, &enabled);
    ASSERT_TRUE(rigid48.IsComplete()) << rigid48.refusal;
    EXPECT_EQ(rigid48.attributes.at(4), white);
    EXPECT_NE(read.LayoutKey(), BuildMetalDeclaredVertexInput(K::Pbr48, rigid60, 60, &disabled).LayoutKey());

    GpuDrawParams skinned = enabled;
    skinned.skinned = true;
    const auto skin80 = BuildMetalDeclaredVertexInput(
        K::SkinnedPbr68, MetalCanonicalElementsFor(K::SkinnedPbr68, 80), 80, &skinned);
    ASSERT_TRUE(skin80.IsComplete()) << skin80.refusal;
    EXPECT_EQ(skin80.attributes.at(6), (MetalDeclaredAttribute{6, A::UChar4Normalized, 76}));
    const auto skin76 = BuildMetalDeclaredVertexInput(
        K::SkinnedPbr68, MetalCanonicalElementsFor(K::SkinnedPbr68, 76), 76, &skinned);
    ASSERT_TRUE(skin76.IsComplete()) << skin76.refusal;
    EXPECT_EQ(skin76.attributes.at(6), (MetalDeclaredAttribute{6, A::Float4, kMetalConstantAttributeOneOffset, true}));
}

// plans/plan_apple_m4.md AM4-085: the glTF records' second coordinate set reaches the PBR functions.
TEST(MetalDeclaredVertexInput, ThePbrFunctionsReadTheRecordsSecondCoordinateSet)
{
    GpuDrawParams pbr{};
    pbr.pbr = true;
    const auto rigid60 = BuildMetalDeclaredVertexInput(K::Pbr48, MetalCanonicalElementsFor(K::Pbr48, 60), 60, &pbr);
    ASSERT_TRUE(rigid60.IsComplete()) << rigid60.refusal;
    EXPECT_EQ(rigid60.attributes.at(5), (MetalDeclaredAttribute{5, A::Float2, 48}));

    pbr.skinned = true;
    for (const int stride : {76, 80})
    {
        const auto skinned = BuildMetalDeclaredVertexInput(
            K::SkinnedPbr68, MetalCanonicalElementsFor(K::SkinnedPbr68, static_cast<std::size_t>(stride)), stride, &pbr);
        ASSERT_TRUE(skinned.IsComplete()) << skinned.refusal;
        EXPECT_EQ(skinned.attributes.at(7), (MetalDeclaredAttribute{7, A::Float2, 68})) << stride;
    }
}

// plans/plan_apple_m4.md AM4-143: several vertex streams and the per-instance world matrix.
namespace
{
    void AddStream(GpuDrawParams& params, int stride, int instanceFrequency = 0)
    {
        const int i = params.vertexStreamCount++;
        auto& stream = params.vertexStreams[static_cast<std::size_t>(i)];
        stream.slot = i;
        stream.strideInBytes = stride;
        stream.instanceFrequency = instanceFrequency;
        if (instanceFrequency == 0)
        {
            stream.combinedByteBase = params.combinedVertexStride;
            params.combinedVertexStride += stride;
        }
    }

    MetalStreamDeclarations Declarations(std::initializer_list<const std::vector<VertexElement>*> list)
    {
        MetalStreamDeclarations declarations{};
        std::size_t i = 0;
        for (const auto* elements : list)
            declarations[i++] = elements;
        return declarations;
    }

    const std::vector<VertexElement> kMatrixColumns = {
        VertexElement(0, F::Vector4, U::TextureCoordinate, 1),
        VertexElement(16, F::Vector4, U::TextureCoordinate, 2),
        VertexElement(32, F::Vector4, U::TextureCoordinate, 3),
        VertexElement(48, F::Vector4, U::TextureCoordinate, 4),
    };
}

// Position in stream 0 and colour in stream 1: each attribute reads its own stream, at its offset
// inside that stream, and each stream has its own per-vertex layout.
TEST(MetalDeclaredVertexInput, EachPerVertexStreamFeedsItsOwnAttributes)
{
    const std::vector<VertexElement> positions = {VertexElement(0, F::Vector3, U::Position, 0)};
    const std::vector<VertexElement> colours = {VertexElement(0, F::Color, U::Color, 0)};
    GpuDrawParams params{};
    params.vertexColorEnabled = true;
    AddStream(params, 12);
    AddStream(params, 4);
    const auto declarations = Declarations({&positions, &colours});

    const auto combined = MetalCombinedPerVertexElements(params, declarations);
    ASSERT_EQ(combined.size(), 2u);
    EXPECT_EQ(combined[1].getOffsetProperty(), 12);
    EXPECT_EQ(MetalSelectionStrideForDeclaration(combined, &params), 16u);

    const auto input = BuildMetalStreamVertexInput(K::Colored16, params, declarations);
    ASSERT_TRUE(input.IsComplete()) << input.refusal;
    EXPECT_EQ(input.attributes, Attrs({{0, A::Float3, 0, false, 0},
                                       {1, A::UChar4Normalized, 0, false, MetalVertexStreamBufferIndex(1)}}));
    ASSERT_EQ(input.layouts.size(), 2u);
    EXPECT_EQ(input.layouts[0], (MetalDeclaredLayout{0, 12, 0}));
    EXPECT_EQ(input.layouts[1], (MetalDeclaredLayout{MetalVertexStreamBufferIndex(1), 4, 0}));
    EXPECT_FALSE(input.instanceMatrix);
}

// A stream the stock function reads nothing from is not fetched, as XNA fetches only declared inputs.
TEST(MetalDeclaredVertexInput, AStreamThatSuppliesNothingGetsNoLayout)
{
    const std::vector<VertexElement> positions = {VertexElement(0, F::Vector3, U::Position, 0),
                                                  VertexElement(12, F::Color, U::Color, 0)};
    const std::vector<VertexElement> unused = {VertexElement(0, F::Vector4, U::Binormal, 0)};
    GpuDrawParams params{};
    params.vertexColorEnabled = true;
    AddStream(params, 16);
    AddStream(params, 16);
    const auto input = BuildMetalStreamVertexInput(K::Colored16, params, Declarations({&positions, &unused}));
    ASSERT_TRUE(input.IsComplete()) << input.refusal;
    ASSERT_EQ(input.layouts.size(), 1u);
    EXPECT_EQ(input.layouts[0].bufferIndex, 0);
}

// The same usage in two streams takes XNA's binding-time remap (SOFTWARE-320): a second stream's
// TextureCoordinate0 binds as TextureCoordinate1, which is DualTextureEffect's second set. Without
// the remap the lookup would alias TEXCOORD0 of stream 0 instead.
TEST(MetalDeclaredVertexInput, TheEffectiveUsageIndexDecidesWhichStreamSuppliesASemantic)
{
    const std::vector<VertexElement> first = {VertexElement(0, F::Vector3, U::Position, 0),
                                              VertexElement(12, F::Vector2, U::TextureCoordinate, 0)};
    const std::vector<VertexElement> second = {VertexElement(0, F::Vector2, U::TextureCoordinate, 0)};
    GpuDrawParams params{};
    params.dualTexture = true;
    AddStream(params, 20);
    AddStream(params, 8);
    const auto declarations = Declarations({&first, &second});
    const auto aliased = BuildMetalStreamVertexInput(K::DualTex20, params, declarations);
    ASSERT_TRUE(aliased.IsComplete()) << aliased.refusal;
    EXPECT_EQ(aliased.attributes.at(2), (MetalDeclaredAttribute{2, A::Float2, 12, false, 0}));

    params.vertexStreams[1].effectiveUsageIndices[0] = 1;
    params.vertexStreams[1].effectiveUsageIndexCount = 1;
    const auto remapped = BuildMetalStreamVertexInput(K::DualTex20, params, declarations);
    ASSERT_TRUE(remapped.IsComplete()) << remapped.refusal;
    EXPECT_EQ(remapped.attributes.at(2),
              (MetalDeclaredAttribute{2, A::Float2, 0, false, MetalVertexStreamBufferIndex(1)}));
}

// The per-instance stream's four Vector4 elements are the world matrix's columns, at attributes
// 12..15, whatever their usages; the stream steps once per InstanceFrequency instances.
TEST(MetalDeclaredVertexInput, ThePerInstanceStreamSuppliesTheInstanceMatrixColumns)
{
    const std::vector<VertexElement> geometry = {VertexElement(0, F::Vector3, U::Position, 0),
                                                 VertexElement(12, F::Color, U::Color, 0)};
    GpuDrawParams params{};
    params.vertexColorEnabled = true;
    params.instanceCount = 4;
    AddStream(params, 16);
    AddStream(params, 64, 2);
    const auto input = BuildMetalStreamVertexInput(K::Colored16, params, Declarations({&geometry, &kMatrixColumns}));
    ASSERT_TRUE(input.IsComplete()) << input.refusal;
    EXPECT_TRUE(input.instanceMatrix);
    const int instanceBuffer = MetalVertexStreamBufferIndex(1);
    EXPECT_EQ(input.attributes, Attrs({{0, A::Float3, 0, false, 0},
                                       {1, A::UChar4Normalized, 12, false, 0},
                                       {12, A::Float4, 0, false, instanceBuffer},
                                       {13, A::Float4, 16, false, instanceBuffer},
                                       {14, A::Float4, 32, false, instanceBuffer},
                                       {15, A::Float4, 48, false, instanceBuffer}}));
    ASSERT_EQ(input.layouts.size(), 2u);
    EXPECT_EQ(input.layouts[1], (MetalDeclaredLayout{instanceBuffer, 64, 2}));
}

// The columns are the per-instance declarations concatenated in slot order (EasyGL's locations
// 12..15), so a matrix split across two streams assembles; a column nothing supplies is (0,0,0,1).
TEST(MetalDeclaredVertexInput, TheInstanceMatrixSpansStreamsAndAMissingColumnIsUnitW)
{
    const std::vector<VertexElement> geometry = {VertexElement(0, F::Vector3, U::Position, 0),
                                                 VertexElement(12, F::Color, U::Color, 0)};
    const std::vector<VertexElement> threeColumns(kMatrixColumns.begin(), kMatrixColumns.begin() + 3);
    const std::vector<VertexElement> oneColumn = {VertexElement(0, F::Vector4, U::TextureCoordinate, 4)};
    GpuDrawParams params{};
    params.vertexColorEnabled = true;
    AddStream(params, 16);
    AddStream(params, 48, 1);
    AddStream(params, 16, 1);
    const auto split = BuildMetalStreamVertexInput(
        K::Colored16, params, Declarations({&geometry, &threeColumns, &oneColumn}));
    ASSERT_TRUE(split.IsComplete()) << split.refusal;
    EXPECT_EQ(split.attributes.at(5), (MetalDeclaredAttribute{15, A::Float4, 0, false, MetalVertexStreamBufferIndex(2)}));
    EXPECT_EQ(split.layouts.size(), 3u);

    params.vertexStreamCount = 2;
    const auto short3 = BuildMetalStreamVertexInput(K::Colored16, params, Declarations({&geometry, &threeColumns}));
    ASSERT_TRUE(short3.IsComplete()) << short3.refusal;
    EXPECT_EQ(short3.attributes.at(5),
              (MetalDeclaredAttribute{15, A::Float4, kMetalConstantAttributeUnitWOffset, true}));
    EXPECT_TRUE(short3.UsesConstantAttributes());
    EXPECT_FLOAT_EQ(kMetalConstantAttributeBlock[kMetalConstantAttributeUnitWOffset / 4 + 3], 1.0f);
    EXPECT_FLOAT_EQ(kMetalConstantAttributeBlock[kMetalConstantAttributeUnitWOffset / 4], 0.0f);
}

// An integer element cannot become a float4 column, and a per-instance stream with no declaration
// supplies nothing at all; both refuse by name rather than draw from a subset.
TEST(MetalDeclaredVertexInput, AnInstanceStreamMetalCannotReadRefuses)
{
    const std::vector<VertexElement> geometry = {VertexElement(0, F::Vector3, U::Position, 0),
                                                 VertexElement(12, F::Color, U::Color, 0)};
    const std::vector<VertexElement> integers = {VertexElement(0, F::Short4, U::TextureCoordinate, 1)};
    const std::vector<VertexElement> none;
    GpuDrawParams params{};
    params.vertexColorEnabled = true;
    AddStream(params, 16);
    AddStream(params, 8, 1);
    EXPECT_FALSE(BuildMetalStreamVertexInput(K::Colored16, params, Declarations({&geometry, &integers})).IsComplete());
    EXPECT_FALSE(BuildMetalStreamVertexInput(K::Colored16, params, Declarations({&geometry, &none})).IsComplete());
}

// A different stream layout or the instance matrix is a different pipeline.
TEST(MetalDeclaredVertexInput, StreamLayoutsAndTheInstanceMatrixArePartOfTheLayoutKey)
{
    const std::vector<VertexElement> geometry = {VertexElement(0, F::Vector3, U::Position, 0),
                                                 VertexElement(12, F::Color, U::Color, 0)};
    GpuDrawParams params{};
    params.vertexColorEnabled = true;
    AddStream(params, 16);
    const auto single = BuildMetalDeclaredVertexInput(K::Colored16, geometry, 16, &params);
    const auto oneStream = BuildMetalStreamVertexInput(K::Colored16, params, Declarations({&geometry}));
    AddStream(params, 64, 1);
    const auto everyInstance = BuildMetalStreamVertexInput(K::Colored16, params, Declarations({&geometry, &kMatrixColumns}));
    params.vertexStreams[1].instanceFrequency = 3;
    const auto everyThird = BuildMetalStreamVertexInput(K::Colored16, params, Declarations({&geometry, &kMatrixColumns}));
    EXPECT_NE(single.LayoutKey(), oneStream.LayoutKey());
    EXPECT_NE(oneStream.LayoutKey(), everyInstance.LayoutKey());
    EXPECT_NE(everyInstance.LayoutKey(), everyThird.LayoutKey());
}

// Slot 0 keeps buffer 0; the others sit between the uniforms and the constant block.
TEST(MetalDeclaredVertexInput, StreamSlotsMapClearOfTheUniformAndConstantBuffers)
{
    EXPECT_EQ(MetalVertexStreamBufferIndex(0), 0);
    for (int slot = 1; slot < CNA::Internal::Renderers::kMaxVertexStreams; ++slot)
    {
        const int index = MetalVertexStreamBufferIndex(slot);
        EXPECT_GT(index, 3) << slot;
        EXPECT_LT(index, kMetalConstantAttributeBufferIndex) << slot;
    }
}
