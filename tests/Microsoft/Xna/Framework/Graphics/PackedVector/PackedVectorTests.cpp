// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include <cmath>
#include <limits>
#include "Microsoft/Xna/Framework/Vector4.hpp"
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

using Microsoft::Xna::Framework::Vector4;
using namespace Microsoft::Xna::Framework::Graphics::PackedVector;

// =============================================================================
// Alpha8
// =============================================================================

TEST(Alpha8Test, DefaultPackedZero)
{
    Alpha8 a;
    EXPECT_EQ(a.getPackedValueProperty(), 0u);
}

TEST(Alpha8Test, CtorFullAlphaPacks255)
{
    Alpha8 a(1.0f);
    EXPECT_EQ(a.getPackedValueProperty(), 255u);
}

TEST(Alpha8Test, ToVector4FullAlpha)
{
    Alpha8 a(1.0f);
    Vector4 v = a.ToVector4();
    EXPECT_FLOAT_EQ(v.X, 0.0f);
    EXPECT_FLOAT_EQ(v.Y, 0.0f);
    EXPECT_FLOAT_EQ(v.Z, 0.0f);
    EXPECT_NEAR(v.W, 1.0f, 0.01f);
}

TEST(Alpha8Test, PackFromVector4)
{
    Alpha8 a;
    a.PackFromVector4(Vector4(0.0f, 0.0f, 0.0f, 0.5f));
    EXPECT_GT(a.getPackedValueProperty(), 0u);
    EXPECT_LT(a.getPackedValueProperty(), 255u);
}

TEST(Alpha8Test, EqualityTrue)
{
    Alpha8 a(0.5f), b(0.5f);
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
}

TEST(Alpha8Test, EqualityFalse)
{
    Alpha8 a(0.0f), b(1.0f);
    EXPECT_FALSE(a == b);
    EXPECT_TRUE(a != b);
}

TEST(Alpha8Test, SetGetPackedValue)
{
    Alpha8 a;
    a.setPackedValueProperty(128u);
    EXPECT_EQ(a.getPackedValueProperty(), 128u);
}

TEST(Alpha8Test, GoldenHalfAlpha)
{
    Alpha8 a(0.5f);
    EXPECT_EQ(a.getPackedValueProperty(), 0x80u);
    EXPECT_NEAR(a.ToVector4().W, 0.50196078f, 1e-6f);
}

TEST(Alpha8Test, GoldenQuarterAlpha)
{
    Alpha8 a(0.25f);
    EXPECT_EQ(a.getPackedValueProperty(), 0x40u);
    EXPECT_NEAR(a.ToVector4().W, 0.25098039f, 1e-6f);
}

// =============================================================================
// Bgr565
// =============================================================================

TEST(Bgr565Test, DefaultPackedZero)
{
    Bgr565 v;
    EXPECT_EQ(v.getPackedValueProperty(), 0u);
}

TEST(Bgr565Test, CtorWhitePacksMaximum)
{
    Bgr565 v(1.0f, 1.0f, 1.0f);
    EXPECT_EQ(v.getPackedValueProperty(), 0xFFFFu);
}

TEST(Bgr565Test, PackFromVector4Black)
{
    Bgr565 v;
    v.PackFromVector4(Vector4(0.0f, 0.0f, 0.0f, 1.0f));
    EXPECT_EQ(v.getPackedValueProperty(), 0u);
}

TEST(Bgr565Test, EqualityTrue)
{
    Bgr565 a(1.0f, 0.0f, 0.0f), b(1.0f, 0.0f, 0.0f);
    EXPECT_TRUE(a == b);
}

TEST(Bgr565Test, EqualityFalse)
{
    Bgr565 a(1.0f, 0.0f, 0.0f), b(0.0f, 1.0f, 0.0f);
    EXPECT_TRUE(a != b);
}

TEST(Bgr565Test, GoldenRed)
{
    Bgr565 v(1.0f, 0.0f, 0.0f);
    EXPECT_EQ(v.getPackedValueProperty(), 0xf800u);
    EXPECT_NEAR(v.ToVector4().X, 1.0f, 1e-6f);
}

TEST(Bgr565Test, GoldenHalf)
{
    Bgr565 v(0.5f, 0.5f, 0.5f);
    EXPECT_EQ(v.getPackedValueProperty(), 0x8410u);
    EXPECT_NEAR(v.ToVector4().X, 0.51612903f, 1e-6f);
    EXPECT_NEAR(v.ToVector4().Y, 0.50793651f, 1e-6f);
    EXPECT_NEAR(v.ToVector4().Z, 0.51612903f, 1e-6f);
}

TEST(Bgr565Test, GoldenGreen)
{
    Bgr565 v(0.0f, 1.0f, 0.0f);
    EXPECT_EQ(v.getPackedValueProperty(), 0x07e0u);
    EXPECT_NEAR(v.ToVector4().Y, 1.0f, 1e-6f);
}

TEST(Bgr565Test, GoldenBlue)
{
    Bgr565 v(0.0f, 0.0f, 1.0f);
    EXPECT_EQ(v.getPackedValueProperty(), 0x001fu);
    EXPECT_NEAR(v.ToVector4().Z, 1.0f, 1e-6f);
}

