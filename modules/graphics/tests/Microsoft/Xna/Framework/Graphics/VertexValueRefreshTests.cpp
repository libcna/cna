// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <vector>
#include <any>
#include <bit>
#include <cstdint>
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace {
// Refresh each vertex op_Equality compares every logical field; Equals(object)
// IL_0000-0035 rejects null/wrong types and delegates, op_Inequality IL_0000-000a negates.
template<class T>
void ExpectOneComponent(const T& zero, const T& changed, std::size_t rawWord)
{
    const T copied = changed;
    EXPECT_TRUE(changed == copied);
    EXPECT_FALSE(changed != copied);
    EXPECT_TRUE(changed.Equals(copied));
    EXPECT_TRUE(changed.Equals(std::any(copied)));
    EXPECT_FALSE(zero == changed);
    EXPECT_FALSE(changed == zero);
    EXPECT_TRUE(zero != changed);
    EXPECT_TRUE(changed != zero);
    EXPECT_FALSE(zero.Equals(changed));
    EXPECT_FALSE(changed.Equals(zero));
    EXPECT_FALSE(zero.Equals(std::any(changed)));
    EXPECT_FALSE(changed.Equals(std::any(zero)));
    // Refresh vertex GetHashCode IL_0000-0010 calls Helpers.SmartGetHashCode:
    // Framework IL_0028-003b XORs raw words, IL_003d-0048 maps zero to 0x7fffffff.
    // One nonzero word XOR zero = that word; no CNA output supplies this oracle.
    EXPECT_EQ(changed.GetHashCode(), rawWord);
    EXPECT_EQ(copied.GetHashCode(), rawWord);
}
}

TEST(VertexValueRefreshTest, VertexPositionColorChecksEveryComponentAndHashWord)
{
    const VertexPositionColor zero;
    {
        SCOPED_TRACE("Position.X");
        auto changed = zero;
        changed.Position.X = 1.0f; // IEEE binary32 1 = 0x3f800000 exactly.
        ExpectOneComponent(zero, changed, 0x3f800000U);
    }
    {
        SCOPED_TRACE("Position.Y");
        auto changed = zero;
        changed.Position.Y = 1.0f; // IEEE binary32 1 = 0x3f800000 exactly.
        ExpectOneComponent(zero, changed, 0x3f800000U);
    }
    {
        SCOPED_TRACE("Position.Z");
        auto changed = zero;
        changed.Position.Z = 1.0f; // IEEE binary32 1 = 0x3f800000 exactly.
        ExpectOneComponent(zero, changed, 0x3f800000U);
    }
    {
        auto changed = zero;
        changed.Color = Color(1, 0, 0, 0); // Packed R | G<<8 | B<<16 | A<<24.
        ExpectOneComponent(zero, changed, 0x00000001U);
    }
    {
        auto changed = zero;
        changed.Color = Color(0, 1, 0, 0); // Packed R | G<<8 | B<<16 | A<<24.
        ExpectOneComponent(zero, changed, 0x00000100U);
    }
    {
        auto changed = zero;
        changed.Color = Color(0, 0, 1, 0); // Packed R | G<<8 | B<<16 | A<<24.
        ExpectOneComponent(zero, changed, 0x00010000U);
    }
    {
        auto changed = zero;
        changed.Color = Color(0, 0, 0, 1); // Packed R | G<<8 | B<<16 | A<<24.
        ExpectOneComponent(zero, changed, 0x01000000U);
    }
}

TEST(VertexValueRefreshTest, VertexPositionTextureChecksEveryComponentAndHashWord)
{
    const VertexPositionTexture zero;
    {
        SCOPED_TRACE("Position.X");
        auto changed = zero;
        changed.Position.X = 1.0f; // IEEE binary32 1 = 0x3f800000 exactly.
        ExpectOneComponent(zero, changed, 0x3f800000U);
    }
    {
        SCOPED_TRACE("Position.Y");
        auto changed = zero;
        changed.Position.Y = 1.0f; // IEEE binary32 1 = 0x3f800000 exactly.
        ExpectOneComponent(zero, changed, 0x3f800000U);
    }
    {
        SCOPED_TRACE("Position.Z");
        auto changed = zero;
        changed.Position.Z = 1.0f; // IEEE binary32 1 = 0x3f800000 exactly.
        ExpectOneComponent(zero, changed, 0x3f800000U);
    }
    {
        SCOPED_TRACE("TextureCoordinate.X");
        auto changed = zero;
        changed.TextureCoordinate.X = 1.0f; // IEEE binary32 1 = 0x3f800000 exactly.
        ExpectOneComponent(zero, changed, 0x3f800000U);
    }
    {
        SCOPED_TRACE("TextureCoordinate.Y");
        auto changed = zero;
        changed.TextureCoordinate.Y = 1.0f; // IEEE binary32 1 = 0x3f800000 exactly.
        ExpectOneComponent(zero, changed, 0x3f800000U);
    }
}

