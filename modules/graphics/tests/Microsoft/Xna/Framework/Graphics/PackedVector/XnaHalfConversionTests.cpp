// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <bit>
#include <cstdint>
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfTypeHelper.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfSingle.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfVector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfVector4.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics::PackedVector;

TEST(XnaHalfConversionTest, UnpackUsesTheLiteralXnaHalfUtilsExponentMapping)
{
    // Microsoft.Xna.Framework 4.0 HalfUtils.Unpack IL_005f..IL_008a.
    struct Case { std::uint16_t half; std::uint32_t single; };
    const Case cases[] = {{0x0000,0x00000000}, {0x8000,0x80000000}, {0x0001,0x33800000},
        {0x03ff,0x387fc000}, {0x0400,0x38800000}, {0x3c00,0x3f800000}, {0x7bff,0x477fe000},
        {0x7c00,0x47800000}, {0x7e00,0x47c00000}, {0x7fff,0x47ffe000},
        {0xfc00,0xc7800000}, {0xfe00,0xc7c00000}, {0xffff,0xc7ffe000}};
    for (const auto& test : cases)
    {
        SCOPED_TRACE(test.half);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(HalfTypeHelper::Convert(test.half)), test.single);
        HalfSingle value; value.setPackedValueProperty(test.half);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(value.ToSingle()), test.single);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(value.ToVector4().X), test.single);
    }
}

TEST(XnaHalfConversionTest, PackUsesTheLiteralXnaHalfUtilsSaturationAndTieBoundaries)
{
    // HalfUtils.Pack's unsigned 47ffefff threshold saturates to sign|7fff.
    struct Case { std::uint32_t single; std::uint16_t half; };
    const Case cases[] = {{0x00000000,0x0000}, {0x80000000,0x8000}, {0x33000000,0x0000},
        {0x33c00000,0x0002}, {0x3f801000,0x3c00}, {0x3f803000,0x3c02},
        {0x477fe000,0x7bff}, {0x477ff000,0x7c00}, {0x47800000,0x7c00},
        {0x47c00000,0x7e00}, {0x47ffe000,0x7fff}, {0x47ffefff,0x7fff},
        {0x47fff000,0x7fff}, {0x7f800000,0x7fff}, {0xff800000,0xffff},
        {0x7fc00000,0x7fff}, {0xffc00000,0xffff}};
    for (const auto& test : cases)
    {
        SCOPED_TRACE(test.single);
        const float input = std::bit_cast<float>(test.single);
        EXPECT_EQ(HalfTypeHelper::Convert(input), test.half);
        EXPECT_EQ(HalfTypeHelper::Convert(std::bit_cast<std::int32_t>(test.single)), test.half);
        EXPECT_EQ(HalfSingle(input).getPackedValueProperty(), test.half);
        HalfSingle packed; packed.PackFromVector4(Vector4(input,0,0,0));
        EXPECT_EQ(packed.getPackedValueProperty(), test.half);
    }
}

TEST(XnaHalfConversionTest, VectorNaturalAndInterfacePathsPreserveTheExtendedCodes)
{
    HalfVector2 pair; pair.setPackedValueProperty(0x7ffffc00U);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(pair.ToVector2().X), 0xc7800000U);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(pair.ToVector2().Y), 0x47ffe000U);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(pair.ToVector4().Y), 0x47ffe000U);
    HalfVector2 pairCopy; pairCopy.PackFromVector4(pair.ToVector4());
    EXPECT_EQ(pairCopy.getPackedValueProperty(), 0x7ffffc00U);
    HalfVector4 quad; quad.setPackedValueProperty(0x7fff7e00fc007c00ULL);
    const auto v = quad.ToVector4();
    EXPECT_EQ(std::bit_cast<std::uint32_t>(v.X), 0x47800000U);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(v.Y), 0xc7800000U);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(v.Z), 0x47c00000U);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(v.W), 0x47ffe000U);
    HalfVector4 copy; copy.PackFromVector4(v);
    EXPECT_EQ(copy.getPackedValueProperty(), 0x7fff7e00fc007c00ULL);
    EXPECT_EQ(HalfVector4(v).getPackedValueProperty(), 0x7fff7e00fc007c00ULL);
    EXPECT_EQ(HalfVector4(v.X,v.Y,v.Z,v.W).getPackedValueProperty(), 0x7fff7e00fc007c00ULL);
}
