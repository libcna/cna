// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <limits>
#include <vector>
#include "Microsoft/Xna/Framework/Color.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Vector4;

// --- Construction ---

TEST(ColorTest, DefaultConstructorIsTransparentBlack)
{
    Color c;
    EXPECT_EQ(c.getRProperty(), 0);
    EXPECT_EQ(c.getGProperty(), 0);
    EXPECT_EQ(c.getBProperty(), 0);
    EXPECT_EQ(c.getAProperty(), 0);
    EXPECT_EQ(c.getPackedValueProperty(), 0u);
    EXPECT_EQ(c, Color::Transparent);
}

TEST(ColorTest, DefaultConstructedArrayElementsAreTransparentBlack)
{
    // C# `new Color[n]` fills the array with default(Color); the C++ counterpart must
    // give a value-initialized vector the same contents.
    std::vector<Color> colors(4);
    for (const Color& c : colors)
    {
        EXPECT_EQ(c.getPackedValueProperty(), 0u);
    }
}

TEST(ColorTest, ByteRgbConstructorSetsComponentsAndOpaqueAlpha)
{
    Color c(static_cast<uint8_t>(10), static_cast<uint8_t>(20), static_cast<uint8_t>(30));
    EXPECT_EQ(c.getRProperty(), 10);
    EXPECT_EQ(c.getGProperty(), 20);
    EXPECT_EQ(c.getBProperty(), 30);
    EXPECT_EQ(c.getAProperty(), 255);
}

TEST(ColorTest, ByteRgbaConstructorSetsAllComponents)
{
    Color c(static_cast<uint8_t>(1), static_cast<uint8_t>(2), static_cast<uint8_t>(3), static_cast<uint8_t>(4));
    EXPECT_EQ(c.getRProperty(), 1);
    EXPECT_EQ(c.getGProperty(), 2);
    EXPECT_EQ(c.getBProperty(), 3);
    EXPECT_EQ(c.getAProperty(), 4);
}

TEST(ColorTest, IntRgbConstructorClampsAndSetsComponents)
{
    Color c(255, 0, 128);
    EXPECT_EQ(c.getRProperty(), 255);
    EXPECT_EQ(c.getGProperty(), 0);
    EXPECT_EQ(c.getBProperty(), 128);
    EXPECT_EQ(c.getAProperty(), 255);
}

TEST(ColorTest, IntRgbaConstructorClampsComponents)
{
    Color c(300, -5, 100, 200);
    EXPECT_EQ(c.getRProperty(), 255);
    EXPECT_EQ(c.getGProperty(), 0);
    EXPECT_EQ(c.getBProperty(), 100);
    EXPECT_EQ(c.getAProperty(), 200);
}

TEST(ColorTest, FloatRgbConstructorScalesToByteRange)
{
    Color c(1.0f, 0.0f, 0.5f);
    EXPECT_EQ(c.getRProperty(), 255);
    EXPECT_EQ(c.getGProperty(), 0);
    EXPECT_EQ(c.getAProperty(), 255);
}

TEST(ColorTest, FloatRgbaConstructorScalesToByteRange)
{
    Color c(0.0f, 1.0f, 0.0f, 1.0f);
    EXPECT_EQ(c.getRProperty(), 0);
    EXPECT_EQ(c.getGProperty(), 255);
    EXPECT_EQ(c.getBProperty(), 0);
    EXPECT_EQ(c.getAProperty(), 255);
}

// --- Static named colors ---

TEST(ColorTest, WhiteHasAllComponentsAtMax)
{
    EXPECT_EQ(Color::White.getRProperty(), 255);
    EXPECT_EQ(Color::White.getGProperty(), 255);
    EXPECT_EQ(Color::White.getBProperty(), 255);
    EXPECT_EQ(Color::White.getAProperty(), 255);
}

TEST(ColorTest, BlackHasRgbZeroAndOpaqueAlpha)
{
    EXPECT_EQ(Color::Black.getRProperty(), 0);
    EXPECT_EQ(Color::Black.getGProperty(), 0);
    EXPECT_EQ(Color::Black.getBProperty(), 0);
    EXPECT_EQ(Color::Black.getAProperty(), 255);
}

