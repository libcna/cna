// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include <any>
#include <bit>
#include <cstdint>
#include <limits>
#include "Microsoft/Xna/Framework/Color.hpp"

using namespace Microsoft::Xna::Framework;

TEST(Tq051ColorTest, FloatConstructorsAndPackRoundTheSingleProductToEven)
{
    // Refresh Color.PackHelper IL_0000-003f -> PackUNorm IL_0000-0012:
    // store Single(x*255), clamp, Math.Round(double) (ties to even).
    // The neighboring inputs straddle 127.5 and 128.5; their binary32 products
    // are 127.499992/127.5/127.500015 and 128.499985/128.5/128.500015.
    struct Case { std::uint32_t bits, rgb, rgba; };
    const Case cases[] = {
        {0x3effffffu, 0xffbf407fu, 0x7fbf407fu},
        {0x3f000000u, 0xffbf4080u, 0x80bf4080u},
        {0x3f000001u, 0xffbf4080u, 0x80bf4080u},
        {0x3f010100u, 0xffbf4080u, 0x80bf4080u},
        {0x3f010101u, 0xffbf4080u, 0x80bf4080u},
        {0x3f010102u, 0xffbf4081u, 0x81bf4081u},
        {0xbf800000u, 0xffbf4000u, 0x00bf4000u}, // -1 clamps to 0
        {0x40000000u, 0xffbf40ffu, 0xffbf40ffu}, // 2 clamps to 255
        {0x7f800000u, 0xffbf40ffu, 0xffbf40ffu},
        {0xff800000u, 0xffbf4000u, 0x00bf4000u},
        {0x7fc00000u, 0xffbf4000u, 0x00bf4000u}, // NaN -> 0
        {0x80000000u, 0xffbf4000u, 0x00bf4000u},
    };
    for (const auto& c : cases)
    {
        SCOPED_TRACE(c.bits);
        const float x = std::bit_cast<float>(c.bits);
        // .25*255=63.75 -> 64; .75*255=191.25 -> 191. Opaque RGB alpha=255.
        EXPECT_EQ(Color(x, .25f, .75f).getPackedValueProperty(), c.rgb);
        EXPECT_EQ(Color(Vector3(x, .25f, .75f)).getPackedValueProperty(), c.rgb);
        EXPECT_EQ(Color(x, .25f, .75f, x).getPackedValueProperty(), c.rgba);
        EXPECT_EQ(Color(Vector4(x, .25f, .75f, x)).getPackedValueProperty(), c.rgba);
        Color packed(17, 34, 51, 68);
        packed.PackFromVector4(Vector4(x, .25f, .75f, x));
        EXPECT_EQ(packed.getPackedValueProperty(), c.rgba);
    }
}

TEST(Tq051ColorTest, IntegerConstructorsClampEveryChannelIncludingAlpha)
{
    // Refresh Color..ctor(int,int,int) IL_0000-0044 and RGBA IL_0000-0051:
    // independently saturate all inputs to [0,255], then pack AABBGGRR.
    constexpr auto lo = std::numeric_limits<SharpRuntime::intcs>::min();
    constexpr auto hi = std::numeric_limits<SharpRuntime::intcs>::max();
    EXPECT_EQ(Color(lo, hi, 256).getPackedValueProperty(), 0xffffff00u);
    EXPECT_EQ(Color(hi, lo, -1).getPackedValueProperty(), 0xff0000ffu);
    EXPECT_EQ(Color(lo, hi, 256, lo).getPackedValueProperty(), 0x00ffff00u);
    EXPECT_EQ(Color(hi, lo, -1, hi).getPackedValueProperty(), 0xff0000ffu);
}