// =============================================================================
// Bgra4444
// =============================================================================

TEST(Bgra4444Test, DefaultPackedZero)
{
    Bgra4444 v;
    EXPECT_EQ(v.getPackedValueProperty(), 0u);
}

TEST(Bgra4444Test, CtorWhitePacksMaximum)
{
    Bgra4444 v(1.0f, 1.0f, 1.0f, 1.0f);
    EXPECT_EQ(v.getPackedValueProperty(), 0xFFFFu);
}

TEST(Bgra4444Test, PackFromVector4Zero)
{
    Bgra4444 v;
    v.PackFromVector4(Vector4(0.0f, 0.0f, 0.0f, 0.0f));
    EXPECT_EQ(v.getPackedValueProperty(), 0u);
}

TEST(Bgra4444Test, EqualityTrue)
{
    Bgra4444 a(0.5f, 0.5f, 0.5f, 1.0f), b(0.5f, 0.5f, 0.5f, 1.0f);
    EXPECT_TRUE(a == b);
}

TEST(Bgra4444Test, EqualityFalse)
{
    Bgra4444 a(1.0f, 0.0f, 0.0f, 1.0f), b(0.0f, 1.0f, 0.0f, 1.0f);
    EXPECT_TRUE(a != b);
}

TEST(Bgra4444Test, GoldenRedOpaque)
{
    Bgra4444 v(1.0f, 0.0f, 0.0f, 1.0f);
    EXPECT_EQ(v.getPackedValueProperty(), 0xff00u);
    Vector4 vec = v.ToVector4();
    EXPECT_NEAR(vec.X, 1.0f, 1e-6f);
    EXPECT_NEAR(vec.Y, 0.0f, 1e-6f);
    EXPECT_NEAR(vec.Z, 0.0f, 1e-6f);
    EXPECT_NEAR(vec.W, 1.0f, 1e-6f);
}

TEST(Bgra4444Test, GoldenGreenHalfAlpha)
{
    Bgra4444 v(0.0f, 1.0f, 0.0f, 0.5f);
    EXPECT_EQ(v.getPackedValueProperty(), 0x80f0u);
    Vector4 vec = v.ToVector4();
    EXPECT_NEAR(vec.X, 0.0f, 1e-6f);
    EXPECT_NEAR(vec.Y, 1.0f, 1e-6f);
    EXPECT_NEAR(vec.Z, 0.0f, 1e-6f);
    EXPECT_NEAR(vec.W, 0.533333f, 1e-5f);
}

TEST(Bgra4444Test, GoldenHalf)
{
    Bgra4444 v(0.5f, 0.5f, 0.5f, 0.5f);
    EXPECT_EQ(v.getPackedValueProperty(), 0x8888u);
    Vector4 vec = v.ToVector4();
    EXPECT_NEAR(vec.X, 0.533333f, 1e-5f);
    EXPECT_NEAR(vec.Y, 0.533333f, 1e-5f);
    EXPECT_NEAR(vec.Z, 0.533333f, 1e-5f);
    EXPECT_NEAR(vec.W, 0.533333f, 1e-5f);
}

// =============================================================================
// Bgra5551
// =============================================================================

TEST(Bgra5551Test, DefaultPackedZero)
{
    Bgra5551 v;
    EXPECT_EQ(v.getPackedValueProperty(), 0u);
}

TEST(Bgra5551Test, CtorWhitePacksMaximum)
{
    Bgra5551 v(1.0f, 1.0f, 1.0f, 1.0f);
    EXPECT_EQ(v.getPackedValueProperty(), 0xFFFFu);
}

TEST(Bgra5551Test, PackFromVector4Zero)
{
    Bgra5551 v;
    v.PackFromVector4(Vector4(0.0f, 0.0f, 0.0f, 0.0f));
    EXPECT_EQ(v.getPackedValueProperty(), 0u);
}

TEST(Bgra5551Test, EqualityTrue)
{
    Bgra5551 a(1.0f, 1.0f, 1.0f, 1.0f), b(1.0f, 1.0f, 1.0f, 1.0f);
    EXPECT_TRUE(a == b);
}

TEST(Bgra5551Test, EqualityFalse)
{
    Bgra5551 a(1.0f, 0.0f, 0.0f, 1.0f), b(0.0f, 1.0f, 0.0f, 1.0f);
    EXPECT_TRUE(a != b);
}

TEST(Bgra5551Test, GoldenRedOpaque)
{
    Bgra5551 v(1.0f, 0.0f, 0.0f, 1.0f);
    EXPECT_EQ(v.getPackedValueProperty(), 0xfc00u);
    Vector4 vec = v.ToVector4();
    EXPECT_NEAR(vec.X, 1.0f, 1e-6f);
    EXPECT_NEAR(vec.Y, 0.0f, 1e-6f);
    EXPECT_NEAR(vec.Z, 0.0f, 1e-6f);
    EXPECT_NEAR(vec.W, 1.0f, 1e-6f);
}