TEST(ColorTest, RedHasCorrectComponents)
{
    EXPECT_EQ(Color::Red.getRProperty(), 255);
    EXPECT_EQ(Color::Red.getGProperty(), 0);
    EXPECT_EQ(Color::Red.getBProperty(), 0);
    EXPECT_EQ(Color::Red.getAProperty(), 255);
}

TEST(ColorTest, TransparentHasZeroAlpha)
{
    EXPECT_EQ(Color::Transparent.getAProperty(), 0);
}

// --- Packed value layout (AABBGGRR) ---

TEST(ColorTest, RedPackedValueIsAabbggrr)
{
    EXPECT_EQ(Color::Red.getPackedValueProperty(), 0xFF0000FFu);
}

TEST(ColorTest, BluePackedValueIsAabbggrr)
{
    EXPECT_EQ(Color::Blue.getPackedValueProperty(), 0xFFFF0000u);
}

TEST(ColorTest, CornflowerBluePackedValue)
{
    // R:100 G:149 B:237 A:255 → 0xFFED9564
    EXPECT_EQ(Color::CornflowerBlue.getPackedValueProperty(), 0xFFED9564u);
}

// --- Equality operators ---

TEST(ColorTest, EqualColorsCompareEqual)
{
    Color a(100, 150, 200, 255);
    Color b(100, 150, 200, 255);
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
}

TEST(ColorTest, DifferentColorsCompareNotEqual)
{
    Color a(100, 150, 200, 255);
    Color b(100, 150, 201, 255);
    EXPECT_FALSE(a == b);
    EXPECT_TRUE(a != b);
}

// --- Lerp ---

TEST(ColorTest, LerpAtZeroReturnsFirstColor)
{
    Color result = Color::Lerp(Color::Black, Color::White, 0.0f);
    EXPECT_EQ(result.getRProperty(), 0);
    EXPECT_EQ(result.getGProperty(), 0);
    EXPECT_EQ(result.getBProperty(), 0);
}

TEST(ColorTest, LerpAtOneReturnsSecondColor)
{
    Color result = Color::Lerp(Color::Black, Color::White, 1.0f);
    EXPECT_EQ(result.getRProperty(), 255);
    EXPECT_EQ(result.getGProperty(), 255);
    EXPECT_EQ(result.getBProperty(), 255);
}

TEST(ColorTest, LerpAtHalfReturnsMidpoint)
{
    Color result = Color::Lerp(Color::Black, Color::White, 0.5f);
    EXPECT_EQ(result.getRProperty(), 127);
    EXPECT_EQ(result.getGProperty(), 127);
    EXPECT_EQ(result.getBProperty(), 127);
    EXPECT_EQ(result.getAProperty(), 255);
}

TEST(ColorTest, LerpClampsBelowZero)
{
    Color result = Color::Lerp(Color::Black, Color::White, -1.0f);
    EXPECT_EQ(result.getRProperty(), 0);
}

TEST(ColorTest, LerpClampsAboveOne)
{
    Color result = Color::Lerp(Color::Black, Color::White, 2.0f);
    EXPECT_EQ(result.getRProperty(), 255);
}

// REMED-CORE-004: MathHelper::Clamp(NaN, 0, 1) faithfully reproduces FNA's own
// comparison-based clamp, under which NaN passes through unclamped (matches
// MathHelper.Clamp's real C# implementation). A NaN `amount` previously reached a bare
// static_cast<intcs>(NaN) with no guard -- undefined behavior, reachable via this public API.
TEST(ColorTest, LerpWithNaNAmountIsDefinedNotUndefinedBehavior)
{
    Color result = Color::Lerp(Color::Black, Color::White, std::numeric_limits<float>::quiet_NaN());
    EXPECT_EQ(result.getRProperty(), 0);
    EXPECT_EQ(result.getGProperty(), 0);
    EXPECT_EQ(result.getBProperty(), 0);
    EXPECT_EQ(result.getAProperty(), 0);
}

TEST(ColorTest, LerpWithInfiniteAmountIsDefinedNotUndefinedBehavior)
{
    Color result = Color::Lerp(Color::Black, Color::White, std::numeric_limits<float>::infinity());
    EXPECT_EQ(result.getRProperty(), 255);
}

