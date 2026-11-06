// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "CNA/Internal/Renderers/WebGPU/WebGPUFixedVertexStreams.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include <array>
#include <cstring>
#include <stdexcept>

using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::WebGPU;
namespace
{
    using Bytes = std::vector<std::uint8_t>;
    using Elements = std::vector<VertexElement>;
    using F = VertexElementFormat;
    using U = VertexElementUsage;
    Bytes Floats(std::initializer_list<float> values)
    {
        Bytes bytes(values.size() * 4);
        std::memcpy(bytes.data(), values.begin(), bytes.size());
        return bytes;
    }
    void Append(Bytes& into, const Bytes& tail) { into.insert(into.end(), tail.begin(), tail.end()); }
    const Elements geometry{{0,F::Vector3,U::Position,0}, {12,F::Vector3,U::Normal,0},
                            {24,F::Vector2,U::TextureCoordinate,0}};
    const Elements skin{{0,F::Vector4,U::BlendWeight,0}, {16,F::Byte4,U::BlendIndices,0}};
    // Literal input fields, deliberately different in all lanes/records. No CNA rendering oracle.
    Bytes Geometry() { return Floats({1,2,3, 0,0,-1, .25f,.75f}); }
    Bytes Skin()
    {
        auto bytes = Floats({.25f,.75f,0,0});
        Append(bytes, {2,1,0,0});
        return bytes;
    }
    Bytes CanonicalSkin()
    {
        // Declared P3 N3 UV2 W4 I4 => 12+12+8+16+4 = 52 bytes.
        auto bytes = Floats({1,2,3, 0,0,-1, .25f,.75f, .25f,.75f,0,0});
        Append(bytes, {2,1,0,0});
        return bytes;
    }
}

TEST(WebGPUFixedVertexStreams, CopiesSecondStreamWithIndependentElementOffsets)
{
    Bytes positions(32, 0xaa), bones(60, 0xbb);
    Append(positions, Geometry());
    Append(positions, Floats({4,5,6, 0,1,0, .5f,.25f}));
    Append(bones, Skin());
    auto nextBones = Floats({.75f,.25f,0,0}); Append(nextBones, {1,2,0,0});
    Append(bones, nextBones);
    const std::array<FixedVertexStreamEXT,2> streams{{
        {&geometry,&positions,32,1}, {&skin,&bones,20,3}}};
    const auto actual = NormalizeFixedVertexStreamsEXT(streams, FixedVertexFamilyEXT::Skinned);
    auto expected = CanonicalSkin();
    Append(expected, Floats({4,5,6, 0,1,0, .5f,.25f, .75f,.25f,0,0}));
    Append(expected, {1,2,0,0}); // Distinct record 2 also catches a missing per-vertex advance.
    EXPECT_EQ(actual.stride, 52u);
    EXPECT_EQ(actual.bytes, expected); // Each offset is multiplied by its OWN stride (32 and 20).
}

TEST(WebGPUFixedVertexStreams, ChangingOnlySecondStreamChangesCapturedWeightsAndIndices)
{
    auto positions = Geometry(), bones = Skin();
    const std::array<FixedVertexStreamEXT,2> streams{{
        {&geometry,&positions,32,0}, {&skin,&bones,20,0}}};
    const auto first = NormalizeFixedVertexStreamsEXT(streams, FixedVertexFamilyEXT::Skinned);
    bones = Floats({1,0,0,0}); Append(bones, {1,0,0,0});
    const auto second = NormalizeFixedVertexStreamsEXT(streams, FixedVertexFamilyEXT::Skinned);
    auto expected = Floats({1,2,3, 0,0,-1, .25f,.75f, 1,0,0,0});
    Append(expected, {1,0,0,0});
    EXPECT_EQ(first.bytes, CanonicalSkin());
    EXPECT_EQ(second.bytes, expected);
    positions.assign(32, 0); bones.assign(20, 0);
    EXPECT_EQ(first.bytes, CanonicalSkin()); // Queued draws own the bytes even after source reuse.
}

