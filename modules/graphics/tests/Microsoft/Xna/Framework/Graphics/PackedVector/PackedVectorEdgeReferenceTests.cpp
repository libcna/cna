// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
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

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics::PackedVector;

namespace
{
    void ExpectVector(const Vector4& value, const Vector4& expected)
    {
        const float actual[] = {value.X, value.Y, value.Z, value.W};
        const float wanted[] = {expected.X, expected.Y, expected.Z, expected.W};
        for (int i = 0; i < 4; ++i)
            EXPECT_EQ(std::bit_cast<std::uint32_t>(actual[i]), std::bit_cast<std::uint32_t>(wanted[i])) << i;
    }

    template<class T>
    void CheckPacked(const Vector4& input, const T& scalar, const T& vector, std::uint64_t expected)
    {
        EXPECT_EQ(static_cast<std::uint64_t>(scalar.getPackedValueProperty()), expected);
        EXPECT_EQ(static_cast<std::uint64_t>(vector.getPackedValueProperty()), expected);
        T packed;
        packed.PackFromVector4(input);
        EXPECT_EQ(static_cast<std::uint64_t>(packed.getPackedValueProperty()), expected);
    }
}

TEST(PackedVectorEdgeReferenceTest, Alpha8BoundariesAndRawOne)
{
    struct Case { Vector4 input; std::uint64_t packed; };
    const Case cases[] = {
        {Vector4{0.0019607844296842813f, 0.0058823530562222f, 0.009803921915590763f, 0.013725490309298038f}, 0x0000000000000004ULL},
        {Vector4{0.0019607841968536377f, 0.005882352590560913f, 0.009803920984268188f, 0.013725489377975464f}, 0x0000000000000003ULL},
        {Vector4{0.001960784662514925f, 0.005882353521883488f, 0.009803922846913338f, 0.013725491240620613f}, 0x0000000000000004ULL},
        {Vector4{1.00048828125f, 1.00146484375f, 65504.0f, 65520.0f}, 0x00000000000000ffULL},
        {Vector4{2.9802322387695312e-08f, 8.940696716308594e-08f, 5.960464477539063e-08f, 6.097555160522461e-05f}, 0x0000000000000000ULL},
        {Vector4{std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), -0.0f}, 0x0000000000000000ULL},
        {Vector4{-0.78125f, -0.140625f, 1.375f, 0.390625f}, 0x0000000000000064ULL},
    };
    for (const auto& test : cases)
    {
        const auto input = test.input;
        CheckPacked(input, Alpha8{input.W}, Alpha8{input.W}, test.packed);
    }
    Alpha8 raw; raw.setPackedValueProperty(1);
    EXPECT_EQ(raw.getPackedValueProperty(), 1);
    ExpectVector(raw.ToVector4(), Vector4{0.0f, 0.0f, 0.0f, 0.003921568859368563f});
    Alpha8 repacked; repacked.PackFromVector4(raw.ToVector4());
    EXPECT_EQ(repacked.getPackedValueProperty(), 1);
}

TEST(PackedVectorEdgeReferenceTest, Bgr565BoundariesAndRawOne)
{
    struct Case { Vector4 input; std::uint64_t packed; };
    const Case cases[] = {
        {Vector4{0.016129031777381897f, 0.02380952425301075f, 0.08064515888690948f, 3.5f}, 0x0000000000000042ULL},
        {Vector4{0.016129029914736748f, 0.0238095223903656f, 0.08064515143632889f, 3.499999761581421f}, 0x0000000000000022ULL},
        {Vector4{0.016129033640027046f, 0.0238095261156559f, 0.08064516633749008f, 3.500000238418579f}, 0x0000000000000843ULL},
        {Vector4{1.00048828125f, 1.00146484375f, 65504.0f, 65520.0f}, 0x000000000000ffffULL},
        {Vector4{2.9802322387695312e-08f, 8.940696716308594e-08f, 5.960464477539063e-08f, 6.097555160522461e-05f}, 0x0000000000000000ULL},
        {Vector4{std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), -0.0f}, 0x00000000000007e0ULL},
        {Vector4{-0.78125f, -0.140625f, 1.375f, 0.390625f}, 0x000000000000001fULL},
    };
    for (const auto& test : cases)
    {
        const auto input = test.input;
        CheckPacked(input, Bgr565{input.X, input.Y, input.Z}, Bgr565{Vector3{input.X, input.Y, input.Z}}, test.packed);
    }
    Bgr565 raw; raw.setPackedValueProperty(1);
    EXPECT_EQ(raw.getPackedValueProperty(), 1);
    ExpectVector(raw.ToVector4(), Vector4{0.0f, 0.0f, 0.032258063554763794f, 1.0f});
    Bgr565 repacked; repacked.PackFromVector4(raw.ToVector4());
    EXPECT_EQ(repacked.getPackedValueProperty(), 1);
}

