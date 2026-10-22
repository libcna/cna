// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Graphics/PbrVertexStream.hpp"
#include <gtest/gtest.h>
using namespace CNA::Internal::Graphics;

namespace
{
    const std::vector<VertexElement> plain{
        {0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0},
        {12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0},
        {24, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0}};
    float Read(const std::vector<std::uint8_t>& data, std::size_t offset)
    {
        float result;
        std::memcpy(&result, data.data() + offset, 4);
        return result;
    }
} // namespace

TEST(PbrVertexStreamTest, MissingTangentUsesNeutralInputAndPreservesEveryVertex)
{
    const float vertices[]{1, 2, 3, 4, 5, 6, 7, 8, 11, 12, 13, 14, 15, 16, 17, 18};
    std::vector<std::uint8_t> source(sizeof(vertices)), out;
    std::memcpy(source.data(), vertices, sizeof(vertices));
    std::size_t stride = 32;
    ASSERT_TRUE(NormalizeRigidPbrStreamEXT(plain, source, stride, out));
    ASSERT_EQ(stride, 48u);
    ASSERT_EQ(out.size(), 96u);
    for (std::size_t i = 0; i < 2; ++i)
    {
        EXPECT_EQ(Read(out, i * 48), vertices[i * 8]);
        EXPECT_EQ(Read(out, i * 48 + 12), vertices[i * 8 + 3]);
        EXPECT_EQ(Read(out, i * 48 + 24), 0);
        EXPECT_EQ(Read(out, i * 48 + 28), 0);
        EXPECT_EQ(Read(out, i * 48 + 32), 0);
        EXPECT_EQ(Read(out, i * 48 + 36), 1);
        EXPECT_EQ(Read(out, i * 48 + 40), vertices[i * 8 + 6]);
        EXPECT_EQ(Read(out, i * 48 + 44), vertices[i * 8 + 7]);
    }
}

TEST(PbrVertexStreamTest, ReorderedPaddedDeclarationsAreResolvedBySemantic)
{
    const std::vector<VertexElement> reordered{
        {28, VertexElementFormat::Vector3, VertexElementUsage::Position, 0},
        {16, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0},
        {4, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0}};
    std::vector<std::uint8_t> source(48), out;
    const float value = 17;
    std::memcpy(source.data() + 28, &value, 4);
    std::size_t stride = 48;
    ASSERT_TRUE(NormalizeRigidPbrStreamEXT(reordered, source, stride, out));
    ASSERT_EQ(out.size(), 48u);
    EXPECT_EQ(Read(out, 0), 17);
    EXPECT_EQ(Read(out, 36), 1);
}

TEST(PbrVertexStreamTest, CanonicalStreamKeepsResidentStorage)
{
    auto declaration = plain;
    declaration[2] =
        VertexElement(40, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0);
    declaration.emplace_back(24, VertexElementFormat::Vector4, VertexElementUsage::Tangent, 0);
    for (bool colored : {false, true})
    {
        std::size_t stride = colored ? 60 : 48;
        if (colored)
        {
            declaration.emplace_back(48, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate,
                                     1);
            declaration.emplace_back(56, VertexElementFormat::Color, VertexElementUsage::Color, 0);
        }
        std::vector<std::uint8_t> source(stride), out;
        ASSERT_TRUE(NormalizeRigidPbrStreamEXT(declaration, source, stride, out));
        EXPECT_TRUE(out.empty());
    }
}

TEST(PbrVertexStreamTest, ExtraUvAndColorSelectExtendedRecordWithoutDroppingData)
{
    auto declaration = plain;
    declaration.emplace_back(32, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 1);
    std::vector<std::uint8_t> source(44), out;
    source[40] = 12;
    source[41] = 34;
    source[42] = 56;
    source[43] = 78;
    for (bool colored : {false, true})
    {
        if (colored)
            declaration.emplace_back(40, VertexElementFormat::Color, VertexElementUsage::Color, 0);
        std::size_t stride = 44;
        out.clear();
        ASSERT_TRUE(NormalizeRigidPbrStreamEXT(declaration, source, stride, out));
        ASSERT_EQ(stride, 60u);
        ASSERT_EQ(out.size(), 60u);
        EXPECT_EQ(out[56], colored ? 12 : 255);
        EXPECT_EQ(out[59], colored ? 78 : 255);
    }
}

TEST(PbrVertexStreamTest, MalformedOrUnsupportedInputsRefuseBeforeCopying)
{
    for (int bad = 0; bad < 5; ++bad)
    {
        auto declaration = plain;
        std::size_t stride = 32;
        std::vector<std::uint8_t> source(32), out;
        if (bad == 0)
            stride = 0;
        if (bad == 1)
            source.pop_back();
        if (bad == 2)
            declaration.erase(declaration.begin());
        if (bad == 3)
            declaration[0] = VertexElement(31, VertexElementFormat::Vector3, VertexElementUsage::Position, 0);
        if (bad == 4)
            declaration.emplace_back(0, VertexElementFormat::Vector2, VertexElementUsage::Tangent, 0);
        EXPECT_FALSE(NormalizeRigidPbrStreamEXT(declaration, source, stride, out));
        EXPECT_TRUE(out.empty());
    }
}