TEST(WebGPUFixedVertexStreams, SameStrideReorderedVector4IndicesHaveTheirOwnDeclaration)
{
    auto positions = Geometry();
    auto byteBones = Skin(); byteBones.resize(32, 0);
    const Elements floatSkin{{0,F::Vector4,U::BlendIndices,0}, {16,F::Vector4,U::BlendWeight,0}};
    auto floatBones = Floats({2,1,0,0, .25f,.75f,0,0});
    std::array<FixedVertexStreamEXT,2> streams{{
        {&geometry,&positions,32,0}, {&skin,&byteBones,32,0}}};
    EXPECT_EQ(NormalizeFixedVertexStreamsEXT(streams, FixedVertexFamilyEXT::Skinned).bytes,
              CanonicalSkin());
    streams[1] = {&floatSkin,&floatBones,32,0};
    const auto actual = NormalizeFixedVertexStreamsEXT(streams, FixedVertexFamilyEXT::Skinned);
    EXPECT_EQ(actual.bytes, CanonicalSkin()); // Small integer palette indices convert exactly.
    EXPECT_EQ(actual.stride, 52u);
}

TEST(WebGPUFixedVertexStreams, PbrFamiliesShareDeclaredFieldsAndNeutralTangent)
{
    auto positions = Geometry(), bones = Skin();
    const std::array<FixedVertexStreamEXT,2> streams{{
        {&geometry,&positions,32,0}, {&skin,&bones,20,0}}};
    // CNAEXT layout oracle: P3 N3 T4 UV2 [W4 I4]. Missing tangent is (0,0,0,1).
    auto expected = Floats({1,2,3, 0,0,-1, 0,0,0,1, .25f,.75f});
    const auto rigid = NormalizeFixedVertexStreamsEXT(streams, FixedVertexFamilyEXT::Pbr);
    EXPECT_EQ(rigid.stride, 48u); EXPECT_EQ(rigid.bytes, expected);
    Append(expected, Skin());
    const auto skinned = NormalizeFixedVertexStreamsEXT(streams, FixedVertexFamilyEXT::SkinnedPbr);
    EXPECT_EQ(skinned.stride, 68u); EXPECT_EQ(skinned.bytes, expected);
}

TEST(WebGPUFixedVertexStreams, ExtendedLayoutsCarrySecondStreamColorAndUvDefaults)
{
    auto positions = Geometry(), bones = Skin();
    const Elements color{{0,F::Color,U::Color,0}};
    const Elements uv1{{0,F::Vector2,U::TextureCoordinate,1}};
    Bytes colors{17,34,51,255}; auto secondUv = Floats({.5f,.25f});
    std::array<FixedVertexStreamEXT,3> streams{{
        {&geometry,&positions,32,0}, {&skin,&bones,20,0}, {&color,&colors,4,0}}};
    auto skinnedExpected = CanonicalSkin(); Append(skinnedExpected, colors);
    const auto skinned = NormalizeFixedVertexStreamsEXT(streams, FixedVertexFamilyEXT::Skinned);
    EXPECT_EQ(skinned.stride, 56u); EXPECT_EQ(skinned.bytes, skinnedExpected);
    auto pbrExpected = Floats({1,2,3, 0,0,-1, 0,0,0,1, .25f,.75f, 0,0});
    Append(pbrExpected, colors);
    auto rigid = NormalizeFixedVertexStreamsEXT(streams, FixedVertexFamilyEXT::Pbr);
    EXPECT_EQ(rigid.stride, 60u); EXPECT_EQ(rigid.bytes, pbrExpected);
    auto skinPbrExpected = Floats({1,2,3, 0,0,-1, 0,0,0,1, .25f,.75f});
    Append(skinPbrExpected, Skin()); Append(skinPbrExpected, Floats({0,0}));
    Append(skinPbrExpected, colors);
    const auto skinPbr = NormalizeFixedVertexStreamsEXT(streams, FixedVertexFamilyEXT::SkinnedPbr);
    EXPECT_EQ(skinPbr.stride, 80u); EXPECT_EQ(skinPbr.bytes, skinPbrExpected);
    streams[2] = {&uv1,&secondUv,8,0};
    auto uvExpected = Floats({1,2,3, 0,0,-1, 0,0,0,1, .25f,.75f, .5f,.25f});
    Append(uvExpected, {255,255,255,255});
    rigid = NormalizeFixedVertexStreamsEXT(streams, FixedVertexFamilyEXT::Pbr);
    EXPECT_EQ(rigid.stride, 60u); EXPECT_EQ(rigid.bytes, uvExpected);
}