TEST(Bgra5551Test, GoldenBlueTransparent)
{
    Bgra5551 v(0.0f, 0.0f, 1.0f, 0.0f);
    EXPECT_EQ(v.getPackedValueProperty(), 0x001fu);
    Vector4 vec = v.ToVector4();
    EXPECT_NEAR(vec.X, 0.0f, 1e-6f);
    EXPECT_NEAR(vec.Y, 0.0f, 1e-6f);
    EXPECT_NEAR(vec.Z, 1.0f, 1e-6f);
    EXPECT_NEAR(vec.W, 0.0f, 1e-6f);
}

TEST(Bgra5551Test, GoldenHalfOpaque)
{
    Bgra5551 v(0.5f, 0.5f, 0.5f, 1.0f);
    EXPECT_EQ(v.getPackedValueProperty(), 0xc210u);
    Vector4 vec = v.ToVector4();
    EXPECT_NEAR(vec.X, 0.516129f, 1e-5f);
    EXPECT_NEAR(vec.Y, 0.516129f, 1e-5f);
    EXPECT_NEAR(vec.Z, 0.516129f, 1e-5f);
    EXPECT_NEAR(vec.W, 1.0f, 1e-6f);
}

// =============================================================================
// Byte4
// =============================================================================

TEST(Byte4Test, DefaultPackedZero)
{
    Byte4 v;
    EXPECT_EQ(v.getPackedValueProperty(), 0u);
}

TEST(Byte4Test, CtorFromRawPacked)
{
    Byte4 v(0xAABBCCDDu);
    EXPECT_EQ(v.getPackedValueProperty(), 0xAABBCCDDu);
}

TEST(Byte4Test, PackFromVector4Zero)
{
    Byte4 v;
    v.PackFromVector4(Vector4(0.0f, 0.0f, 0.0f, 0.0f));
    EXPECT_EQ(v.getPackedValueProperty(), 0u);
}

TEST(Byte4Test, EqualityTrue)
{
    Byte4 a(0xAAu), b(0xAAu);
    EXPECT_TRUE(a == b);
}

TEST(Byte4Test, EqualityFalse)
{
    Byte4 a(0u), b(0xAAu);
    EXPECT_TRUE(a != b);
}

// =============================================================================
// HalfSingle
// =============================================================================

TEST(HalfSingleTest, DefaultPackedZero)
{
    HalfSingle v;
    EXPECT_EQ(v.getPackedValueProperty(), 0u);
}

TEST(HalfSingleTest, CtorZeroPacksZero)
{
    HalfSingle v(0.0f);
    EXPECT_EQ(v.getPackedValueProperty(), 0x0000u);
}

TEST(HalfSingleTest, GoldenPackedValues)
{
    EXPECT_EQ(HalfSingle(1.0f).getPackedValueProperty(),  0x3c00u);
    EXPECT_EQ(HalfSingle(-1.0f).getPackedValueProperty(), 0xbc00u);
    EXPECT_EQ(HalfSingle(0.5f).getPackedValueProperty(),  0x3800u);
    EXPECT_EQ(HalfSingle(0.25f).getPackedValueProperty(), 0x3400u);
}

TEST(HalfSingleTest, ToSingleRoundTrip)
{
    HalfSingle v(1.0f);
    EXPECT_NEAR(v.ToSingle(), 1.0f, 0.001f);
}

TEST(HalfSingleTest, ToVector4)
{
    HalfSingle v(1.0f);
    Vector4 vec = v.ToVector4();
    EXPECT_NEAR(vec.X, 1.0f, 0.001f);
    EXPECT_FLOAT_EQ(vec.W, 1.0f);
}

TEST(HalfSingleTest, EqualityTrue)
{
    HalfSingle a(0.5f), b(0.5f);
    EXPECT_TRUE(a == b);
}

TEST(HalfSingleTest, EqualityFalse)
{
    HalfSingle a(0.0f), b(1.0f);
    EXPECT_TRUE(a != b);
}

// =============================================================================
// HalfVector2
// =============================================================================

TEST(HalfVector2Test, DefaultPackedZero)
{
    HalfVector2 v;
    EXPECT_EQ(v.getPackedValueProperty(), 0u);
}

TEST(HalfVector2Test, ToVector4ZComponent)
{
    HalfVector2 v(1.0f, 0.0f);
    Vector4 vec = v.ToVector4();
    EXPECT_FLOAT_EQ(vec.Z, 0.0f);
    EXPECT_FLOAT_EQ(vec.W, 1.0f);
}

TEST(HalfVector2Test, EqualityTrue)
{
    HalfVector2 a(1.0f, 0.5f), b(1.0f, 0.5f);
    EXPECT_TRUE(a == b);
}

TEST(HalfVector2Test, EqualityFalse)
{
    HalfVector2 a(1.0f, 0.0f), b(0.0f, 1.0f);
    EXPECT_TRUE(a != b);
}

TEST(HalfVector2Test, GoldenPackedValues)
{
    EXPECT_EQ(HalfVector2(1.0f, 0.0f).getPackedValueProperty(), 0x00003c00u);
    EXPECT_EQ(HalfVector2(0.0f, 1.0f).getPackedValueProperty(), 0x3c000000u);
    EXPECT_EQ(HalfVector2(0.5f, 0.5f).getPackedValueProperty(), 0x38003800u);
}