TEST(PackedVectorEdgeReferenceTest, Bgra4444BoundariesAndRawOne)
{
    struct Case { Vector4 input; std::uint64_t packed; };
    const Case cases[] = {
        {Vector4{0.03333333507180214f, 0.10000000149011612f, 0.1666666716337204f, 0.23333333432674408f}, 0x0000000000004022ULL},
        {Vector4{0.03333333134651184f, 0.09999999403953552f, 0.1666666567325592f, 0.23333331942558289f}, 0x0000000000003012ULL},
        {Vector4{0.03333333879709244f, 0.10000000894069672f, 0.1666666865348816f, 0.23333334922790527f}, 0x0000000000004123ULL},
        {Vector4{1.00048828125f, 1.00146484375f, 65504.0f, 65520.0f}, 0x000000000000ffffULL},
        {Vector4{2.9802322387695312e-08f, 8.940696716308594e-08f, 5.960464477539063e-08f, 6.097555160522461e-05f}, 0x0000000000000000ULL},
        {Vector4{std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), -0.0f}, 0x00000000000000f0ULL},
        {Vector4{-0.78125f, -0.140625f, 1.375f, 0.390625f}, 0x000000000000600fULL},
    };
    for (const auto& test : cases)
    {
        const auto input = test.input;
        CheckPacked(input, Bgra4444{input.X, input.Y, input.Z, input.W}, Bgra4444{input}, test.packed);
    }
    Bgra4444 raw; raw.setPackedValueProperty(1);
    EXPECT_EQ(raw.getPackedValueProperty(), 1);
    ExpectVector(raw.ToVector4(), Vector4{0.0f, 0.0f, 0.06666667014360428f, 0.0f});
    Bgra4444 repacked; repacked.PackFromVector4(raw.ToVector4());
    EXPECT_EQ(repacked.getPackedValueProperty(), 1);
}

TEST(PackedVectorEdgeReferenceTest, Bgra5551BoundariesAndRawOne)
{
    struct Case { Vector4 input; std::uint64_t packed; };
    const Case cases[] = {
        {Vector4{0.016129031777381897f, 0.04838709533214569f, 0.08064515888690948f, 3.5f}, 0x0000000000008042ULL},
        {Vector4{0.016129029914736748f, 0.04838709160685539f, 0.08064515143632889f, 3.499999761581421f}, 0x0000000000008022ULL},
        {Vector4{0.016129033640027046f, 0.04838709905743599f, 0.08064516633749008f, 3.500000238418579f}, 0x0000000000008443ULL},
        {Vector4{1.00048828125f, 1.00146484375f, 65504.0f, 65520.0f}, 0x000000000000ffffULL},
        {Vector4{2.9802322387695312e-08f, 8.940696716308594e-08f, 5.960464477539063e-08f, 6.097555160522461e-05f}, 0x0000000000000000ULL},
        {Vector4{std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), -0.0f}, 0x00000000000003e0ULL},
        {Vector4{-0.78125f, -0.140625f, 1.375f, 0.390625f}, 0x000000000000001fULL},
    };
    for (const auto& test : cases)
    {
        const auto input = test.input;
        CheckPacked(input, Bgra5551{input.X, input.Y, input.Z, input.W}, Bgra5551{input}, test.packed);
    }
    Bgra5551 raw; raw.setPackedValueProperty(1);
    EXPECT_EQ(raw.getPackedValueProperty(), 1);
    ExpectVector(raw.ToVector4(), Vector4{0.0f, 0.0f, 0.032258063554763794f, 0.0f});
    Bgra5551 repacked; repacked.PackFromVector4(raw.ToVector4());
    EXPECT_EQ(repacked.getPackedValueProperty(), 1);
}