// --- Multiply ---

TEST(ColorTest, MultiplyByOnePreservesColor)
{
    Color result = Color::Multiply(Color::Red, 1.0f);
    EXPECT_EQ(result.getRProperty(), 255);
    EXPECT_EQ(result.getGProperty(), 0);
    EXPECT_EQ(result.getBProperty(), 0);
    EXPECT_EQ(result.getAProperty(), 255);
}

TEST(ColorTest, MultiplyByZeroGivesBlack)
{
    Color result = Color::Multiply(Color::Red, 0.0f);
    EXPECT_EQ(result.getRProperty(), 0);
    EXPECT_EQ(result.getAProperty(), 0);
}

TEST(ColorTest, MultiplyOperatorMatchesStaticMethod)
{
    Color a = Color::White * 0.5f;
    Color b = Color::Multiply(Color::White, 0.5f);
    EXPECT_EQ(a.getRProperty(), b.getRProperty());
    EXPECT_EQ(a.getAProperty(), b.getAProperty());
}

// REMED-CORE-004: Multiply shares Lerp's exact same root cause -- an unguarded
// static_cast<intcs>(component * scale) is undefined behavior whenever `scale` is NaN or
// produces a huge finite product. Multiply has no upfront clamp on `scale` at all (unlike
// Lerp's `amount`), so this was reachable for any caller-supplied scale, not just NaN misuse.
TEST(ColorTest, MultiplyWithNaNScaleIsDefinedNotUndefinedBehavior)
{
    Color result = Color::Multiply(Color::White, std::numeric_limits<float>::quiet_NaN());
    EXPECT_EQ(result.getRProperty(), 0);
    EXPECT_EQ(result.getGProperty(), 0);
    EXPECT_EQ(result.getBProperty(), 0);
    EXPECT_EQ(result.getAProperty(), 0);
}

TEST(ColorTest, MultiplyWithExtremeScaleIsDefinedNotUndefinedBehavior)
{
    Color result = Color::Multiply(Color::White, 1e30f);
    EXPECT_EQ(result.getRProperty(), 255);
}

TEST(ColorTest, MultiplyWithNegativeScaleIsDefinedNotUndefinedBehavior)
{
    Color result = Color::Multiply(Color::White, -1.0f);
    EXPECT_EQ(result.getRProperty(), 0);
}

// --- ToVector3 / ToVector4 ---

TEST(ColorTest, ToVector3NormalizesComponents)
{
    Vector3 v = Color::Red.ToVector3();
    EXPECT_FLOAT_EQ(v.X, 1.0f);
    EXPECT_FLOAT_EQ(v.Y, 0.0f);
    EXPECT_FLOAT_EQ(v.Z, 0.0f);
}

TEST(ColorTest, ToVector4IncludesAlpha)
{
    Vector4 v = Color::Red.ToVector4();
    EXPECT_FLOAT_EQ(v.X, 1.0f);
    EXPECT_FLOAT_EQ(v.Y, 0.0f);
    EXPECT_FLOAT_EQ(v.Z, 0.0f);
    EXPECT_FLOAT_EQ(v.W, 1.0f);
}

// --- FromNonPremultiplied ---

TEST(ColorTest, FromNonPremultipliedScalesRgbByAlpha)
{
    // R=255, G=0, B=0, A=128 → R=(255*128/255)=128, G=0, B=0, A=128
    Color c = Color::FromNonPremultiplied(255, 0, 0, 128);
    EXPECT_EQ(c.getRProperty(), 128);
    EXPECT_EQ(c.getGProperty(), 0);
    EXPECT_EQ(c.getBProperty(), 0);
    EXPECT_EQ(c.getAProperty(), 128);
}

TEST(ColorTest, FromNonPremultipliedVector4ScalesRgbByAlpha)
{
    // alpha=0.5 → each channel * 0.5, and 0.5 * 255 = 127.5 rounds to 128, not 127
    // (tests/reference/xna40/framework/framework-packing-oracle.json, color/vector4_quarters).
    Color c = Color::FromNonPremultiplied(Vector4(1.0f, 0.0f, 0.0f, 0.5f));
    EXPECT_EQ(c.getAProperty(), 128);
    EXPECT_GT(c.getRProperty(), 0);
    EXPECT_EQ(c.getGProperty(), 0);
    EXPECT_EQ(c.getBProperty(), 0);
}