TEST(HalfVector2Test, GoldenToVector4)
{
    HalfVector2 v(1.0f, 0.5f);
    Vector4 tv = v.ToVector4();
    EXPECT_NEAR(tv.X, 1.0f, 1e-4f);
    EXPECT_NEAR(tv.Y, 0.5f, 1e-4f);
    EXPECT_FLOAT_EQ(tv.Z, 0.0f);
    EXPECT_FLOAT_EQ(tv.W, 1.0f);
}

// =============================================================================
// HalfVector4
// =============================================================================

TEST(HalfVector4Test, DefaultPackedZero)
{
    HalfVector4 v;
    EXPECT_EQ(v.getPackedValueProperty(), 0ull);
}

TEST(HalfVector4Test, ToVector4RoundTrip)
{
    HalfVector4 v(1.0f, 0.5f, 0.25f, 0.0f);
    Vector4 vec = v.ToVector4();
    EXPECT_NEAR(vec.X, 1.0f, 0.01f);
    EXPECT_NEAR(vec.Y, 0.5f, 0.01f);
}

TEST(HalfVector4Test, EqualityTrue)
{
    HalfVector4 a(1.0f, 0.0f, 0.0f, 1.0f), b(1.0f, 0.0f, 0.0f, 1.0f);
    EXPECT_TRUE(a == b);
}

TEST(HalfVector4Test, EqualityFalse)
{
    HalfVector4 a(1.0f, 0.0f, 0.0f, 1.0f), b(0.0f, 1.0f, 0.0f, 1.0f);
    EXPECT_TRUE(a != b);
}

// =============================================================================
// NormalizedByte2
// =============================================================================

TEST(NormalizedByte2Test, DefaultPackedZero)
{
    NormalizedByte2 v;
    EXPECT_EQ(v.getPackedValueProperty(), 0u);
}

TEST(NormalizedByte2Test, CtorZero)
{
    NormalizedByte2 v(0.0f, 0.0f);
    EXPECT_EQ(v.getPackedValueProperty(), 0u);
}

TEST(NormalizedByte2Test, ToVector4ZW)
{
    NormalizedByte2 v(0.0f, 0.0f);
    Vector4 vec = v.ToVector4();
    EXPECT_FLOAT_EQ(vec.Z, 0.0f);
    EXPECT_FLOAT_EQ(vec.W, 1.0f);
}

TEST(NormalizedByte2Test, EqualityTrue)
{
    NormalizedByte2 a(0.5f, -0.5f), b(0.5f, -0.5f);
    EXPECT_TRUE(a == b);
}

TEST(NormalizedByte2Test, EqualityFalse)
{
    NormalizedByte2 a(1.0f, 0.0f), b(0.0f, 1.0f);
    EXPECT_TRUE(a != b);
}

TEST(NormalizedByte2Test, GoldenPackedValues)
{
    EXPECT_EQ(NormalizedByte2( 1.0f,  0.0f).getPackedValueProperty(), 0x007fu);
    EXPECT_EQ(NormalizedByte2( 0.0f,  1.0f).getPackedValueProperty(), 0x7f00u);
    EXPECT_EQ(NormalizedByte2(-1.0f, -1.0f).getPackedValueProperty(), 0x8181u);
    EXPECT_EQ(NormalizedByte2( 0.5f,  0.5f).getPackedValueProperty(), 0x4040u);
}

TEST(NormalizedByte2Test, GoldenNegativeOneToVector2)
{
    NormalizedByte2 v(-1.0f, -1.0f);
    auto tv = v.ToVector4();
    EXPECT_FLOAT_EQ(tv.X, -1.0f);
    EXPECT_FLOAT_EQ(tv.Y, -1.0f);
}

// =============================================================================
// NormalizedByte4
// =============================================================================

TEST(NormalizedByte4Test, DefaultPackedZero)
{
    NormalizedByte4 v;
    EXPECT_EQ(v.getPackedValueProperty(), 0u);
}

TEST(NormalizedByte4Test, CtorZero)
{
    NormalizedByte4 v(0.0f, 0.0f, 0.0f, 0.0f);
    EXPECT_EQ(v.getPackedValueProperty(), 0u);
}

TEST(NormalizedByte4Test, EqualityTrue)
{
    NormalizedByte4 a(0.5f, 0.5f, 0.5f, 0.5f), b(0.5f, 0.5f, 0.5f, 0.5f);
    EXPECT_TRUE(a == b);
}

TEST(NormalizedByte4Test, EqualityFalse)
{
    NormalizedByte4 a(1.0f, 0.0f, 0.0f, 1.0f), b(0.0f, 1.0f, 0.0f, 1.0f);
    EXPECT_TRUE(a != b);
}

TEST(NormalizedByte4Test, GoldenPackedValues)
{
    EXPECT_EQ(NormalizedByte4( 1.0f,  0.0f,  0.0f,  1.0f).getPackedValueProperty(), 0x7f00007fu);
    EXPECT_EQ(NormalizedByte4( 0.5f,  0.5f,  0.5f,  0.5f).getPackedValueProperty(), 0x40404040u);
    EXPECT_EQ(NormalizedByte4(-1.0f, -1.0f, -1.0f, -1.0f).getPackedValueProperty(), 0x81818181u);
}