TEST(PackedVectorEdgeReferenceTest, Byte4BoundariesAndRawOne)
{
    struct Case { Vector4 input; std::uint64_t packed; };
    const Case cases[] = {
        {Vector4{0.5f, 1.5f, 2.5f, 3.5f}, 0x0000000004020200ULL},
        {Vector4{0.4999999701976776f, 1.4999998807907104f, 2.499999761581421f, 3.499999761581421f}, 0x0000000003020100ULL},
        {Vector4{0.5000000596046448f, 1.5000001192092896f, 2.500000238418579f, 3.500000238418579f}, 0x0000000004030201ULL},
        {Vector4{1.00048828125f, 1.00146484375f, 65504.0f, 65520.0f}, 0x00000000ffff0101ULL},
        {Vector4{2.9802322387695312e-08f, 8.940696716308594e-08f, 5.960464477539063e-08f, 6.097555160522461e-05f}, 0x0000000000000000ULL},
        {Vector4{std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), -0.0f}, 0x000000000000ff00ULL},
        {Vector4{-0.78125f, -0.140625f, 1.375f, 0.390625f}, 0x0000000000010000ULL},
    };
    for (const auto& test : cases)
    {
        const auto input = test.input;
        CheckPacked(input, Byte4{input.X, input.Y, input.Z, input.W}, Byte4{input}, test.packed);
    }
    Byte4 raw; raw.setPackedValueProperty(1);
    EXPECT_EQ(raw.getPackedValueProperty(), 1);
    ExpectVector(raw.ToVector4(), Vector4{1.0f, 0.0f, 0.0f, 0.0f});
    Byte4 repacked; repacked.PackFromVector4(raw.ToVector4());
    EXPECT_EQ(repacked.getPackedValueProperty(), 1);
}

TEST(PackedVectorEdgeReferenceTest, HalfSingleBoundariesAndRawOne)
{
    struct Case { Vector4 input; std::uint64_t packed; };
    const Case cases[] = {
        {Vector4{0.5f, 1.5f, 2.5f, 3.5f}, 0x0000000000003800ULL},
        {Vector4{0.4999999701976776f, 1.4999998807907104f, 2.499999761581421f, 3.499999761581421f}, 0x0000000000003800ULL},
        {Vector4{0.5000000596046448f, 1.5000001192092896f, 2.500000238418579f, 3.500000238418579f}, 0x0000000000003800ULL},
        {Vector4{1.00048828125f, 1.00146484375f, 65504.0f, 65520.0f}, 0x0000000000003c00ULL},
        {Vector4{2.9802322387695312e-08f, 8.940696716308594e-08f, 5.960464477539063e-08f, 6.097555160522461e-05f}, 0x0000000000000000ULL},
        {Vector4{std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), -0.0f}, 0x0000000000007fffULL},
        {Vector4{-0.78125f, -0.140625f, 1.375f, 0.390625f}, 0x000000000000ba40ULL},
    };
    for (const auto& test : cases)
    {
        const auto input = test.input;
        CheckPacked(input, HalfSingle{input.X}, HalfSingle{input.X}, test.packed);
    }
    HalfSingle raw; raw.setPackedValueProperty(1);
    EXPECT_EQ(raw.getPackedValueProperty(), 1);
    ExpectVector(raw.ToVector4(), Vector4{5.960464477539063e-08f, 0.0f, 0.0f, 1.0f});
    HalfSingle repacked; repacked.PackFromVector4(raw.ToVector4());
    EXPECT_EQ(repacked.getPackedValueProperty(), 1);
}