TEST(Tq051ColorTest, PackedValueAndSettersPreserveOtherBytesAndCanClearBits)
{
    // Refresh get/set_PackedValue IL_0000-0007; component setters mask then OR
    // (R IL_0007-000e, G IL_0007-0010, B/A IL_0007-0011). Hex words follow AABBGGRR.
    Color c;
    c.setPackedValueProperty(0xa1b2c3d4u);
    EXPECT_EQ(c.getPackedValueProperty(), 0xa1b2c3d4u);
    EXPECT_EQ(c.getRProperty(), 0xd4);
    EXPECT_EQ(c.getGProperty(), 0xc3);
    EXPECT_EQ(c.getBProperty(), 0xb2);
    EXPECT_EQ(c.getAProperty(), 0xa1);
    c.setRProperty(0); EXPECT_EQ(c.getPackedValueProperty(), 0xa1b2c300u);
    c.setGProperty(0); EXPECT_EQ(c.getPackedValueProperty(), 0xa1b20000u);
    c.setBProperty(0); EXPECT_EQ(c.getPackedValueProperty(), 0xa1000000u);
    c.setAProperty(0); EXPECT_EQ(c.getPackedValueProperty(), 0u);
    c.setRProperty(0x12); c.setGProperty(0x34); c.setBProperty(0x56); c.setAProperty(0x78);
    EXPECT_EQ(c.getPackedValueProperty(), 0x78563412u);
}

TEST(Tq051ColorTest, EqualityAndHashUseEveryPackedByte)
{
    // Refresh Equals(Color) IL_0000-0012, operators IL_0000-0008/000b;
    // GetHashCode IL_0000-000b returns the packed word. CNA's size_t surface
    // preserves its low 32 bits, as the C API's int32_t result does.
    Color c; c.setPackedValueProperty(0xa1b2c3d4u);
    EXPECT_EQ(static_cast<std::uint32_t>(c.GetHashCode()), 0xa1b2c3d4u);
    for (const auto bits : {0xa1b2c3d5u, 0xa1b2c2d4u, 0xa1b3c3d4u, 0xa0b2c3d4u})
    {
        Color other; other.setPackedValueProperty(bits);
        EXPECT_FALSE(c.Equals(other)); EXPECT_FALSE(c.Equals(std::any{other}));
        EXPECT_FALSE(c == other); EXPECT_TRUE(c != other);
        EXPECT_EQ(static_cast<std::uint32_t>(other.GetHashCode()), bits);
    }
    EXPECT_TRUE(c.Equals(std::any{c}));
    EXPECT_FALSE(c.Equals(std::any{})); EXPECT_FALSE(c.Equals(std::any{0xa1b2c3d4u}));
    // Refresh Color.ToString IL_0005-004f uses this exact field order and punctuation.
    EXPECT_EQ(c.ToString(), "{R:212 G:195 B:178 A:161}");
}

TEST(Tq051ColorTest, VectorsDivideEachFractionalByteBy255)
{
    // Refresh Color.ToVector3 IL_000d/0026/0040, ToVector4 adds IL_005a,
    // PackUtils.UnpackUNorm IL_0000-000c: (byte & 255) / Single(255).
    // Exact binary32 nearest-even quotients 1/255,64/255,128/255,254/255;
    // assert bits (zero tolerance), since the IL ends in one Single division.
    const Color c(1, 64, 128, 254);
    const auto v3 = c.ToVector3(); const auto v4 = c.ToVector4();
    EXPECT_EQ(std::bit_cast<std::uint32_t>(v3.X), 0x3b808081u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(v3.Y), 0x3e808081u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(v3.Z), 0x3f008081u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(v4.X), 0x3b808081u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(v4.Y), 0x3e808081u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(v4.Z), 0x3f008081u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(v4.W), 0x3f7efeffu);
}

TEST(Tq051ColorTest, LerpHasLiteralFactorTiesAndDescendingChannels)
{
    // Refresh Color.Lerp IL_003f-004a packs factor; IL_0051-0082 uses signed
    // delta*factor >> 16. Half: 0+floor(255/2)=127; 255+floor(-255/2)=127.
    const Color a(0, 255, 1, 254), b(255, 0, 254, 1);
    EXPECT_EQ(Color::Lerp(a,b,.5f).getPackedValueProperty(), 0x7f7f7f7fu);
    // 256.5 -> even 256: floor(255*256/65536)=0; 257.5 -> 258: =1.
    EXPECT_EQ(Color::Lerp(a,b,513.0f/131072).getPackedValueProperty(), 0xfd01fe00u);
    EXPECT_EQ(Color::Lerp(a,b,515.0f/131072).getPackedValueProperty(), 0xfd01fd01u);
    EXPECT_EQ(Color::Lerp(a,b,-1).getPackedValueProperty(), 0xfe01ff00u);
    EXPECT_EQ(Color::Lerp(a,b,2).getPackedValueProperty(), 0x01fe00ffu);
}