TEST(NormalizedByte4Test, GoldenNegativeOneToVector4)
{
    NormalizedByte4 v(-1.0f, -1.0f, -1.0f, -1.0f);
    auto tv = v.ToVector4();
    EXPECT_FLOAT_EQ(tv.X, -1.0f);
    EXPECT_FLOAT_EQ(tv.Y, -1.0f);
    EXPECT_FLOAT_EQ(tv.Z, -1.0f);
    EXPECT_FLOAT_EQ(tv.W, -1.0f);
}

// =============================================================================
// NormalizedShort2
// =============================================================================

TEST(NormalizedShort2Test, DefaultPackedZero)
{
    NormalizedShort2 v;
    EXPECT_EQ(v.getPackedValueProperty(), 0u);
}

TEST(NormalizedShort2Test, CtorZero)
{
    NormalizedShort2 v(0.0f, 0.0f);
    EXPECT_EQ(v.getPackedValueProperty(), 0u);
}

TEST(NormalizedShort2Test, ToVector4ZW)
{
    NormalizedShort2 v(0.0f, 0.0f);
    Vector4 vec = v.ToVector4();
    EXPECT_FLOAT_EQ(vec.Z, 0.0f);
    EXPECT_FLOAT_EQ(vec.W, 1.0f);
}

TEST(NormalizedShort2Test, EqualityTrue)
{
    NormalizedShort2 a(1.0f, -1.0f), b(1.0f, -1.0f);
    EXPECT_TRUE(a == b);
}

TEST(NormalizedShort2Test, EqualityFalse)
{
    NormalizedShort2 a(1.0f, 0.0f), b(0.0f, 1.0f);
    EXPECT_TRUE(a != b);
}

TEST(NormalizedShort2Test, GoldenPackedValues)
{
    EXPECT_EQ(NormalizedShort2( 1.0f,  0.0f).getPackedValueProperty(), 0x00007fffu);
    EXPECT_EQ(NormalizedShort2( 0.0f,  1.0f).getPackedValueProperty(), 0x7fff0000u);
    EXPECT_EQ(NormalizedShort2(-1.0f, -1.0f).getPackedValueProperty(), 0x80018001u);
    EXPECT_EQ(NormalizedShort2( 0.5f,  0.5f).getPackedValueProperty(), 0x40004000u);
}

TEST(NormalizedShort2Test, GoldenNegativeOneToVector2)
{
    NormalizedShort2 v(-1.0f, -1.0f);
    auto tv = v.ToVector4();
    EXPECT_FLOAT_EQ(tv.X, -1.0f);
    EXPECT_FLOAT_EQ(tv.Y, -1.0f);
}

// =============================================================================
// NormalizedShort4
// =============================================================================

TEST(NormalizedShort4Test, DefaultPackedZero)
{
    NormalizedShort4 v;
    EXPECT_EQ(v.getPackedValueProperty(), 0ull);
}

TEST(NormalizedShort4Test, CtorZero)
{
    NormalizedShort4 v(0.0f, 0.0f, 0.0f, 0.0f);
    EXPECT_EQ(v.getPackedValueProperty(), 0ull);
}

TEST(NormalizedShort4Test, EqualityTrue)
{
    NormalizedShort4 a(1.0f, -1.0f, 0.5f, -0.5f), b(1.0f, -1.0f, 0.5f, -0.5f);
    EXPECT_TRUE(a == b);
}

TEST(NormalizedShort4Test, EqualityFalse)
{
    NormalizedShort4 a(1.0f, 0.0f, 0.0f, 1.0f), b(0.0f, 1.0f, 0.0f, 1.0f);
    EXPECT_TRUE(a != b);
}

TEST(NormalizedShort4Test, GoldenPackedValues)
{
    EXPECT_EQ(NormalizedShort4( 1.0f,  0.0f,  0.0f,  1.0f).getPackedValueProperty(), 0x7fff000000007fffull);
    EXPECT_EQ(NormalizedShort4( 0.5f,  0.5f,  0.5f,  0.5f).getPackedValueProperty(), 0x4000400040004000ull);
    EXPECT_EQ(NormalizedShort4(-1.0f, -1.0f, -1.0f, -1.0f).getPackedValueProperty(), 0x8001800180018001ull);
}

TEST(NormalizedShort4Test, GoldenNegativeOneToVector4)
{
    NormalizedShort4 v(-1.0f, -1.0f, -1.0f, -1.0f);
    auto tv = v.ToVector4();
    EXPECT_FLOAT_EQ(tv.X, -1.0f);
    EXPECT_FLOAT_EQ(tv.Y, -1.0f);
    EXPECT_FLOAT_EQ(tv.Z, -1.0f);
    EXPECT_FLOAT_EQ(tv.W, -1.0f);
}

// =============================================================================
// Rg32
// =============================================================================