TEST(PackedVectorEdgeReferenceTest, HalfVector2BoundariesAndRawOne)
{
    struct Case { Vector4 input; std::uint64_t packed; };
    const Case cases[] = {
        {Vector4{0.5f, 1.5f, 2.5f, 3.5f}, 0x000000003e003800ULL},
        {Vector4{0.4999999701976776f, 1.4999998807907104f, 2.499999761581421f, 3.499999761581421f}, 0x000000003e003800ULL},
        {Vector4{0.5000000596046448f, 1.5000001192092896f, 2.500000238418579f, 3.500000238418579f}, 0x000000003e003800ULL},
        {Vector4{1.00048828125f, 1.00146484375f, 65504.0f, 65520.0f}, 0x000000003c023c00ULL},
        {Vector4{2.9802322387695312e-08f, 8.940696716308594e-08f, 5.960464477539063e-08f, 6.097555160522461e-05f}, 0x0000000000020000ULL},
        {Vector4{std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), -0.0f}, 0x000000007fff7fffULL},
        {Vector4{-0.78125f, -0.140625f, 1.375f, 0.390625f}, 0x00000000b080ba40ULL},
    };
    for (const auto& test : cases)
    {
        const auto input = test.input;
        CheckPacked(input, HalfVector2{input.X, input.Y}, HalfVector2{Vector2{input.X, input.Y}}, test.packed);
    }
    HalfVector2 raw; raw.setPackedValueProperty(1);
    EXPECT_EQ(raw.getPackedValueProperty(), 1);
    ExpectVector(raw.ToVector4(), Vector4{5.960464477539063e-08f, 0.0f, 0.0f, 1.0f});
    HalfVector2 repacked; repacked.PackFromVector4(raw.ToVector4());
    EXPECT_EQ(repacked.getPackedValueProperty(), 1);
}

TEST(PackedVectorEdgeReferenceTest, HalfVector4BoundariesAndRawOne)
{
    struct Case { Vector4 input; std::uint64_t packed; };
    const Case cases[] = {
        {Vector4{0.5f, 1.5f, 2.5f, 3.5f}, 0x430041003e003800ULL},
        {Vector4{0.4999999701976776f, 1.4999998807907104f, 2.499999761581421f, 3.499999761581421f}, 0x430041003e003800ULL},
        {Vector4{0.5000000596046448f, 1.5000001192092896f, 2.500000238418579f, 3.500000238418579f}, 0x430041003e003800ULL},
        {Vector4{1.00048828125f, 1.00146484375f, 65504.0f, 65520.0f}, 0x7c007bff3c023c00ULL},
        {Vector4{2.9802322387695312e-08f, 8.940696716308594e-08f, 5.960464477539063e-08f, 6.097555160522461e-05f}, 0x03ff000100020000ULL},
        {Vector4{std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), -0.0f}, 0x8000ffff7fff7fffULL},
        {Vector4{-0.78125f, -0.140625f, 1.375f, 0.390625f}, 0x36403d80b080ba40ULL},
    };
    for (const auto& test : cases)
    {
        const auto input = test.input;
        CheckPacked(input, HalfVector4{input.X, input.Y, input.Z, input.W}, HalfVector4{input}, test.packed);
    }
    HalfVector4 raw; raw.setPackedValueProperty(1);
    EXPECT_EQ(raw.getPackedValueProperty(), 1);
    ExpectVector(raw.ToVector4(), Vector4{5.960464477539063e-08f, 0.0f, 0.0f, 0.0f});
    HalfVector4 repacked; repacked.PackFromVector4(raw.ToVector4());
    EXPECT_EQ(repacked.getPackedValueProperty(), 1);
}

