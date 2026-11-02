// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <cstdint>
#include <limits>
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
using namespace Microsoft::Xna::Framework;

TEST(ColorEdgeReferenceTest, FloatConstructorsAndPremultiplicationMatchLiteralReferenceValues)
{
    struct Case { Vector4 input; UInt32 rgba; UInt32 rgb; UInt32 premultiplied; };
    const Case cases[] = {
        {Vector4{0.25f, 0.5f, 0.75f, 1.0f}, 0xffbf8040U, 0xffbf8040U, 0xffbf8040U},
        {Vector4{0.0019607841968536377f, 0.005882352590560913f, 0.009803920984268188f, 0.013725489377975464f}, 0x03020100U, 0xff020100U, 0x03000000U},
        {Vector4{0.0019607844296842813f, 0.0058823530562222f, 0.009803921915590763f, 0.013725490309298038f}, 0x04020200U, 0xff020200U, 0x04000000U},
        {Vector4{0.001960784662514925f, 0.005882353521883488f, 0.009803922846913338f, 0.013725491240620613f}, 0x04030201U, 0xff030201U, 0x04000000U},
        {Vector4{-1.125f, -1.0f, -0.125f, 0.0f}, 0x00000000U, 0xff000000U, 0x00000000U},
        {Vector4{0.0f, 0.875f, 1.0f, 1.125f}, 0xffffdf00U, 0xffffdf00U, 0xfffffb00U},
        {Vector4{2.9802322387695312e-08f, 8.940696716308594e-08f, 5.960464477539063e-08f, 6.097555160522461e-05f}, 0x00000000U, 0xff000000U, 0x00000000U},
        {Vector4{std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), -0.0f}, 0x0000ff00U, 0xff00ff00U, 0x00000000U},
        {Vector4{-0.78125f, -0.140625f, 1.375f, 0.390625f}, 0x64ff0000U, 0xffff0000U, 0x64890000U},
    };
    for (const auto& test : cases)
    {
        const auto v = test.input;
        EXPECT_EQ(Color(v).getPackedValueProperty(), test.rgba);
        EXPECT_EQ(Color(v.X, v.Y, v.Z, v.W).getPackedValueProperty(), test.rgba);
        EXPECT_EQ(Color(Vector3(v.X, v.Y, v.Z)).getPackedValueProperty(), test.rgb);
        EXPECT_EQ(Color(v.X, v.Y, v.Z).getPackedValueProperty(), test.rgb);
        Color packed; packed.PackFromVector4(v);
        EXPECT_EQ(packed.getPackedValueProperty(), test.rgba);
        EXPECT_EQ(Color::FromNonPremultiplied(v).getPackedValueProperty(), test.premultiplied);
    }
}

TEST(ColorEdgeReferenceTest, IntegerClampingRawLayoutAndVectorExpansionAreIndependentLiterals)
{
    EXPECT_EQ(Color(-1, 0, 255, 256).getPackedValueProperty(), 0xffff0000U);
    EXPECT_EQ(Color(-1, 0, 255).getPackedValueProperty(), 0xffff0000U);
    EXPECT_EQ(Color::FromNonPremultiplied(1, 2, 3, 128).getPackedValueProperty(), 0x80010100U);
    Color value; value.setPackedValueProperty(0xff0000ffU);
    EXPECT_EQ(value.getRProperty(), 255); EXPECT_EQ(value.getGProperty(), 0);
    EXPECT_EQ(value.getBProperty(), 0); EXPECT_EQ(value.getAProperty(), 255);
    EXPECT_EQ(value.getPackedValueProperty(), 0xff0000ffU);
    EXPECT_EQ(value.ToVector3(), Vector3(1, 0, 0));
    EXPECT_EQ(value.ToVector4(), Vector4(1, 0, 0, 1));
}

TEST(ColorEdgeReferenceTest, FixedPointNaNAndSubunitFactorsRetainTheMeasuredXnaDecision)
{
    const Color first(255, 1, 0, 254), second(0, 254, 1, 0);
    EXPECT_EQ(Color::Lerp(first, second, std::numeric_limits<float>::quiet_NaN()).getPackedValueProperty(), 0xfe0001ffU);
    EXPECT_EQ(Color::Lerp(first, second, 1.0f / 131072).getPackedValueProperty(), 0xfe0001ffU);
    EXPECT_EQ(Color::Lerp(first, second, 0.99999994f).getPackedValueProperty(), 0x0001fe00U);
    const float infinity = std::numeric_limits<float>::infinity();
    EXPECT_EQ(Color::Multiply(first, infinity).getPackedValueProperty(), 0xff00ffffU);
    EXPECT_EQ((first * infinity).getPackedValueProperty(), 0xff00ffffU);
}