TEST(Tq051ColorTest, MultiplyTruncatesFactorBeforeScalingEveryChannel)
{
    // Refresh Multiply and op_Multiply IL_0022-0049 truncate scale*65536;
    // IL_004a-0067 multiply unsigned channels and shift. 255*257=65535,
    // 255*258=65790; 254*258=65532. All expected words are hand-derived.
    const Color c(255,254,1,0);
    for (const float scale : {515.0f/131072, 516.0f/131072})
    {
        const std::uint32_t expected = scale == 515.0f/131072 ? 0u : 1u;
        EXPECT_EQ(Color::Multiply(c,scale).getPackedValueProperty(), expected);
        EXPECT_EQ((c*scale).getPackedValueProperty(), expected);
        EXPECT_EQ((scale*c).getPackedValueProperty(), expected); // Existing extension.
    }
    // floor((10,20,30,40)*72089/65536) = (10,21,32,43).
    EXPECT_EQ(Color::Multiply(Color(10,20,30,40),1.1f).getPackedValueProperty(), 0x2b20150au);
}

TEST(Tq051ColorTest, IntegerPremultiplicationWidensBeforeDivisionAndClamp)
{
    // Refresh FromNonPremultiplied(int,int,int,int) IL_0000-003f:
    // conv.i8 for BOTH operands, mul, div 255, ClampToByte64, then clamp alpha.
    constexpr auto lo = std::numeric_limits<SharpRuntime::intcs>::min();
    constexpr auto hi = std::numeric_limits<SharpRuntime::intcs>::max();
    struct Case { SharpRuntime::intcs r,g,b,a; std::uint32_t expected; };
    const Case cases[] = {
        {hi,hi,hi,hi,0xffffffffu}, // (2^31-1)^2 / 255 > 255, no Int32 wrap
        {lo,lo,lo,lo,0x00ffffffu}, // 2^62 / 255 > 255; alpha still clamps to 0
        {hi,lo,1,2,0x020000ffu},
        {-1,128,300,128,0x80964000u}, // trunc(-128/255)=0; 16384/255=64; 38400/255=150
        {-255,-128,255,-255,0x000080ffu}, // multiply before input/alpha clamp
        {1,2,3,128,0x80010100u}, // 128/255=0,256/255=1,384/255=1
        {hi,lo,1,0,0u},
    };
    for (const auto& c : cases)
    {
        SCOPED_TRACE(c.a);
        EXPECT_EQ(Color::FromNonPremultiplied(c.r,c.g,c.b,c.a).getPackedValueProperty(), c.expected);
    }
}

TEST(Tq051ColorTest, VectorPremultiplicationOccursBeforeClampingAndPacking)
{
    // Refresh FromNonPremultiplied(Vector4) IL_0002-0036 multiplies raw RGB
    // by raw W before PackHelper. .25/.5/.75 * .5 *255 -> 32,64,96.
    EXPECT_EQ(Color::FromNonPremultiplied(Vector4(.25f,.5f,.75f,.5f)).getPackedValueProperty(),0x80604020u);
    EXPECT_EQ(Color::FromNonPremultiplied(Vector4(2,-2,.5f,.5f)).getPackedValueProperty(),0x804000ffu);
    EXPECT_EQ(Color::FromNonPremultiplied(Vector4(.5f,-.5f,2,2)).getPackedValueProperty(),0xffff00ffu);
    EXPECT_EQ(Color::FromNonPremultiplied(Vector4(-1,-.5f,1,-.5f)).getPackedValueProperty(),0x00004080u);
}