TEST(Rg32Test, DefaultPackedZero)
{
    Rg32 v;
    EXPECT_EQ(v.getPackedValueProperty(), 0u);
}

TEST(Rg32Test, CtorZero)
{
    Rg32 v(0.0f, 0.0f);
    EXPECT_EQ(v.getPackedValueProperty(), 0u);
}

TEST(Rg32Test, ToVector4ZW)
{
    Rg32 v(0.0f, 0.0f);
    Vector4 vec = v.ToVector4();
    EXPECT_FLOAT_EQ(vec.Z, 0.0f);
    EXPECT_FLOAT_EQ(vec.W, 1.0f);
}

TEST(Rg32Test, EqualityTrue)
{
    Rg32 a(1.0f, 0.5f), b(1.0f, 0.5f);
    EXPECT_TRUE(a == b);
}

TEST(Rg32Test, EqualityFalse)
{
    Rg32 a(1.0f, 0.0f), b(0.0f, 1.0f);
    EXPECT_TRUE(a != b);
}

// =============================================================================
// Rgba1010102
// =============================================================================

TEST(Rgba1010102Test, DefaultPackedZero)
{
    Rgba1010102 v;
    EXPECT_EQ(v.getPackedValueProperty(), 0u);
}

TEST(Rgba1010102Test, CtorZero)
{
    Rgba1010102 v(0.0f, 0.0f, 0.0f, 0.0f);
    EXPECT_EQ(v.getPackedValueProperty(), 0u);
}

TEST(Rgba1010102Test, EqualityTrue)
{
    Rgba1010102 a(1.0f, 0.0f, 0.0f, 1.0f), b(1.0f, 0.0f, 0.0f, 1.0f);
    EXPECT_TRUE(a == b);
}

TEST(Rgba1010102Test, EqualityFalse)
{
    Rgba1010102 a(1.0f, 0.0f, 0.0f, 1.0f), b(0.0f, 1.0f, 0.0f, 1.0f);
    EXPECT_TRUE(a != b);
}

// =============================================================================
// Rgba64
// =============================================================================

TEST(Rgba64Test, DefaultPackedZero)
{
    Rgba64 v;
    EXPECT_EQ(v.getPackedValueProperty(), 0ull);
}

TEST(Rgba64Test, CtorZero)
{
    Rgba64 v(0.0f, 0.0f, 0.0f, 0.0f);
    EXPECT_EQ(v.getPackedValueProperty(), 0ull);
}

TEST(Rgba64Test, EqualityTrue)
{
    Rgba64 a(1.0f, 0.5f, 0.25f, 1.0f), b(1.0f, 0.5f, 0.25f, 1.0f);
    EXPECT_TRUE(a == b);
}

TEST(Rgba64Test, EqualityFalse)
{
    Rgba64 a(1.0f, 0.0f, 0.0f, 1.0f), b(0.0f, 1.0f, 0.0f, 1.0f);
    EXPECT_TRUE(a != b);
}

// =============================================================================
// Short2
// =============================================================================

TEST(Short2Test, DefaultPackedZero)
{
    Short2 v;
    EXPECT_EQ(v.getPackedValueProperty(), 0u);
}

TEST(Short2Test, CtorZero)
{
    Short2 v(0.0f, 0.0f);
    EXPECT_EQ(v.getPackedValueProperty(), 0u);
}

TEST(Short2Test, ToVector4ZW)
{
    Short2 v(0.0f, 0.0f);
    Vector4 vec = v.ToVector4();
    EXPECT_FLOAT_EQ(vec.Z, 0.0f);
    EXPECT_FLOAT_EQ(vec.W, 1.0f);
}

TEST(Short2Test, ToVector4XY)
{
    Short2 v(100.0f, -200.0f);
    Vector4 vec = v.ToVector4();
    EXPECT_FLOAT_EQ(vec.X, 100.0f);
    EXPECT_FLOAT_EQ(vec.Y, -200.0f);
}

TEST(Short2Test, EqualityTrue)
{
    Short2 a(100.0f, -200.0f), b(100.0f, -200.0f);
    EXPECT_TRUE(a == b);
}

TEST(Short2Test, EqualityFalse)
{
    Short2 a(100.0f, 0.0f), b(0.0f, 100.0f);
    EXPECT_TRUE(a != b);
}

// =============================================================================
// Short4
// =============================================================================

TEST(Short4Test, DefaultPackedZero)
{
    Short4 v;
    EXPECT_EQ(v.getPackedValueProperty(), 0ull);
}

TEST(Short4Test, CtorZero)
{
    Short4 v(0.0f, 0.0f, 0.0f, 0.0f);
    EXPECT_EQ(v.getPackedValueProperty(), 0ull);
}

TEST(Short4Test, ToVector4XYZW)
{
    Short4 v(10.0f, 20.0f, 30.0f, 40.0f);
    Vector4 vec = v.ToVector4();
    EXPECT_FLOAT_EQ(vec.X, 10.0f);
    EXPECT_FLOAT_EQ(vec.Y, 20.0f);
    EXPECT_FLOAT_EQ(vec.Z, 30.0f);
    EXPECT_FLOAT_EQ(vec.W, 40.0f);
}