TEST(VertexValueRefreshTest, VertexPositionColorTextureChecksEveryComponentAndHashWord)
{
    const VertexPositionColorTexture zero;
    {
        SCOPED_TRACE("Position.X");
        auto changed = zero;
        changed.Position.X = 1.0f; // IEEE binary32 1 = 0x3f800000 exactly.
        ExpectOneComponent(zero, changed, 0x3f800000U);
    }
    {
        SCOPED_TRACE("Position.Y");
        auto changed = zero;
        changed.Position.Y = 1.0f; // IEEE binary32 1 = 0x3f800000 exactly.
        ExpectOneComponent(zero, changed, 0x3f800000U);
    }
    {
        SCOPED_TRACE("Position.Z");
        auto changed = zero;
        changed.Position.Z = 1.0f; // IEEE binary32 1 = 0x3f800000 exactly.
        ExpectOneComponent(zero, changed, 0x3f800000U);
    }
    {
        SCOPED_TRACE("TextureCoordinate.X");
        auto changed = zero;
        changed.TextureCoordinate.X = 1.0f; // IEEE binary32 1 = 0x3f800000 exactly.
        ExpectOneComponent(zero, changed, 0x3f800000U);
    }
    {
        SCOPED_TRACE("TextureCoordinate.Y");
        auto changed = zero;
        changed.TextureCoordinate.Y = 1.0f; // IEEE binary32 1 = 0x3f800000 exactly.
        ExpectOneComponent(zero, changed, 0x3f800000U);
    }
    {
        auto changed = zero;
        changed.Color = Color(1, 0, 0, 0); // Packed R | G<<8 | B<<16 | A<<24.
        ExpectOneComponent(zero, changed, 0x00000001U);
    }
    {
        auto changed = zero;
        changed.Color = Color(0, 1, 0, 0); // Packed R | G<<8 | B<<16 | A<<24.
        ExpectOneComponent(zero, changed, 0x00000100U);
    }
    {
        auto changed = zero;
        changed.Color = Color(0, 0, 1, 0); // Packed R | G<<8 | B<<16 | A<<24.
        ExpectOneComponent(zero, changed, 0x00010000U);
    }
    {
        auto changed = zero;
        changed.Color = Color(0, 0, 0, 1); // Packed R | G<<8 | B<<16 | A<<24.
        ExpectOneComponent(zero, changed, 0x01000000U);
    }
}

TEST(VertexValueRefreshTest, VertexPositionNormalTextureChecksEveryComponentAndHashWord)
{
    const VertexPositionNormalTexture zero;
    {
        SCOPED_TRACE("Position.X");
        auto changed = zero;
        changed.Position.X = 1.0f; // IEEE binary32 1 = 0x3f800000 exactly.
        ExpectOneComponent(zero, changed, 0x3f800000U);
    }
    {
        SCOPED_TRACE("Position.Y");
        auto changed = zero;
        changed.Position.Y = 1.0f; // IEEE binary32 1 = 0x3f800000 exactly.
        ExpectOneComponent(zero, changed, 0x3f800000U);
    }
    {
        SCOPED_TRACE("Position.Z");
        auto changed = zero;
        changed.Position.Z = 1.0f; // IEEE binary32 1 = 0x3f800000 exactly.
        ExpectOneComponent(zero, changed, 0x3f800000U);
    }
    {
        SCOPED_TRACE("Normal.X");
        auto changed = zero;
        changed.Normal.X = 1.0f; // IEEE binary32 1 = 0x3f800000 exactly.
        ExpectOneComponent(zero, changed, 0x3f800000U);
    }
    {
        SCOPED_TRACE("Normal.Y");
        auto changed = zero;
        changed.Normal.Y = 1.0f; // IEEE binary32 1 = 0x3f800000 exactly.
        ExpectOneComponent(zero, changed, 0x3f800000U);
    }
    {
        SCOPED_TRACE("Normal.Z");
        auto changed = zero;
        changed.Normal.Z = 1.0f; // IEEE binary32 1 = 0x3f800000 exactly.
        ExpectOneComponent(zero, changed, 0x3f800000U);
    }
    {
        SCOPED_TRACE("TextureCoordinate.X");
        auto changed = zero;
        changed.TextureCoordinate.X = 1.0f; // IEEE binary32 1 = 0x3f800000 exactly.
        ExpectOneComponent(zero, changed, 0x3f800000U);
    }
    {
        SCOPED_TRACE("TextureCoordinate.Y");
        auto changed = zero;
        changed.TextureCoordinate.Y = 1.0f; // IEEE binary32 1 = 0x3f800000 exactly.
        ExpectOneComponent(zero, changed, 0x3f800000U);
    }
}

