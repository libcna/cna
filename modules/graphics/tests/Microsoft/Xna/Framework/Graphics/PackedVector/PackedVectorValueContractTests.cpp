// SPDX-License-Identifier: MS-PL
//
// The documented XNA 4.0 value contracts of the packed-vector types: typed equality, boxed
// object equality, GetHashCode, ToString, and the two/three-component expansions. Expected
// strings and hash codes come from the Microsoft implementations
// (xna4-decomp/.../Microsoft.Xna.Framework.Graphics.PackedVector/*.cs, PackUtils.cs), not from
// what CNA happens to produce.

#include <gtest/gtest.h>

#include <any>
#include <cstdint>
#include <iterator>
#include <limits>
#include <string>
#include <utility>

#include "Microsoft/Xna/Framework/Color.hpp"
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

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Vector4;
using namespace Microsoft::Xna::Framework::Graphics::PackedVector;

namespace
{
    /// The packed storage type of a packed-vector type, as its own PackedValue property reports it.
    template <typename T>
    using PackedStorage = decltype(std::declval<const T&>().getPackedValueProperty());

    /// Builds a packed value of type T directly from its storage, which is the only way to reach
    /// codes the float constructors cannot produce (notably the SNORM sign endpoints). The storage
    /// type is deduced rather than named so a call fits inside a GoogleTest macro argument.
    template <typename T>
    T FromPacked(std::uint64_t packed)
    {
        T value;
        value.setPackedValueProperty(static_cast<PackedStorage<T>>(packed));
        return value;
    }

    /// The four contracts every one of the seventeen types shares. Instantiated once per type
    /// below with two unequal packed values, so no type is covered only by the generic pass.
    template <typename T>
    void ExpectValueContract(std::uint64_t first, std::uint64_t second)
    {
        ASSERT_NE(first, second);
        const T a = FromPacked<T>(first);
        const T sameAsA = FromPacked<T>(first);
        const T b = FromPacked<T>(second);

        EXPECT_TRUE(a.Equals(sameAsA));
        EXPECT_FALSE(a.Equals(b));

        // Boxed equality: equal value, unequal value, a different type, and an empty object.
        EXPECT_TRUE(a.Equals(std::any(sameAsA)));
        EXPECT_FALSE(a.Equals(std::any(b)));
        EXPECT_FALSE(a.Equals(std::any(42)));
        EXPECT_FALSE(a.Equals(std::any(std::string("Alpha8"))));
        EXPECT_FALSE(a.Equals(std::any()));

        // Equal values hash equally; that is the half of the CLR contract that must hold.
        EXPECT_EQ(a.GetHashCode(), sameAsA.GetHashCode());
        EXPECT_EQ(a.ToString(), sameAsA.ToString());
    }
}

// =============================================================================
// Per-type value contract
// =============================================================================

TEST(PackedVectorValueContractTest, EveryTypeHonoursTheSharedValueContract)
{
    ExpectValueContract<Alpha8>(0x12, 0x34);
    ExpectValueContract<Bgr565>(0x1234, 0x4321);
    ExpectValueContract<Bgra4444>(0x1234, 0x4321);
    ExpectValueContract<Bgra5551>(0x1234, 0x4321);
    ExpectValueContract<Byte4>(0x12345678u, 0x87654321u);
    ExpectValueContract<HalfSingle>(0x3C00, 0x4000);
    ExpectValueContract<HalfVector2>(0x3C004000u, 0x40003C00u);
    ExpectValueContract<HalfVector4>(0x3C0040004200440FuLL, 0x4400420040003C00uLL);
    ExpectValueContract<NormalizedByte2>(0x1234, 0x4321);
    ExpectValueContract<NormalizedByte4>(0x12345678u, 0x87654321u);
    ExpectValueContract<NormalizedShort2>(0x12345678u, 0x87654321u);
    ExpectValueContract<NormalizedShort4>(0x123456789ABCDEF0uLL, 0x0FEDCBA987654321uLL);
    ExpectValueContract<Rg32>(0x12345678u, 0x87654321u);
    ExpectValueContract<Rgba1010102>(0x12345678u, 0x87654321u);
    ExpectValueContract<Rgba64>(0x123456789ABCDEF0uLL, 0x0FEDCBA987654321uLL);
    ExpectValueContract<Short2>(0x12345678u, 0x87654321u);
    ExpectValueContract<Short4>(0x123456789ABCDEF0uLL, 0x0FEDCBA987654321uLL);
}