TEST(PackedVectorEdgeReferenceTest, NormalizedByte2BoundariesAndRawOne)
{
    struct Case { Vector4 input; std::uint64_t packed; };
    const Case cases[] = {
        {Vector4{0.003937007859349251f, 0.011811023578047752f, 0.019685039296746254f, 0.027559055015444756f}, 0x0000000000000200ULL},
        {Vector4{0.0039370073936879635f, 0.011811022646725178f, 0.019685037434101105f, 0.027559053152799606f}, 0x0000000000000100ULL},
        {Vector4{0.003937008325010538f, 0.011811024509370327f, 0.019685041159391403f, 0.027559056878089905f}, 0x0000000000000201ULL},
        {Vector4{1.00048828125f, 1.00146484375f, 65504.0f, 65520.0f}, 0x0000000000007f7fULL},
        {Vector4{2.9802322387695312e-08f, 8.940696716308594e-08f, 5.960464477539063e-08f, 6.097555160522461e-05f}, 0x0000000000000000ULL},
        {Vector4{std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), -0.0f}, 0x0000000000007f00ULL},
        {Vector4{-0.78125f, -0.140625f, 1.375f, 0.390625f}, 0x000000000000ee9dULL},
    };
    for (const auto& test : cases)
    {
        const auto input = test.input;
        CheckPacked(input, NormalizedByte2{input.X, input.Y}, NormalizedByte2{Vector2{input.X, input.Y}}, test.packed);
    }
    NormalizedByte2 raw; raw.setPackedValueProperty(1);
    EXPECT_EQ(raw.getPackedValueProperty(), 1);
    ExpectVector(raw.ToVector4(), Vector4{0.007874015718698502f, 0.0f, 0.0f, 1.0f});
    NormalizedByte2 repacked; repacked.PackFromVector4(raw.ToVector4());
    EXPECT_EQ(repacked.getPackedValueProperty(), 1);
}

TEST(PackedVectorEdgeReferenceTest, NormalizedByte4BoundariesAndRawOne)
{
    struct Case { Vector4 input; std::uint64_t packed; };
    const Case cases[] = {
        {Vector4{0.003937007859349251f, 0.011811023578047752f, 0.019685039296746254f, 0.027559055015444756f}, 0x0000000004020200ULL},
        {Vector4{0.0039370073936879635f, 0.011811022646725178f, 0.019685037434101105f, 0.027559053152799606f}, 0x0000000003020100ULL},
        {Vector4{0.003937008325010538f, 0.011811024509370327f, 0.019685041159391403f, 0.027559056878089905f}, 0x0000000004030201ULL},
        {Vector4{1.00048828125f, 1.00146484375f, 65504.0f, 65520.0f}, 0x000000007f7f7f7fULL},
        {Vector4{2.9802322387695312e-08f, 8.940696716308594e-08f, 5.960464477539063e-08f, 6.097555160522461e-05f}, 0x0000000000000000ULL},
        {Vector4{std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), -0.0f}, 0x0000000000817f00ULL},
        {Vector4{-0.78125f, -0.140625f, 1.375f, 0.390625f}, 0x00000000327fee9dULL},
    };
    for (const auto& test : cases)
    {
        const auto input = test.input;
        CheckPacked(input, NormalizedByte4{input.X, input.Y, input.Z, input.W}, NormalizedByte4{input}, test.packed);
    }
    NormalizedByte4 raw; raw.setPackedValueProperty(1);
    EXPECT_EQ(raw.getPackedValueProperty(), 1);
    ExpectVector(raw.ToVector4(), Vector4{0.007874015718698502f, 0.0f, 0.0f, 0.0f});
    NormalizedByte4 repacked; repacked.PackFromVector4(raw.ToVector4());
    EXPECT_EQ(repacked.getPackedValueProperty(), 1);
}