// Refresh GetHashCode uses raw bytes, while vertex equality calls Vector3/Vector2
// op_Equality (ceq/bne.un on singles). Signed zeros compare equal; NaNs compare unequal.
TEST(VertexValueRefreshTest, SignedZeroAndNanKeepRefreshEqualityAndRawHashSemantics)
{
    {
        SCOPED_TRACE("VertexPositionColor");
        const VertexPositionColor zero;
        auto negativeZero = zero;
        negativeZero.Position.X = std::bit_cast<float>(std::uint32_t{0x80000000U});
        EXPECT_TRUE(zero == negativeZero);
        EXPECT_FALSE(zero != negativeZero);
        EXPECT_TRUE(zero.Equals(std::any(negativeZero)));
        EXPECT_EQ(negativeZero.GetHashCode(), 0x80000000U);
        auto nan = zero;
        nan.Position.X = std::bit_cast<float>(std::uint32_t{0x7fc01234U});
        EXPECT_FALSE(nan == nan);
        EXPECT_TRUE(nan != nan);
        EXPECT_FALSE(nan.Equals(nan));
        EXPECT_FALSE(nan.Equals(std::any(nan)));
        EXPECT_EQ(nan.GetHashCode(), 0x7fc01234U);
    }
    {
        SCOPED_TRACE("VertexPositionTexture");
        const VertexPositionTexture zero;
        auto negativeZero = zero;
        negativeZero.Position.X = std::bit_cast<float>(std::uint32_t{0x80000000U});
        EXPECT_TRUE(zero == negativeZero);
        EXPECT_FALSE(zero != negativeZero);
        EXPECT_TRUE(zero.Equals(std::any(negativeZero)));
        EXPECT_EQ(negativeZero.GetHashCode(), 0x80000000U);
        auto nan = zero;
        nan.Position.X = std::bit_cast<float>(std::uint32_t{0x7fc01234U});
        EXPECT_FALSE(nan == nan);
        EXPECT_TRUE(nan != nan);
        EXPECT_FALSE(nan.Equals(nan));
        EXPECT_FALSE(nan.Equals(std::any(nan)));
        EXPECT_EQ(nan.GetHashCode(), 0x7fc01234U);
    }
    {
        SCOPED_TRACE("VertexPositionColorTexture");
        const VertexPositionColorTexture zero;
        auto negativeZero = zero;
        negativeZero.Position.X = std::bit_cast<float>(std::uint32_t{0x80000000U});
        EXPECT_TRUE(zero == negativeZero);
        EXPECT_FALSE(zero != negativeZero);
        EXPECT_TRUE(zero.Equals(std::any(negativeZero)));
        EXPECT_EQ(negativeZero.GetHashCode(), 0x80000000U);
        auto nan = zero;
        nan.Position.X = std::bit_cast<float>(std::uint32_t{0x7fc01234U});
        EXPECT_FALSE(nan == nan);
        EXPECT_TRUE(nan != nan);
        EXPECT_FALSE(nan.Equals(nan));
        EXPECT_FALSE(nan.Equals(std::any(nan)));
        EXPECT_EQ(nan.GetHashCode(), 0x7fc01234U);
    }
    {
        SCOPED_TRACE("VertexPositionNormalTexture");
        const VertexPositionNormalTexture zero;
        auto negativeZero = zero;
        negativeZero.Position.X = std::bit_cast<float>(std::uint32_t{0x80000000U});
        EXPECT_TRUE(zero == negativeZero);
        EXPECT_FALSE(zero != negativeZero);
        EXPECT_TRUE(zero.Equals(std::any(negativeZero)));
        EXPECT_EQ(negativeZero.GetHashCode(), 0x80000000U);
        auto nan = zero;
        nan.Position.X = std::bit_cast<float>(std::uint32_t{0x7fc01234U});
        EXPECT_FALSE(nan == nan);
        EXPECT_TRUE(nan != nan);
        EXPECT_FALSE(nan.Equals(nan));
        EXPECT_FALSE(nan.Equals(std::any(nan)));
        EXPECT_EQ(nan.GetHashCode(), 0x7fc01234U);
    }
}