TEST(WebGPUFixedVertexStreams, UnconsumedShortStreamDoesNotLimitTheVertexRange)
{
    auto positions = Geometry(), bones = Skin();
    const Elements unused{{0,F::Vector4,U::TextureCoordinate,7}};
    Bytes empty;
    const std::array<FixedVertexStreamEXT,3> streams{{
        {&geometry,&positions,32,0}, {&skin,&bones,20,0}, {&unused,&empty,16,100}}};
    EXPECT_EQ(NormalizeFixedVertexStreamsEXT(streams, FixedVertexFamilyEXT::Skinned).bytes,
              CanonicalSkin()); // Shader input liveness: TEXCOORD7 is never fetched.
}

TEST(WebGPUFixedVertexStreams, InvalidConsumedDeclarationsAndByteRangesAreRejected)
{
    auto positions = Geometry(), bones = Skin();
    auto bad = skin;
    std::array<FixedVertexStreamEXT,2> streams{{
        {&geometry,&positions,32,0}, {&bad,&bones,20,0}}};
    bad.pop_back(); // Required BLENDINDICES0 absent.
    EXPECT_THROW((void)NormalizeFixedVertexStreamsEXT(streams, FixedVertexFamilyEXT::Skinned), std::invalid_argument);
    bad = {{0,F::Vector3,U::BlendWeight,0}, {16,F::Byte4,U::BlendIndices,0}};
    EXPECT_THROW((void)NormalizeFixedVertexStreamsEXT(streams, FixedVertexFamilyEXT::Skinned), std::invalid_argument);
    bad = {{0,F::Vector4,U::BlendWeight,0}, {19,F::Byte4,U::BlendIndices,0}};
    EXPECT_THROW((void)NormalizeFixedVertexStreamsEXT(streams, FixedVertexFamilyEXT::Skinned), std::invalid_argument);
    bad = skin; bones.pop_back(); // Whole-record byte contract, not an out-of-bounds read.
    EXPECT_THROW((void)NormalizeFixedVertexStreamsEXT(streams, FixedVertexFamilyEXT::Skinned), std::invalid_argument);
    bones = Skin(); streams[1].vertexOffset = 2;
    EXPECT_THROW((void)NormalizeFixedVertexStreamsEXT(streams, FixedVertexFamilyEXT::Skinned), std::invalid_argument);
}

TEST(WebGPUFixedVertexStreams, PbrBasisInSecondStreamIsPreserved)
{
    const Elements position{{0,F::Vector3,U::Position,0}};
    const Elements basis{{0,F::Vector3,U::Normal,0}, {12,F::Vector4,U::Tangent,0},
                         {28,F::Vector2,U::TextureCoordinate,0}};
    auto p = Floats({1,2,3});
    auto b = Floats({0,1,0, 1,0,0,-1, .25f,.75f});
    auto bones = Skin();
    const std::array<FixedVertexStreamEXT,3> streams{{
        {&position,&p,12,0}, {&basis,&b,36,0}, {&skin,&bones,20,0}}};
    auto expected = Floats({1,2,3, 0,1,0, 1,0,0,-1, .25f,.75f});
    EXPECT_EQ(NormalizeFixedVertexStreamsEXT(streams, FixedVertexFamilyEXT::Pbr).bytes, expected);
    Append(expected, Skin());
    EXPECT_EQ(NormalizeFixedVertexStreamsEXT(streams, FixedVertexFamilyEXT::SkinnedPbr).bytes, expected);
    // Declared tangent sign is -1, not the neutral input's +1; every basis lane is literal.
}