TEST(PackedVectorEdgeReferenceTest, NormalizedShort2BoundariesAndRawOne)
{
    struct Case { Vector4 input; std::uint64_t packed; };
    const Case cases[] = {
        {Vector4{1.5259254723787308e-05f, 4.577776417136192e-05f, 7.629627361893654e-05f, 0.00010681478306651115f}, 0x0000000000020000ULL},
        {Vector4{1.5259252904797904e-05f, 4.5777760533383116e-05f, 7.629626634297892e-05f, 0.00010681477579055354f}, 0x0000000000010000ULL},
        {Vector4{1.525925654277671e-05f, 4.577776780934073e-05f, 7.629628089489415e-05f, 0.00010681479034246877f}, 0x0000000000020001ULL},
        {Vector4{1.00048828125f, 1.00146484375f, 65504.0f, 65520.0f}, 0x000000007fff7fffULL},
        {Vector4{2.9802322387695312e-08f, 8.940696716308594e-08f, 5.960464477539063e-08f, 6.097555160522461e-05f}, 0x0000000000000000ULL},
        {Vector4{std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), -0.0f}, 0x000000007fff0000ULL},
        {Vector4{-0.78125f, -0.140625f, 1.375f, 0.390625f}, 0x00000000ee009c01ULL},
    };
    for (const auto& test : cases)
    {
        const auto input = test.input;
        CheckPacked(input, NormalizedShort2{input.X, input.Y}, NormalizedShort2{Vector2{input.X, input.Y}}, test.packed);
    }
    NormalizedShort2 raw; raw.setPackedValueProperty(1);
    EXPECT_EQ(raw.getPackedValueProperty(), 1);
    ExpectVector(raw.ToVector4(), Vector4{3.0518509447574615e-05f, 0.0f, 0.0f, 1.0f});
    NormalizedShort2 repacked; repacked.PackFromVector4(raw.ToVector4());
    EXPECT_EQ(repacked.getPackedValueProperty(), 1);
}

TEST(PackedVectorEdgeReferenceTest, NormalizedShort4BoundariesAndRawOne)
{
    struct Case { Vector4 input; std::uint64_t packed; };
    const Case cases[] = {
        {Vector4{1.5259254723787308e-05f, 4.577776417136192e-05f, 7.629627361893654e-05f, 0.00010681478306651115f}, 0x0004000200020000ULL},
        {Vector4{1.5259252904797904e-05f, 4.5777760533383116e-05f, 7.629626634297892e-05f, 0.00010681477579055354f}, 0x0003000200010000ULL},
        {Vector4{1.525925654277671e-05f, 4.577776780934073e-05f, 7.629628089489415e-05f, 0.00010681479034246877f}, 0x0004000300020001ULL},
        {Vector4{1.00048828125f, 1.00146484375f, 65504.0f, 65520.0f}, 0x7fff7fff7fff7fffULL},
        {Vector4{2.9802322387695312e-08f, 8.940696716308594e-08f, 5.960464477539063e-08f, 6.097555160522461e-05f}, 0x0002000000000000ULL},
        {Vector4{std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), -0.0f}, 0x000080017fff0000ULL},
        {Vector4{-0.78125f, -0.140625f, 1.375f, 0.390625f}, 0x32007fffee009c01ULL},
    };
    for (const auto& test : cases)
    {
        const auto input = test.input;
        CheckPacked(input, NormalizedShort4{input.X, input.Y, input.Z, input.W}, NormalizedShort4{input}, test.packed);
    }
    NormalizedShort4 raw; raw.setPackedValueProperty(1);
    EXPECT_EQ(raw.getPackedValueProperty(), 1);
    ExpectVector(raw.ToVector4(), Vector4{3.0518509447574615e-05f, 0.0f, 0.0f, 0.0f});
    NormalizedShort4 repacked; repacked.PackFromVector4(raw.ToVector4());
    EXPECT_EQ(repacked.getPackedValueProperty(), 1);
}

TEST(PackedVectorEdgeReferenceTest, Rg32BoundariesAndRawOne)
{
    struct Case { Vector4 input; std::uint64_t packed; };
    const Case cases[] = {
        {Vector4{7.629510946571827e-06f, 2.288853283971548e-05f, 3.8147554732859135e-05f, 5.340657662600279e-05f}, 0x0000000000020000ULL},
        {Vector4{7.629510037077125e-06f, 2.2888531020726077e-05f, 3.814755109488033e-05f, 5.340657298802398e-05f}, 0x0000000000010000ULL},
        {Vector4{7.629511856066529e-06f, 2.2888534658704884e-05f, 3.814755837083794e-05f, 5.3406580263981596e-05f}, 0x0000000000020001ULL},
        {Vector4{1.00048828125f, 1.00146484375f, 65504.0f, 65520.0f}, 0x00000000ffffffffULL},
        {Vector4{2.9802322387695312e-08f, 8.940696716308594e-08f, 5.960464477539063e-08f, 6.097555160522461e-05f}, 0x0000000000000000ULL},
        {Vector4{std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), -0.0f}, 0x00000000ffff0000ULL},
        {Vector4{-0.78125f, -0.140625f, 1.375f, 0.390625f}, 0x0000000000000000ULL},
    };
    for (const auto& test : cases)
    {
        const auto input = test.input;
        CheckPacked(input, Rg32{input.X, input.Y}, Rg32{Vector2{input.X, input.Y}}, test.packed);
    }
    Rg32 raw; raw.setPackedValueProperty(1);
    EXPECT_EQ(raw.getPackedValueProperty(), 1);
    ExpectVector(raw.ToVector4(), Vector4{1.5259021893143654e-05f, 0.0f, 0.0f, 1.0f});
    Rg32 repacked; repacked.PackFromVector4(raw.ToVector4());
    EXPECT_EQ(repacked.getPackedValueProperty(), 1);
}