// Refresh VertexElement.GetHashCode IL_0000-0010 delegates to the same raw-word XOR.
TEST(VertexValueRefreshTest, ElementHashIncludesAllFourWordsAndNonzeroCancellationUsesSentinel)
{
    EXPECT_EQ(VertexElement(1, VertexElementFormat::Single, VertexElementUsage::Position, 0).GetHashCode(), 1U);
    EXPECT_EQ(VertexElement(0, VertexElementFormat::Vector2, VertexElementUsage::Position, 0).GetHashCode(), 1U);
    EXPECT_EQ(VertexElement(0, VertexElementFormat::Single, VertexElementUsage::Color, 0).GetHashCode(), 1U);
    EXPECT_EQ(VertexElement(0, VertexElementFormat::Single, VertexElementUsage::Position, 1).GetHashCode(), 1U);
    EXPECT_EQ(VertexElement(-1, VertexElementFormat::Single, VertexElementUsage::Position, 0).GetHashCode(), 0xffffffffU);
    // 4 XOR Color(4) XOR Position(0) XOR index(0) = 0, mapped to INT32_MAX.
    EXPECT_EQ(VertexElement(4, VertexElementFormat::Color, VertexElementUsage::Position, 0).GetHashCode(), 0x7fffffffU);
    // Two raw 1.0f words cancel even though this vertex is nonzero.
    EXPECT_EQ(VertexPositionTexture(Vector3(1, 1, 0), Vector2(0, 0)).GetHashCode(), 0x7fffffffU);
}

// Refresh ToString: PC/PT IL_0000-0033, PCT/PNT IL_0000-0041.
// Framework Vector3.ToString IL_0000-0046, Vector2 IL_0000-0037,
// Color IL_0000-004f provide the nested formats. Integers and halves are exact binary32;
// these literals need no tolerance and have terminating decimal representations.
TEST(VertexValueRefreshTest, ConstructorsAndWholeStringsPreserveEveryFieldInOrder)
{
    const Vector3 position(1, -2, 3);
    const Color color(10, 20, 30, 40);
    const Vector2 uv(0.5f, -0.25f);
    EXPECT_EQ(VertexPositionColor(position, color).ToString(),
        "{Position:{X:1 Y:-2 Z:3} Color:{R:10 G:20 B:30 A:40}}");
    EXPECT_EQ(VertexPositionTexture(position, uv).ToString(),
        "{Position:{X:1 Y:-2 Z:3} TextureCoordinate:{X:0.5 Y:-0.25}}");
    EXPECT_EQ(VertexPositionColorTexture(position, color, uv).ToString(),
        "{Position:{X:1 Y:-2 Z:3} Color:{R:10 G:20 B:30 A:40} TextureCoordinate:{X:0.5 Y:-0.25}}");
    EXPECT_EQ(VertexPositionNormalTexture(position, Vector3(4, -5, 6), uv).ToString(),
        "{Position:{X:1 Y:-2 Z:3} Normal:{X:4 Y:-5 Z:6} TextureCoordinate:{X:0.5 Y:-0.25}}");
}

// Refresh VertexElement.ToString IL_0000-004f boxes the two enums;
// VertexElementFormat/VertexElementUsage literal fields define every spelling.
TEST(VertexValueRefreshTest, ElementEnumNamesAreCompleteAndLiteral)
{
    const char* formats[]{"Single", "Vector2", "Vector3", "Vector4", "Color", "Byte4", "Short2", "Short4", "NormalizedShort2", "NormalizedShort4", "HalfVector2", "HalfVector4"};
    const char* usages[]{"Position", "Color", "TextureCoordinate", "Normal", "Binormal", "Tangent", "BlendIndices", "BlendWeight", "Depth", "Fog", "PointSize", "Sample", "TessellateFactor"};
    for (int i = 0; i < 12; ++i) {
        const VertexElement element(12, static_cast<VertexElementFormat>(i), VertexElementUsage::Normal, 7);
        EXPECT_EQ(element.ToString(), std::string("{Offset:12 Format:") + formats[i] + " Usage:Normal UsageIndex:7}");
    }
    for (int i = 0; i < 13; ++i) {
        const VertexElement element(12, VertexElementFormat::HalfVector4, static_cast<VertexElementUsage>(i), 7);
        EXPECT_EQ(element.ToString(), std::string("{Offset:12 Format:HalfVector4 Usage:") + usages[i] + " UsageIndex:7}");
    }
}

// Refresh sequential value-type zero initialization: no explicit default constructor;
// fields are Vector3, packed Color and Vector2 (PCT field declarations / .ctor IL_0000-0015).
TEST(VertexValueRefreshTest, VertexPositionColorTextureDefaultFieldsAreAllZero)
{
    const VertexPositionColorTexture value;
    EXPECT_EQ(std::bit_cast<std::uint32_t>(value.Position.X), 0U);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(value.Position.Y), 0U);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(value.Position.Z), 0U);
    EXPECT_EQ(value.Color.getPackedValueProperty(), 0U);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(value.TextureCoordinate.X), 0U);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(value.TextureCoordinate.Y), 0U);
}