// color/from_non_premultiplied_vector4 and color/from_non_premultiplied_ints: the float overload
// premultiplies before packing, so both overloads answer the same colour for the same input.
TEST(ColorTest, FromNonPremultipliedOverloadsAgreeWithXna)
{
    Color fromFloats = Color::FromNonPremultiplied(Vector4(1.0f, 0.0f, 0.25f, 0.5f));
    EXPECT_EQ(fromFloats.getRProperty(), 128);
    EXPECT_EQ(fromFloats.getGProperty(), 0);
    EXPECT_EQ(fromFloats.getBProperty(), 32);
    EXPECT_EQ(fromFloats.getAProperty(), 128);

    Color fromInts = Color::FromNonPremultiplied(255, 0, 64, 128);
    EXPECT_EQ(fromInts.getRProperty(), 128);
    EXPECT_EQ(fromInts.getGProperty(), 0);
    EXPECT_EQ(fromInts.getBProperty(), 32);
    EXPECT_EQ(fromInts.getAProperty(), 128);
}

// --- Vector4 / Vector3 constructors ---

TEST(ColorTest, Vector4ConstructorSetsAllComponents)
{
    Color c(Vector4(1.0f, 0.5f, 0.0f, 1.0f));
    EXPECT_EQ(c.getRProperty(), 255);
    EXPECT_EQ(c.getBProperty(), 0);
    EXPECT_EQ(c.getAProperty(), 255);
}

TEST(ColorTest, Vector3ConstructorSetsRgbAndOpaqueAlpha)
{
    Color c(Vector3(0.0f, 1.0f, 0.5f));
    EXPECT_EQ(c.getRProperty(), 0);
    EXPECT_EQ(c.getGProperty(), 255);
    EXPECT_EQ(c.getAProperty(), 255);
}

// --- Component setters ---

TEST(ColorTest, SetRPropertyUpdatesOnlyRedChannel)
{
    Color c = Color::White;
    c.setRProperty(static_cast<uint8_t>(10));
    EXPECT_EQ(c.getRProperty(), 10);
    EXPECT_EQ(c.getGProperty(), 255);
    EXPECT_EQ(c.getBProperty(), 255);
    EXPECT_EQ(c.getAProperty(), 255);
}

TEST(ColorTest, SetGPropertyUpdatesOnlyGreenChannel)
{
    Color c = Color::White;
    c.setGProperty(static_cast<uint8_t>(20));
    EXPECT_EQ(c.getRProperty(), 255);
    EXPECT_EQ(c.getGProperty(), 20);
    EXPECT_EQ(c.getBProperty(), 255);
    EXPECT_EQ(c.getAProperty(), 255);
}

TEST(ColorTest, SetBPropertyUpdatesOnlyBlueChannel)
{
    Color c = Color::White;
    c.setBProperty(static_cast<uint8_t>(30));
    EXPECT_EQ(c.getRProperty(), 255);
    EXPECT_EQ(c.getGProperty(), 255);
    EXPECT_EQ(c.getBProperty(), 30);
    EXPECT_EQ(c.getAProperty(), 255);
}

TEST(ColorTest, SetAPropertyUpdatesOnlyAlphaChannel)
{
    Color c = Color::White;
    c.setAProperty(static_cast<uint8_t>(0));
    EXPECT_EQ(c.getRProperty(), 255);
    EXPECT_EQ(c.getGProperty(), 255);
    EXPECT_EQ(c.getBProperty(), 255);
    EXPECT_EQ(c.getAProperty(), 0);
}

// --- PackedValue setter ---

TEST(ColorTest, SetPackedValuePropertyRoundTrips)
{
    Color c(0, 0, 0, 0);
    c.setPackedValueProperty(0xFF0000FFu);
    EXPECT_EQ(c.getPackedValueProperty(), 0xFF0000FFu);
    EXPECT_EQ(c.getRProperty(), 255);
    EXPECT_EQ(c.getGProperty(), 0);
    EXPECT_EQ(c.getBProperty(), 0);
    EXPECT_EQ(c.getAProperty(), 255);
}