TEST(PackedVectorEdgeReferenceTest, Rgba1010102BoundariesAndRawOne)
{
    struct Case { Vector4 input; std::uint64_t packed; };
    const Case cases[] = {
        {Vector4{0.0004887585528194904f, 0.0014662756584584713f, 0.002443792764097452f, 1.1666666269302368f}, 0x00000000c0200800ULL},
        {Vector4{0.0004887584946118295f, 0.0014662755420431495f, 0.0024437925312668085f, 1.1666665077209473f}, 0x00000000c0200400ULL},
        {Vector4{0.0004887586110271513f, 0.0014662757748737931f, 0.002443792996928096f, 1.1666667461395264f}, 0x00000000c0300801ULL},
        {Vector4{1.00048828125f, 1.00146484375f, 65504.0f, 65520.0f}, 0x00000000ffffffffULL},
        {Vector4{2.9802322387695312e-08f, 8.940696716308594e-08f, 5.960464477539063e-08f, 6.097555160522461e-05f}, 0x0000000000000000ULL},
        {Vector4{std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), -0.0f}, 0x00000000000ffc00ULL},
        {Vector4{-0.78125f, -0.140625f, 1.375f, 0.390625f}, 0x000000007ff00000ULL},
    };
    for (const auto& test : cases)
    {
        const auto input = test.input;
        CheckPacked(input, Rgba1010102{input.X, input.Y, input.Z, input.W}, Rgba1010102{input}, test.packed);
    }
    Rgba1010102 raw; raw.setPackedValueProperty(1);
    EXPECT_EQ(raw.getPackedValueProperty(), 1);
    ExpectVector(raw.ToVector4(), Vector4{0.0009775171056389809f, 0.0f, 0.0f, 0.0f});
    Rgba1010102 repacked; repacked.PackFromVector4(raw.ToVector4());
    EXPECT_EQ(repacked.getPackedValueProperty(), 1);
}

TEST(PackedVectorEdgeReferenceTest, Rgba64BoundariesAndRawOne)
{
    struct Case { Vector4 input; std::uint64_t packed; };
    const Case cases[] = {
        {Vector4{7.629510946571827e-06f, 2.288853283971548e-05f, 3.8147554732859135e-05f, 5.340657662600279e-05f}, 0x0004000200020000ULL},
        {Vector4{7.629510037077125e-06f, 2.2888531020726077e-05f, 3.814755109488033e-05f, 5.340657298802398e-05f}, 0x0003000200010000ULL},
        {Vector4{7.629511856066529e-06f, 2.2888534658704884e-05f, 3.814755837083794e-05f, 5.3406580263981596e-05f}, 0x0004000300020001ULL},
        {Vector4{1.00048828125f, 1.00146484375f, 65504.0f, 65520.0f}, 0xffffffffffffffffULL},
        {Vector4{2.9802322387695312e-08f, 8.940696716308594e-08f, 5.960464477539063e-08f, 6.097555160522461e-05f}, 0x0004000000000000ULL},
        {Vector4{std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), -0.0f}, 0x00000000ffff0000ULL},
        {Vector4{-0.78125f, -0.140625f, 1.375f, 0.390625f}, 0x6400ffff00000000ULL},
    };
    for (const auto& test : cases)
    {
        const auto input = test.input;
        CheckPacked(input, Rgba64{input.X, input.Y, input.Z, input.W}, Rgba64{input}, test.packed);
    }
    Rgba64 raw; raw.setPackedValueProperty(1);
    EXPECT_EQ(raw.getPackedValueProperty(), 1);
    ExpectVector(raw.ToVector4(), Vector4{1.5259021893143654e-05f, 0.0f, 0.0f, 0.0f});
    Rgba64 repacked; repacked.PackFromVector4(raw.ToVector4());
    EXPECT_EQ(repacked.getPackedValueProperty(), 1);
}