TEST(Short4Test, EqualityTrue)
{
    Short4 a(1.0f, 2.0f, 3.0f, 4.0f), b(1.0f, 2.0f, 3.0f, 4.0f);
    EXPECT_TRUE(a == b);
}

TEST(Short4Test, EqualityFalse)
{
    Short4 a(1.0f, 0.0f, 0.0f, 0.0f), b(0.0f, 1.0f, 0.0f, 0.0f);
    EXPECT_TRUE(a != b);
}

// =============================================================================
// Edge-case tests — Task 199
// =============================================================================

// ── Clamping: inputs outside valid range are clamped to boundary ──────────────

TEST(Alpha8EdgeTest, ClampAboveOne)
{
    Alpha8 a(1.5f);
    EXPECT_EQ(a.getPackedValueProperty(), 0xFFu);
}

TEST(Alpha8EdgeTest, ClampBelowZero)
{
    Alpha8 a(-0.5f);
    EXPECT_EQ(a.getPackedValueProperty(), 0x00u);
}

TEST(Bgr565EdgeTest, ClampAboveOne)
{
    Bgr565 v(2.0f, 0.0f, 0.0f);
    EXPECT_EQ(v.getPackedValueProperty(), Bgr565(1.0f, 0.0f, 0.0f).getPackedValueProperty());
}

TEST(Bgr565EdgeTest, ClampBelowZero)
{
    Bgr565 v(-1.0f, 0.0f, 0.0f);
    EXPECT_EQ(v.getPackedValueProperty(), Bgr565(0.0f, 0.0f, 0.0f).getPackedValueProperty());
}

TEST(NormalizedByte2EdgeTest, ClampAboveAndBelow)
{
    // x=1.5 clamped to 1.0 → 0x7F; y=-1.5 clamped to -1.0 → 0x81
    NormalizedByte2 v(1.5f, -1.5f);
    EXPECT_EQ(v.getPackedValueProperty(), 0x817Fu);
}

TEST(NormalizedByte4EdgeTest, ClampAllChannels)
{
    NormalizedByte4 v(2.0f, -2.0f, 1.5f, -1.5f);
    EXPECT_EQ(v.getPackedValueProperty(), NormalizedByte4(1.0f, -1.0f, 1.0f, -1.0f).getPackedValueProperty());
}

TEST(NormalizedShort2EdgeTest, ClampAboveAndBelow)
{
    // x=2.0 → 0x7FFF; y=-2.0 → 0x8001
    NormalizedShort2 v(2.0f, -2.0f);
    EXPECT_EQ(v.getPackedValueProperty(), 0x80017FFFu);
}

TEST(NormalizedShort4EdgeTest, ClampAllChannels)
{
    NormalizedShort4 v(2.0f, -2.0f, 1.5f, -1.5f);
    EXPECT_EQ(v.getPackedValueProperty(), NormalizedShort4(1.0f, -1.0f, 1.0f, -1.0f).getPackedValueProperty());
}

TEST(Rg32EdgeTest, ClampAboveAndBelow)
{
    // r=1.5 → 0xFFFF; g=-0.5 → 0x0000
    Rg32 v(1.5f, -0.5f);
    EXPECT_EQ(v.getPackedValueProperty(), 0x0000FFFFu);
}

TEST(Rgba1010102EdgeTest, ClampAboveOne)
{
    Rgba1010102 v(2.0f, 0.0f, 0.0f, 1.0f);
    EXPECT_EQ(v.getPackedValueProperty(), Rgba1010102(1.0f, 0.0f, 0.0f, 1.0f).getPackedValueProperty());
}

TEST(Rgba64EdgeTest, ClampAboveAndBelow)
{
    Rgba64 v(1.5f, -0.5f, 0.0f, 1.0f);
    EXPECT_EQ(v.getPackedValueProperty(), Rgba64(1.0f, 0.0f, 0.0f, 1.0f).getPackedValueProperty());
}

TEST(Short2EdgeTest, ClampLargePositive)
{
    // 40000 clamped to 32767 = 0x7FFF
    Short2 v(40000.0f, 0.0f);
    EXPECT_EQ(v.getPackedValueProperty(), 0x00007FFFu);
}

TEST(Short2EdgeTest, ClampLargeNegative)
{
    // -40000 clamped to -32768 = 0x8000
    Short2 v(0.0f, -40000.0f);
    EXPECT_EQ(v.getPackedValueProperty(), 0x80000000u);
}

TEST(Short4EdgeTest, ClampAllChannels)
{
    Short4 v(40000.0f, -40000.0f, 32767.0f, -32768.0f);
    EXPECT_EQ(v.getPackedValueProperty(), Short4(32767.0f, -32768.0f, 32767.0f, -32768.0f).getPackedValueProperty());
}

// ── HalfTypeHelper special float values ──────────────────────────────────────

TEST(HalfTypeHelperTest, NegativeZero)
{
    const uint16_t h = HalfTypeHelper::Convert(-0.0f);
    EXPECT_EQ(h, 0x8000u);
    // Round-trip back to -0.0f
    const float back = HalfTypeHelper::Convert(h);
    EXPECT_EQ(std::signbit(back), true);
    EXPECT_EQ(back, 0.0f);
}