// --- Equals (direct call) ---

TEST(ColorTest, EqualsReturnsTrueForSameComponents)
{
    Color a(50, 100, 150, 200);
    Color b(50, 100, 150, 200);
    EXPECT_TRUE(a.Equals(b));
}

TEST(ColorTest, EqualsReturnsFalseForDifferentComponents)
{
    Color a(50, 100, 150, 200);
    Color b(50, 100, 150, 201);
    EXPECT_FALSE(a.Equals(b));
}

// --- GetHashCode ---

TEST(ColorTest, GetHashCodeEqualForEqualColors)
{
    Color a(10, 20, 30, 40);
    Color b(10, 20, 30, 40);
    EXPECT_EQ(a.GetHashCode(), b.GetHashCode());
}

TEST(ColorTest, GetHashCodeDifferentForDifferentColors)
{
    Color a(10, 20, 30, 40);
    Color b(10, 20, 30, 41);
    EXPECT_NE(a.GetHashCode(), b.GetHashCode());
}

// --- ToString ---

TEST(ColorTest, ToStringHasExpectedFormat)
{
    Color c(255, 0, 128, 255);
    std::string s = c.ToString();
    EXPECT_NE(s.find("R:255"), std::string::npos);
    EXPECT_NE(s.find("G:0"),   std::string::npos);
    EXPECT_NE(s.find("B:128"), std::string::npos);
    EXPECT_NE(s.find("A:255"), std::string::npos);
}

// --- PackFromVector4 (IPackedVector) ---

TEST(ColorTest, PackFromVector4SetsComponents)
{
    Color c(0, 0, 0, 0);
    c.PackFromVector4(Vector4(1.0f, 0.0f, 0.0f, 1.0f));
    EXPECT_EQ(c.getRProperty(), 255);
    EXPECT_EQ(c.getGProperty(), 0);
    EXPECT_EQ(c.getBProperty(), 0);
    EXPECT_EQ(c.getAProperty(), 255);
}

// REMED-CORE-004, revised against the XNA 4.0 runtime itself
// (tests/reference/xna40/framework/framework-packing-oracle.json, cases color/packfromvector4_*):
// XNA's IPackedVector.PackFromVector4 saturates and rounds exactly like the Color(Vector4)
// constructor, so an out-of-range component clamps at 0/255 rather than wrapping. FNA's does not
// -- "R = (byte) (vector.X * 255.0f);" (Color.cs) neither clamps nor rounds, and produced the
// wraparound these tests used to pin. XNA wins (CLAUDE.md; plans/plan_bindings_upstream.md).
// A non-finite component stays defined here, unlike a bare C++ cast: NaN packs as 0, which is
// what the runtime produces, and the infinities saturate with everything else.
TEST(ColorTest, PackFromVector4AtZeroGivesZero)
{
    Color c(255, 255, 255, 255);
    c.PackFromVector4(Vector4(0.0f, 0.0f, 0.0f, 0.0f));
    EXPECT_EQ(c.getRProperty(), 0);
    EXPECT_EQ(c.getGProperty(), 0);
    EXPECT_EQ(c.getBProperty(), 0);
    EXPECT_EQ(c.getAProperty(), 0);
}

TEST(ColorTest, PackFromVector4AtOneGivesTwoFiftyFive)
{
    Color c(0, 0, 0, 0);
    c.PackFromVector4(Vector4(1.0f, 1.0f, 1.0f, 1.0f));
    EXPECT_EQ(c.getRProperty(), 255);
    EXPECT_EQ(c.getGProperty(), 255);
    EXPECT_EQ(c.getBProperty(), 255);
    EXPECT_EQ(c.getAProperty(), 255);
}