TEST(PackedVectorValueContractTest, BoxedEqualityRejectsAnotherPackedType)
{
    // Both hold the same 16-bit code, so only the type distinguishes them.
    const Bgr565 colour = FromPacked<Bgr565>(0x1234);
    const Bgra4444 other = FromPacked<Bgra4444>(0x1234);
    EXPECT_FALSE(colour.Equals(std::any(other)));
    EXPECT_FALSE(other.Equals(std::any(colour)));
}

// =============================================================================
// GetHashCode — exact Microsoft values
// =============================================================================

TEST(PackedVectorValueContractTest, ByteAndUInt16HashIsThePackedValue)
{
    EXPECT_EQ(FromPacked<Alpha8>(0xFF).GetHashCode(), 255);
    EXPECT_EQ(FromPacked<Alpha8>(0x00).GetHashCode(), 0);
    EXPECT_EQ(FromPacked<Bgr565>(0xFFFF).GetHashCode(), 65535);
    EXPECT_EQ(FromPacked<Bgra4444>(0x8000).GetHashCode(), 32768);
    EXPECT_EQ(FromPacked<HalfSingle>(0x3C00).GetHashCode(), 15360);
}

TEST(PackedVectorValueContractTest, UInt32HashReinterpretsThePackedValue)
{
    // System.UInt32.GetHashCode() is `(int)m_value`, so the high bit becomes the sign bit.
    EXPECT_EQ(FromPacked<Byte4>(0x00000001u).GetHashCode(), 1);
    EXPECT_EQ(FromPacked<Byte4>(0xFFFFFFFFu).GetHashCode(), -1);
    EXPECT_EQ(FromPacked<Rg32>(0x80000000u).GetHashCode(),
              static_cast<int>(std::numeric_limits<std::int32_t>::min()));
}

TEST(PackedVectorValueContractTest, UInt64HashFoldsTheHalvesWithXor)
{
    // System.UInt64.GetHashCode() is `((int)m_value) ^ (int)(m_value >> 32)`.
    EXPECT_EQ(FromPacked<Rgba64>(0x0000000100000002uLL).GetHashCode(), 1 ^ 2);
    EXPECT_EQ(FromPacked<Short4>(0xFFFFFFFF00000000uLL).GetHashCode(), -1);
    EXPECT_EQ(FromPacked<HalfVector4>(0x00000000FFFFFFFFuLL).GetHashCode(), -1);
    EXPECT_EQ(FromPacked<NormalizedShort4>(0xABCDEF0112345678uLL).GetHashCode(),
              static_cast<int>(static_cast<std::int32_t>(0x12345678u ^ 0xABCDEF01u)));
}

// =============================================================================
// ToString — exact Microsoft formatting
// =============================================================================

TEST(PackedVectorValueContractTest, HexToStringUsesTheStorageWidthInUppercase)
{
    EXPECT_EQ(FromPacked<Alpha8>(0x0A).ToString(), "0A");
    EXPECT_EQ(FromPacked<Alpha8>(0xFF).ToString(), "FF");
    EXPECT_EQ(FromPacked<Bgr565>(0x00AB).ToString(), "00AB");
    EXPECT_EQ(FromPacked<Bgra4444>(0xBEEF).ToString(), "BEEF");
    EXPECT_EQ(FromPacked<Bgra5551>(0x0001).ToString(), "0001");
    EXPECT_EQ(FromPacked<NormalizedByte2>(0xCAFE).ToString(), "CAFE");
    EXPECT_EQ(FromPacked<Byte4>(0x000000ABu).ToString(), "000000AB");
    EXPECT_EQ(FromPacked<NormalizedByte4>(0xDEADBEEFu).ToString(), "DEADBEEF");
    EXPECT_EQ(FromPacked<NormalizedShort2>(0x0000FFFFu).ToString(), "0000FFFF");
    EXPECT_EQ(FromPacked<Rg32>(0x12345678u).ToString(), "12345678");
    EXPECT_EQ(FromPacked<Rgba1010102>(0xFFFFFFFFu).ToString(), "FFFFFFFF");
    EXPECT_EQ(FromPacked<Short2>(0x00000000u).ToString(), "00000000");
    EXPECT_EQ(FromPacked<NormalizedShort4>(0x0123456789ABCDEFuLL).ToString(),
              "0123456789ABCDEF");
    EXPECT_EQ(FromPacked<Rgba64>(0x00000000000000FFuLL).ToString(),
              "00000000000000FF");
    EXPECT_EQ(FromPacked<Short4>(0xFFFFFFFFFFFFFFFFuLL).ToString(),
              "FFFFFFFFFFFFFFFF");
}