TEST(HalfTypeHelperTest, PositiveInfinity)
{
    const float inf = std::numeric_limits<float>::infinity();
    EXPECT_EQ(HalfTypeHelper::Convert(inf), 0x7C00u);
    EXPECT_TRUE(std::isinf(HalfTypeHelper::Convert(static_cast<uint16_t>(0x7C00u))));
    EXPECT_GT(HalfTypeHelper::Convert(static_cast<uint16_t>(0x7C00u)), 0.0f);
}

TEST(HalfTypeHelperTest, NegativeInfinity)
{
    const float ninf = -std::numeric_limits<float>::infinity();
    EXPECT_EQ(HalfTypeHelper::Convert(ninf), 0xFC00u);
    EXPECT_TRUE(std::isinf(HalfTypeHelper::Convert(static_cast<uint16_t>(0xFC00u))));
    EXPECT_LT(HalfTypeHelper::Convert(static_cast<uint16_t>(0xFC00u)), 0.0f);
}

TEST(HalfTypeHelperTest, NaNPreservesNaN)
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const uint16_t h = HalfTypeHelper::Convert(nan);
    // Must be a NaN half: exponent = 0x1F (all ones), mantissa != 0
    EXPECT_EQ(h & 0x7C00u, 0x7C00u);
    EXPECT_NE(h & 0x03FFu, 0u);
    // Round-trip: Convert(NaN half) must produce a NaN float
    EXPECT_TRUE(std::isnan(HalfTypeHelper::Convert(h)));
}

TEST(HalfTypeHelperTest, DenormalSmallest)
{
    // Smallest half-float denormal: 0x0001 = 2^-24 ≈ 5.96e-8
    const float expected = 5.9604644775390625e-8f;
    EXPECT_NEAR(HalfTypeHelper::Convert(static_cast<uint16_t>(0x0001u)), expected, expected * 1e-5f);
}

TEST(HalfTypeHelperTest, DenormalMid)
{
    // 0x0200 = 2^-15 ≈ 3.052e-5 (half denormal)
    const float expected = 3.0517578125e-5f;
    EXPECT_NEAR(HalfTypeHelper::Convert(static_cast<uint16_t>(0x0200u)), expected, expected * 1e-5f);
}

TEST(HalfTypeHelperTest, SmallFloatBecomesZero)
{
    // Floats smaller than 2^-25 round to ±0 in half-float
    const float tiny = 1.0e-8f;
    const uint16_t h = HalfTypeHelper::Convert(tiny);
    EXPECT_EQ(h, 0x0000u);
}

// ── HalfSingle special values ─────────────────────────────────────────────────

TEST(HalfSingleEdgeTest, PositiveInfinity)
{
    HalfSingle v(std::numeric_limits<float>::infinity());
    EXPECT_EQ(v.getPackedValueProperty(), 0x7C00u);
    EXPECT_TRUE(std::isinf(v.ToSingle()));
    EXPECT_GT(v.ToSingle(), 0.0f);
}

TEST(HalfSingleEdgeTest, NegativeInfinity)
{
    HalfSingle v(-std::numeric_limits<float>::infinity());
    EXPECT_EQ(v.getPackedValueProperty(), 0xFC00u);
    EXPECT_TRUE(std::isinf(v.ToSingle()));
    EXPECT_LT(v.ToSingle(), 0.0f);
}

TEST(HalfSingleEdgeTest, NegativeZeroRoundTrip)
{
    HalfSingle v(-0.0f);
    EXPECT_EQ(v.getPackedValueProperty(), 0x8000u);
    EXPECT_EQ(std::signbit(v.ToSingle()), true);
}

// ── Boundary values 0.0, 1.0, -1.0 round-trips for normalized types ──────────

TEST(NormalizedByte2EdgeTest, ZeroRoundTrip)
{
    NormalizedByte2 v(0.0f, 0.0f);
    auto tv = v.ToVector4();
    EXPECT_FLOAT_EQ(tv.X, 0.0f);
    EXPECT_FLOAT_EQ(tv.Y, 0.0f);
}

TEST(NormalizedByte2EdgeTest, OneRoundTrip)
{
    NormalizedByte2 v(1.0f, 1.0f);
    auto tv = v.ToVector4();
    EXPECT_FLOAT_EQ(tv.X, 1.0f);
    EXPECT_FLOAT_EQ(tv.Y, 1.0f);
}

TEST(NormalizedShort2EdgeTest, ZeroRoundTrip)
{
    NormalizedShort2 v(0.0f, 0.0f);
    auto tv = v.ToVector4();
    EXPECT_FLOAT_EQ(tv.X, 0.0f);
    EXPECT_FLOAT_EQ(tv.Y, 0.0f);
}

TEST(NormalizedShort2EdgeTest, OneRoundTrip)
{
    NormalizedShort2 v(1.0f, 1.0f);
    auto tv = v.ToVector4();
    EXPECT_FLOAT_EQ(tv.X, 1.0f);
    EXPECT_FLOAT_EQ(tv.Y, 1.0f);
}