TEST(ColorTest, PackFromVector4RoundsAndSaturatesLikeTheConstructor)
{
    // color/packfromvector4_quarters and color/packfromvector4_out_of_range.
    Color quarters(0, 0, 0, 0);
    quarters.PackFromVector4(Vector4(0.25f, 0.5f, 0.75f, 1.0f));
    EXPECT_EQ(quarters.getRProperty(), 64);
    EXPECT_EQ(quarters.getGProperty(), 128);
    EXPECT_EQ(quarters.getBProperty(), 191);
    EXPECT_EQ(quarters.getAProperty(), 255);

    Color outOfRange(0, 0, 0, 0);
    outOfRange.PackFromVector4(Vector4(2.0f, -1.0f, 0.5f, 1.0f));
    EXPECT_EQ(outOfRange.getRProperty(), 255);
    EXPECT_EQ(outOfRange.getGProperty(), 0);
    EXPECT_EQ(outOfRange.getBProperty(), 128);
    EXPECT_EQ(outOfRange.getAProperty(), 255);
}

TEST(ColorTest, PackFromVector4NaNIsDefinedNotUndefinedBehavior)
{
    Color c(128, 128, 128, 128);
    c.PackFromVector4(Vector4(std::numeric_limits<float>::quiet_NaN(), 0.5f, 0.5f, 0.5f));
    EXPECT_EQ(c.getRProperty(), 0);
    EXPECT_EQ(c.getGProperty(), 128);
}

TEST(ColorTest, PackFromVector4InfinitiesSaturate)
{
    Color c(0, 0, 0, 0);
    c.PackFromVector4(Vector4(std::numeric_limits<float>::infinity(),
                              -std::numeric_limits<float>::infinity(), 0.5f, 1.0f));
    EXPECT_EQ(c.getRProperty(), 255);
    EXPECT_EQ(c.getGProperty(), 0);
    EXPECT_EQ(c.getBProperty(), 128);
    EXPECT_EQ(c.getAProperty(), 255);
}

TEST(ColorTest, PackFromVector4ExtremeFiniteValueSaturates)
{
    Color c(0, 0, 0, 0);
    c.PackFromVector4(Vector4(1e30f, -1e30f, 0.0f, 0.0f));
    EXPECT_EQ(c.getRProperty(), 255);
    EXPECT_EQ(c.getGProperty(), 0);
}

TEST(ColorTest, PackFromVector4IsDeterministicAcrossRepeatedCalls)
{
    Color a(0, 0, 0, 0);
    Color b(0, 0, 0, 0);
    const Vector4 v(3.7f, -2.2f, 1e20f, std::numeric_limits<float>::quiet_NaN());
    a.PackFromVector4(v);
    b.PackFromVector4(v);
    EXPECT_EQ(a.getPackedValueProperty(), b.getPackedValueProperty());
}

// --- operator* commutative (CNAEXT) ---

TEST(ColorTest, MultiplyOperatorCommutativeMatchesNormal)
{
    Color a = Color::White * 0.5f;
    Color b = 0.5f * Color::White;
    EXPECT_EQ(a.getRProperty(), b.getRProperty());
    EXPECT_EQ(a.getAProperty(), b.getAProperty());
}

// --- Memory layout invariants ---

TEST(ColorTest, SizeIsLargerThanFourBytesVtablePresent)
{
    // Color inherits IPackedVectorT<UInt32> (virtual), so a vtable pointer sits
    // before packedValue. Casting Color* to uint8_t* for GL pixel I/O writes into
    // the vtable, not the pixel data. GetBackBufferData must use a plain uint8_t[]
    // buffer and construct Color(r, g, b, a) per pixel — never cast Color* directly.
    EXPECT_GT(sizeof(Color), sizeof(uint32_t));
}

TEST(ColorTest, ConstructedFromRawRgbaBytesYieldsCorrectComponents)
{
    // Regression guard for the GetBackBufferData vtable mis-cast bug (fixed in a63475e).
    // Simulates the correct unpack path: plain buffer → Color(r, g, b, a) per pixel.
    const uint8_t buf[4] = {255, 128, 64, 32};
    Color c(buf[0], buf[1], buf[2], buf[3]);
    EXPECT_EQ(c.getRProperty(), 255);
    EXPECT_EQ(c.getGProperty(), 128);
    EXPECT_EQ(c.getBProperty(),  64);
    EXPECT_EQ(c.getAProperty(),  32);
}
