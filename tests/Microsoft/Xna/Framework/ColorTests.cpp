#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Color.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Vector4;

// --- Construction ---

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