TEST(PackedVectorValueContractTest, HalfTypesFormatTheExpandedValue)
{
    // Microsoft formats ToSingle() / ToVector2() / ToVector4() rather than the packed bits, so
    // each of these has to agree with the expanded value's own formatting.
    const HalfSingle one(1.0f);
    EXPECT_EQ(one.ToString(), "1");

    const HalfVector2 pair(1.0f, 2.0f);
    EXPECT_EQ(pair.ToString(), pair.ToVector2().ToString());
    EXPECT_EQ(pair.ToString(), "{X:1 Y:2}");

    const HalfVector4 quad(1.0f, 2.0f, 3.0f, 4.0f);
    EXPECT_EQ(quad.ToString(), quad.ToVector4().ToString());
    EXPECT_EQ(quad.ToString(), "{X:1 Y:2 Z:3 W:4}");
}

// =============================================================================
// ToVector2 / ToVector3
// =============================================================================

TEST(PackedVectorValueContractTest, Bgr565ExpandsToVector3)
{
    const Bgr565 white(1.0f, 1.0f, 1.0f);
    const Vector3 expanded = white.ToVector3();
    EXPECT_FLOAT_EQ(expanded.X, 1.0f);
    EXPECT_FLOAT_EQ(expanded.Y, 1.0f);
    EXPECT_FLOAT_EQ(expanded.Z, 1.0f);

    // The three channels are 5/6/5 bits wide, so each one divides by its own maximum.
    const Bgr565 packed = FromPacked<Bgr565>(0xF81F);
    const Vector3 channels = packed.ToVector3();
    EXPECT_FLOAT_EQ(channels.X, 1.0f);
    EXPECT_FLOAT_EQ(channels.Y, 0.0f);
    EXPECT_FLOAT_EQ(channels.Z, 1.0f);

    // ToVector3 is the RGB of ToVector4, whose W is always 1.
    const Vector4 asVector4 = packed.ToVector4();
    EXPECT_FLOAT_EQ(asVector4.X, channels.X);
    EXPECT_FLOAT_EQ(asVector4.Y, channels.Y);
    EXPECT_FLOAT_EQ(asVector4.Z, channels.Z);
    EXPECT_FLOAT_EQ(asVector4.W, 1.0f);
}

TEST(PackedVectorValueContractTest, Rg32ExpandsToUnsignedNormalizedVector2)
{
    const Rg32 value(0.25f, 1.0f);
    const Vector2 expanded = value.ToVector2();
    EXPECT_NEAR(expanded.X, 0.25f, 1.0e-4f);
    EXPECT_FLOAT_EQ(expanded.Y, 1.0f);
    EXPECT_FLOAT_EQ(FromPacked<Rg32>(0u).ToVector2().X, 0.0f);
}

TEST(PackedVectorValueContractTest, Short2ExpandsToUnscaledVector2)
{
    const Short2 value(-1234.0f, 5678.0f);
    const Vector2 expanded = value.ToVector2();
    EXPECT_FLOAT_EQ(expanded.X, -1234.0f);
    EXPECT_FLOAT_EQ(expanded.Y, 5678.0f);

    const Vector2 extremes = FromPacked<Short2>(0x80008000u).ToVector2();
    EXPECT_FLOAT_EQ(extremes.X, -32768.0f);
    EXPECT_FLOAT_EQ(extremes.Y, -32768.0f);
}

