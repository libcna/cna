// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <cstdint>
#include <bit>
#include "System/Globalization/CultureInfo.hpp"
#include <locale>
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Alpha8.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Bgr565.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Bgra4444.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Bgra5551.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Byte4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfSingle.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfVector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfVector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/NormalizedByte2.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/NormalizedByte4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/NormalizedShort2.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/NormalizedShort4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Rg32.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Rgba1010102.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Rgba64.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Short2.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Short4.hpp"

using namespace Microsoft::Xna::Framework::Graphics::PackedVector;
namespace
{
    class CommaPunctuation final : public std::numpunct<char>
    {
        char do_decimal_point() const override { return ','; }
        char do_thousands_sep() const override { return '\''; }
        std::string do_grouping() const override { return "\3"; }
    };
    class GlobalLocale
    {
        std::locale saved_;
    public:
        explicit GlobalLocale(const std::locale& chosen) : saved_(std::locale()) { std::locale::global(chosen); }
        ~GlobalLocale() { std::locale::global(saved_); }
    };
    class CurrentCulture
    {
        System::Globalization::CultureInfo saved_ = System::Globalization::CultureInfo::getCurrentCultureProperty();
    public:
        explicit CurrentCulture(const char* name)
        {
            System::Globalization::CultureInfo::setCurrentCultureProperty(System::Globalization::CultureInfo(name));
        }
        ~CurrentCulture() { System::Globalization::CultureInfo::setCurrentCultureProperty(saved_); }
    };
    template<class T> T Raw(std::uint64_t word)
    {
        T value;
        value.setPackedValueProperty(static_cast<decltype(value.getPackedValueProperty())>(word));
        return value;
    }
}
TEST(Tq040PackedFormatting, HexUsesInvariantCultureForEveryIntegerFormat)
{
    const GlobalLocale restore(std::locale(std::locale::classic(),new CommaPunctuation));
    // Each XNA integer ToString IL_0006..0015 uses X2/X4/X8/X16 + InvariantCulture.
    // Grouping and decimal punctuation cannot alter this literal hexadecimal contract.
    EXPECT_EQ(Raw<Alpha8>(0xabULL).ToString(),"AB");
    EXPECT_EQ(Raw<Bgr565>(0xabcdULL).ToString(),"ABCD");
    EXPECT_EQ(Raw<Bgra4444>(0xabcdULL).ToString(),"ABCD");
    EXPECT_EQ(Raw<Bgra5551>(0xabcdULL).ToString(),"ABCD");
    EXPECT_EQ(Raw<Byte4>(0x1234abcdULL).ToString(),"1234ABCD");
    EXPECT_EQ(Raw<NormalizedByte2>(0xabcdULL).ToString(),"ABCD");
    EXPECT_EQ(Raw<NormalizedByte4>(0x1234abcdULL).ToString(),"1234ABCD");
    EXPECT_EQ(Raw<NormalizedShort2>(0x1234abcdULL).ToString(),"1234ABCD");
    EXPECT_EQ(Raw<NormalizedShort4>(0x123456789abcdef0ULL).ToString(),"123456789ABCDEF0");
    EXPECT_EQ(Raw<Rg32>(0x1234abcdULL).ToString(),"1234ABCD");
    EXPECT_EQ(Raw<Rgba1010102>(0x1234abcdULL).ToString(),"1234ABCD");
    EXPECT_EQ(Raw<Rgba64>(0x123456789abcdef0ULL).ToString(),"123456789ABCDEF0");
    EXPECT_EQ(Raw<Short2>(0x1234abcdULL).ToString(),"1234ABCD");
    EXPECT_EQ(Raw<Short4>(0x123456789abcdef0ULL).ToString(),"123456789ABCDEF0");
}
TEST(Tq040PackedFormatting, HalfSingleUsesInvariantDecimalAndSevenSignificantDigits)
{
    const GlobalLocale restore(std::locale(std::locale::classic(),new CommaPunctuation));
    const CurrentCulture culture("de-DE");
    // HalfSingle.ToString IL_0001..0013 calls Single.ToString(InvariantCulture).
    // .NET Framework Single general format is G7: 1+2^-10=1.0009765625 -> 1.000977.
    EXPECT_EQ(Raw<HalfSingle>(0x3e00).ToString(),"1.5");
    EXPECT_EQ(Raw<HalfSingle>(0x3c01).ToString(),"1.000977");
    // 2^-24=5.9604644775390625e-8 -> G7 5.960464E-08.
    EXPECT_EQ(Raw<HalfSingle>(0x0001).ToString(),"5.960464E-08");
}
TEST(Tq040PackedFormatting, HalfVectorsUseSevenSignificantDigitsThroughTheVectorFormatter)
{
    const GlobalLocale restore(std::locale::classic());
    const CurrentCulture culture("");
    // HalfVector2/4.ToString IL_0001..0014 call the expanded vector's ToString.
    // Vector2.ToString IL_001b..002b / Vector4 IL_001b..0049 use Single's general G7.
    EXPECT_EQ(Raw<HalfVector2>(0xbc013c01).ToString(),"{X:1.000977 Y:-1.000977}");
    EXPECT_EQ(Raw<HalfVector4>(0x00013c01bc013c01ULL).ToString(),
              "{X:1.000977 Y:-1.000977 Z:1.000977 W:5.960464E-08}");
}
TEST(Tq040PackedFormatting, HalfVectorsRetainCurrentCultureDecimalSeparator)
{
    const GlobalLocale restore(std::locale::classic());
    const CurrentCulture culture("de-DE");
    // Vector2/4.ToString IL_0000..0007 explicitly select CurrentCulture, not invariant.
    EXPECT_EQ(Raw<HalfVector2>(0xbe003e00).ToString(),"{X:1,5 Y:-1,5}");
    EXPECT_EQ(Raw<HalfVector4>(0xbe003e00be003e00ULL).ToString(),"{X:1,5 Y:-1,5 Z:1,5 W:-1,5}");
}

TEST(Tq040PackedFormatting, NegativeZeroTextIsCanonicalWithoutChangingPackedBits)
{
    const GlobalLocale restore(std::locale::classic());
    const CurrentCulture culture("");
    // XNA HalfSingle.ToString IL_0009..0013 calls .NET Framework 4 Single.ToString:
    // either zero sign displays as "0" (the "-0" spelling is a .NET Core 3+ change).
    const auto scalar = Raw<HalfSingle>(0x8000);
    EXPECT_EQ(scalar.ToString(), "0");
    EXPECT_EQ(scalar.getPackedValueProperty(), 0x8000);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(scalar.ToSingle()), 0x80000000u);
    // HalfVector2/4.ToString IL_0001..0014 delegate to vector formatting with the same rule.
    const auto pair = Raw<HalfVector2>(0x80008000u);
    EXPECT_EQ(pair.ToString(), "{X:0 Y:0}");
    EXPECT_EQ(pair.getPackedValueProperty(), 0x80008000u);
    const auto quad = Raw<HalfVector4>(0x8000800080008000ULL);
    EXPECT_EQ(quad.ToString(), "{X:0 Y:0 Z:0 W:0}");
    EXPECT_EQ(quad.getPackedValueProperty(), 0x8000800080008000ULL);
}