TEST(PackedVectorEdgeReferenceTest, Short2BoundariesAndRawOne)
{
    struct Case { Vector4 input; std::uint64_t packed; };
    const Case cases[] = {
        {Vector4{0.5f, 1.5f, 2.5f, 3.5f}, 0x0000000000020000ULL},
        {Vector4{0.4999999701976776f, 1.4999998807907104f, 2.499999761581421f, 3.499999761581421f}, 0x0000000000010000ULL},
        {Vector4{0.5000000596046448f, 1.5000001192092896f, 2.500000238418579f, 3.500000238418579f}, 0x0000000000020001ULL},
        {Vector4{1.00048828125f, 1.00146484375f, 65504.0f, 65520.0f}, 0x0000000000010001ULL},
        {Vector4{2.9802322387695312e-08f, 8.940696716308594e-08f, 5.960464477539063e-08f, 6.097555160522461e-05f}, 0x0000000000000000ULL},
        {Vector4{std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), -0.0f}, 0x000000007fff0000ULL},
        {Vector4{-0.78125f, -0.140625f, 1.375f, 0.390625f}, 0x000000000000ffffULL},
    };
    for (const auto& test : cases)
    {
        const auto input = test.input;
        CheckPacked(input, Short2{input.X, input.Y}, Short2{Vector2{input.X, input.Y}}, test.packed);
    }
    Short2 raw; raw.setPackedValueProperty(1);
    EXPECT_EQ(raw.getPackedValueProperty(), 1);
    ExpectVector(raw.ToVector4(), Vector4{1.0f, 0.0f, 0.0f, 1.0f});
    Short2 repacked; repacked.PackFromVector4(raw.ToVector4());
    EXPECT_EQ(repacked.getPackedValueProperty(), 1);
}

TEST(PackedVectorEdgeReferenceTest, Short4BoundariesAndRawOne)
{
    struct Case { Vector4 input; std::uint64_t packed; };
    const Case cases[] = {
        {Vector4{0.5f, 1.5f, 2.5f, 3.5f}, 0x0004000200020000ULL},
        {Vector4{0.4999999701976776f, 1.4999998807907104f, 2.499999761581421f, 3.499999761581421f}, 0x0003000200010000ULL},
        {Vector4{0.5000000596046448f, 1.5000001192092896f, 2.500000238418579f, 3.500000238418579f}, 0x0004000300020001ULL},
        {Vector4{1.00048828125f, 1.00146484375f, 65504.0f, 65520.0f}, 0x7fff7fff00010001ULL},
        {Vector4{2.9802322387695312e-08f, 8.940696716308594e-08f, 5.960464477539063e-08f, 6.097555160522461e-05f}, 0x0000000000000000ULL},
        {Vector4{std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), -0.0f}, 0x000080007fff0000ULL},
        {Vector4{-0.78125f, -0.140625f, 1.375f, 0.390625f}, 0x000000010000ffffULL},
    };
    for (const auto& test : cases)
    {
        const auto input = test.input;
        CheckPacked(input, Short4{input.X, input.Y, input.Z, input.W}, Short4{input}, test.packed);
    }
    Short4 raw; raw.setPackedValueProperty(1);
    EXPECT_EQ(raw.getPackedValueProperty(), 1);
    ExpectVector(raw.ToVector4(), Vector4{1.0f, 0.0f, 0.0f, 0.0f});
    Short4 repacked; repacked.PackFromVector4(raw.ToVector4());
    EXPECT_EQ(repacked.getPackedValueProperty(), 1);
}