TEST(PackedVectorValueContractTest, SignedNormalizedVector2RoundTripsItsEndpoints)
{
    const NormalizedByte2 bytes(-1.0f, 1.0f);
    EXPECT_FLOAT_EQ(bytes.ToVector2().X, -1.0f);
    EXPECT_FLOAT_EQ(bytes.ToVector2().Y, 1.0f);

    const NormalizedShort2 shorts(-1.0f, 1.0f);
    EXPECT_FLOAT_EQ(shorts.ToVector2().X, -1.0f);
    EXPECT_FLOAT_EQ(shorts.ToVector2().Y, 1.0f);
}

TEST(PackedVectorValueContractTest, SignedNormalizedMinimumCodeExpandsToExactlyMinusOne)
{
    // PackUtils.UnpackSNorm special-cases the most negative code: 0x80 is -1, not -128/127.
    // SNORM packing clamps at -127, so this code is only reachable through PackedValue.
    const Vector2 bytes = FromPacked<NormalizedByte2>(0x0080).ToVector2();
    EXPECT_FLOAT_EQ(bytes.X, -1.0f);

    const Vector2 shorts = FromPacked<NormalizedShort2>(0x00008000u).ToVector2();
    EXPECT_FLOAT_EQ(shorts.X, -1.0f);

    const Vector4 byteQuad = FromPacked<NormalizedByte4>(0x80808080u).ToVector4();
    EXPECT_FLOAT_EQ(byteQuad.X, -1.0f);
    EXPECT_FLOAT_EQ(byteQuad.W, -1.0f);

    const Vector4 shortQuad =
        FromPacked<NormalizedShort4>(0x8000800080008000uLL).ToVector4();
    EXPECT_FLOAT_EQ(shortQuad.X, -1.0f);
    EXPECT_FLOAT_EQ(shortQuad.W, -1.0f);
}

TEST(PackedVectorValueContractTest, TwoComponentExpansionMatchesTheVector4Channels)
{
    const NormalizedByte2 bytes(-0.5f, 0.25f);
    EXPECT_FLOAT_EQ(bytes.ToVector2().X, bytes.ToVector4().X);
    EXPECT_FLOAT_EQ(bytes.ToVector2().Y, bytes.ToVector4().Y);

    const NormalizedShort2 shorts(-0.5f, 0.25f);
    EXPECT_FLOAT_EQ(shorts.ToVector2().X, shorts.ToVector4().X);
    EXPECT_FLOAT_EQ(shorts.ToVector2().Y, shorts.ToVector4().Y);

    const Rg32 unsignedPair(0.5f, 0.75f);
    EXPECT_FLOAT_EQ(unsignedPair.ToVector2().X, unsignedPair.ToVector4().X);
    EXPECT_FLOAT_EQ(unsignedPair.ToVector2().Y, unsignedPair.ToVector4().Y);

    const Short2 integers(11.0f, -22.0f);
    EXPECT_FLOAT_EQ(integers.ToVector2().X, integers.ToVector4().X);
    EXPECT_FLOAT_EQ(integers.ToVector2().Y, integers.ToVector4().Y);
}

// =============================================================================
// IPackedVector::ToVector4 is now part of the interface
// =============================================================================

TEST(PackedVectorValueContractTest, ToVector4DispatchesThroughTheNonGenericInterface)
{
    const Alpha8 alpha(1.0f);
    const Bgr565 colour(1.0f, 0.0f, 0.0f);
    const Rgba64 wide(0.25f, 0.5f, 0.75f, 1.0f);
    const Color packedColour(255, 0, 0, 255);

    const IPackedVector* implementations[] = {&alpha, &colour, &wide, &packedColour};
    const Vector4 expected[] = {alpha.ToVector4(), colour.ToVector4(), wide.ToVector4(),
                                packedColour.ToVector4()};

    for (std::size_t index = 0; index < std::size(implementations); ++index)
    {
        const Vector4 through = implementations[index]->ToVector4();
        EXPECT_FLOAT_EQ(through.X, expected[index].X) << "index " << index;
        EXPECT_FLOAT_EQ(through.Y, expected[index].Y) << "index " << index;
        EXPECT_FLOAT_EQ(through.Z, expected[index].Z) << "index " << index;
        EXPECT_FLOAT_EQ(through.W, expected[index].W) << "index " << index;
    }
}
