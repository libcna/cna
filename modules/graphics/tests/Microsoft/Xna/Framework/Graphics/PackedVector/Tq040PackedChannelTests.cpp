// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <any>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <type_traits>
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
    enum class Encoding { UNorm, Unsigned, SNorm, Signed, Half };
    struct Channel { int component; unsigned width; unsigned shift; };
    struct Sample { float input; std::uint64_t code; float expanded; };
    template<class T> T Scalar(const Vector4& v)
    {
        if constexpr (std::is_same_v<T, Alpha8>) return T(v.W);
        else if constexpr (std::is_same_v<T, HalfSingle>) return T(v.X);
        else if constexpr (std::is_same_v<T, Bgr565>) return T(v.X, v.Y, v.Z);
        else if constexpr (std::is_constructible_v<T, float, float, float, float>)
            return T(v.X, v.Y, v.Z, v.W);
        else return T(v.X, v.Y);
    }
    template<class T> T Natural(const Vector4& v)
    {
        if constexpr (std::is_same_v<T, Alpha8>) return T(v.W);
        else if constexpr (std::is_same_v<T, HalfSingle>) return T(v.X);
        else if constexpr (std::is_same_v<T, Bgr565>) return T(Vector3(v.X,v.Y,v.Z));
        else if constexpr (std::is_constructible_v<T, Vector4>) return T(v);
        else return T(Vector2(v.X,v.Y));
    }
    void Bits(const Vector4& actual, const std::array<float,4>& expected)
    {
        const float channels[] = {actual.X, actual.Y, actual.Z, actual.W};
        for (int i=0; i<4; ++i)
            EXPECT_EQ(std::bit_cast<std::uint32_t>(channels[i]),
                      std::bit_cast<std::uint32_t>(expected[i])) << "expanded component " << i;
    }
    template<class T>
    void Check(const Channel& ch, const Sample& s, const std::array<float,4>& padding)
    {
        // One active field, others zero: literal code << the XNA PackHelper field shift.
        std::array<float,4> components{};
        components[ch.component] = s.input;
        const Vector4 input(components[0],components[1],components[2],components[3]);
        const auto word = s.code << ch.shift;
        EXPECT_EQ(Scalar<T>(input).getPackedValueProperty(), word);
        EXPECT_EQ(Natural<T>(input).getPackedValueProperty(), word);
        T value;
        using Storage = decltype(value.getPackedValueProperty());
        IPackedVectorT<Storage>& generic = value;
        generic.setPackedValueProperty(static_cast<Storage>(~Storage{0}));
        IPackedVector& hook = value;
        hook.PackFromVector4(input);
        EXPECT_EQ(generic.getPackedValueProperty(), word); // Also proves replacement, not OR.
        std::array<float,4> expected = padding;
        expected[ch.component] = s.expanded;
        Bits(hook.ToVector4(), expected);
        // Independently inject the word so a pack and unpack defect cannot cancel out.
        generic.setPackedValueProperty(static_cast<Storage>(word));
        Bits(hook.ToVector4(), expected);
        if constexpr (std::is_same_v<T, Alpha8>)
            EXPECT_EQ(std::bit_cast<std::uint32_t>(value.ToAlpha()), std::bit_cast<std::uint32_t>(s.expanded));
        else if constexpr (std::is_same_v<T, HalfSingle>)
            EXPECT_EQ(std::bit_cast<std::uint32_t>(value.ToSingle()), std::bit_cast<std::uint32_t>(s.expanded));
        else if constexpr (std::is_same_v<T, Bgr565>)
        {
            const auto v = value.ToVector3();
            Bits(Vector4(v.X,v.Y,v.Z,1), expected);
        }
        else if constexpr (std::is_constructible_v<T, Vector2>)
        {
            const auto v = value.ToVector2();
            Bits(Vector4(v.X,v.Y,0,1), expected);
        }
        const auto v = hook.ToVector4();
        hook.PackFromVector4(v);
        EXPECT_EQ(generic.getPackedValueProperty(), word);
    }
    template<class T, std::size_t N>
    void Boundaries(Encoding kind, const Channel (&fields)[N], const std::array<float,4>& padding)
    {
        const float inf = std::numeric_limits<float>::infinity();
        const float nan = std::bit_cast<float>(std::uint32_t{0x7fc00000});
        for (const auto& ch : fields)
        {
            SCOPED_TRACE(ch.component);
            const std::uint64_t mask = (std::uint64_t{1} << ch.width)-1;
            const auto run = [&](float input, std::uint64_t code, float expanded) {
                Check<T>(ch, {input,code,expanded}, padding);
            };
            if (kind == Encoding::Half)
            {
                // HalfUtils.Pack IL_002e..008e, Unpack IL_0000..008a.
                // Each tuple is {binary32 input bits, half code, binary32 expansion bits}.
                const std::uint32_t cases[][3] = {
                    {0x00000000,0x0000,0x00000000}, {0x80000000,0x8000,0x80000000},
                    {0x33000000,0x0000,0x00000000}, {0x33800000,0x0001,0x33800000},
                    {0x387fc000,0x03ff,0x387fc000}, {0x38800000,0x0400,0x38800000},
                    {0x3f800fff,0x3c00,0x3f800000}, {0x3f801000,0x3c00,0x3f800000},
                    {0x3f801001,0x3c01,0x3f802000}, {0xbf803000,0xbc02,0xbf804000},
                    {0x477fe000,0x7bff,0x477fe000}, {0x477ff000,0x7c00,0x47800000},
                    {0x47ffefff,0x7fff,0x47ffe000}, {0x47fff000,0x7fff,0x47ffe000},
                    {0x7f800000,0x7fff,0x47ffe000}, {0xff800000,0xffff,0xc7ffe000},
                    {0x7fc00000,0x7fff,0x47ffe000}, {0xffc00000,0xffff,0xc7ffe000}
                };
                for (const auto& c: cases)
                    run(std::bit_cast<float>(c[0]),c[1],std::bit_cast<float>(c[2]));
                continue;
            }
            // PackUtils.ClampAndRound IL_0000..003d: NaN=>0, +/-inf=>bounds;
            // System.Math.Round(double) resolves exact midpoints to the even integer.
            run(-0.0f,0,0); run(nan,0,0);
            if (kind == Encoding::UNorm || kind == Encoding::Unsigned)
            {
                const float scale = kind == Encoding::UNorm ? float(mask) : 1.0f;
                const float high = float(mask)/scale;
                run(-inf,0,0); run(-2.0f,0,0);
                run(high,mask,high); run(inf,mask,high);
                run(std::numeric_limits<float>::max(),mask,high);
                const float tie = kind == Encoding::UNorm ? 0.5f : 2.5f;
                const std::uint64_t lower = kind == Encoding::UNorm ? mask/2 : 2;
                const std::uint64_t even = lower + (lower & 1);
                run(std::nextafter(tie,-inf),lower,float(lower)/scale);
                run(tie,even,float(even)/scale);
                run(std::nextafter(tie,inf),lower+1,float(lower+1)/scale);
            }
            else
            {
                const auto positive = mask>>1;
                const float scale = kind == Encoding::SNorm ? float(positive) : 1.0f;
                const auto negativeCode = kind == Encoding::SNorm ? positive+2 : positive+1;
                const float low = kind == Encoding::SNorm ? -1.0f : -32768.0f;
                const float high = kind == Encoding::SNorm ? 1.0f : 32767.0f;
                run(low,negativeCode,low); run(-inf,negativeCode,low);
                run(-std::numeric_limits<float>::max(),negativeCode,low);
                run(high,positive,high); run(inf,positive,high);
                run(std::numeric_limits<float>::max(),positive,high);
                const float tie = kind == Encoding::SNorm ? -0.5f : -2.5f;
                const auto magnitude = kind == Encoding::SNorm ? (positive+1)/2 : 2;
                run(tie, (mask+1-magnitude)&mask, -float(magnitude)/scale);
                const auto upperMagnitude = kind == Encoding::SNorm ? magnitude-1 : 2;
                const auto lowerMagnitude = kind == Encoding::SNorm ? magnitude : 3;
                run(std::nextafter(tie,-inf),(mask+1-lowerMagnitude)&mask,-float(lowerMagnitude)/scale);
                run(std::nextafter(tie,inf),(mask+1-upperMagnitude)&mask,-float(upperMagnitude)/scale);
            }
        }
    }
}

// XNA Microsoft.Xna.Framework.dll: Alpha8::PackHelper IL_0000..IL_000c; Alpha8::Microsoft.Xna.Framework.Graphics.PackedVector.IPackedVector.ToVector4 IL_0000..IL_001c.
TEST(Tq040PackedChannels, Alpha8EveryChannelBoundary)
{
    const Channel fields[] = {{3,8,0}};
    Boundaries<Alpha8>(Encoding::UNorm,fields,{0,0,0,0});
}

// XNA Microsoft.Xna.Framework.dll: Bgr565::PackHelper IL_0000..IL_002f; Bgr565::ToVector3 IL_0000..IL_0042; Bgr565::Microsoft.Xna.Framework.Graphics.PackedVector.IPackedVector.ToVector4 IL_0000..IL_0026.
TEST(Tq040PackedChannels, Bgr565EveryChannelBoundary)
{
    const Channel fields[] = {{0,5,11}, {1,6,5}, {2,5,0}};
    Boundaries<Bgr565>(Encoding::UNorm,fields,{0,0,0,1});
}

// XNA Microsoft.Xna.Framework.dll: Bgra4444::PackHelper IL_0000..IL_003f; Bgra4444::ToVector4 IL_0000..IL_0058.
TEST(Tq040PackedChannels, Bgra4444EveryChannelBoundary)
{
    const Channel fields[] = {{0,4,8}, {1,4,4}, {2,4,0}, {3,4,12}};
    Boundaries<Bgra4444>(Encoding::UNorm,fields,{0,0,0,0});
}

// XNA Microsoft.Xna.Framework.dll: Bgra5551::PackHelper IL_0000..IL_0040; Bgra5551::ToVector4 IL_0000..IL_0058.
TEST(Tq040PackedChannels, Bgra5551EveryChannelBoundary)
{
    const Channel fields[] = {{0,5,10}, {1,5,5}, {2,5,0}, {3,1,15}};
    Boundaries<Bgra5551>(Encoding::UNorm,fields,{0,0,0,0});
}

// XNA Microsoft.Xna.Framework.dll: Byte4::PackHelper IL_0000..IL_003f; Byte4::ToVector4 IL_0000..IL_005d.
TEST(Tq040PackedChannels, Byte4EveryChannelBoundary)
{
    const Channel fields[] = {{0,8,0}, {1,8,8}, {2,8,16}, {3,8,24}};
    Boundaries<Byte4>(Encoding::Unsigned,fields,{0,0,0,0});
}

// XNA Microsoft.Xna.Framework.dll: HalfSingle::Microsoft.Xna.Framework.Graphics.PackedVector.IPackedVector.PackFromVector4 IL_0000..IL_0012; HalfSingle::ToSingle IL_0000..IL_000b.
TEST(Tq040PackedChannels, HalfSingleEveryChannelBoundary)
{
    const Channel fields[] = {{0,16,0}};
    Boundaries<HalfSingle>(Encoding::Half,fields,{0,0,0,1});
}

// XNA Microsoft.Xna.Framework.dll: HalfVector2::PackHelper IL_0000..IL_0014; HalfVector2::ToVector2 IL_0000..IL_002a; HalfVector2::Microsoft.Xna.Framework.Graphics.PackedVector.IPackedVector.ToVector4 IL_0000..IL_0024.
TEST(Tq040PackedChannels, HalfVector2EveryChannelBoundary)
{
    const Channel fields[] = {{0,16,0}, {1,16,16}};
    Boundaries<HalfVector2>(Encoding::Half,fields,{0,0,0,1});
}

// XNA Microsoft.Xna.Framework.dll: HalfVector4::PackHelper IL_0000..IL_0030; HalfVector4::ToVector4 IL_0000..IL_0056.
TEST(Tq040PackedChannels, HalfVector4EveryChannelBoundary)
{
    const Channel fields[] = {{0,16,0}, {1,16,16}, {2,16,32}, {3,16,48}};
    Boundaries<HalfVector4>(Encoding::Half,fields,{0,0,0,0});
}

// XNA Microsoft.Xna.Framework.dll: NormalizedByte2::PackHelper IL_0000..IL_001e; NormalizedByte2::ToVector2 IL_0000..IL_0031; NormalizedByte2::Microsoft.Xna.Framework.Graphics.PackedVector.IPackedVector.ToVector4 IL_0000..IL_0024.
TEST(Tq040PackedChannels, NormalizedByte2EveryChannelBoundary)
{
    const Channel fields[] = {{0,8,0}, {1,8,8}};
    Boundaries<NormalizedByte2>(Encoding::SNorm,fields,{0,0,0,1});
}

// XNA Microsoft.Xna.Framework.dll: NormalizedByte4::PackHelper IL_0000..IL_003f; NormalizedByte4::ToVector4 IL_0000..IL_0065.
TEST(Tq040PackedChannels, NormalizedByte4EveryChannelBoundary)
{
    const Channel fields[] = {{0,8,0}, {1,8,8}, {2,8,16}, {3,8,24}};
    Boundaries<NormalizedByte4>(Encoding::SNorm,fields,{0,0,0,0});
}

// XNA Microsoft.Xna.Framework.dll: NormalizedShort2::PackHelper IL_0000..IL_001e; NormalizedShort2::ToVector2 IL_0000..IL_0032; NormalizedShort2::Microsoft.Xna.Framework.Graphics.PackedVector.IPackedVector.ToVector4 IL_0000..IL_0024.
TEST(Tq040PackedChannels, NormalizedShort2EveryChannelBoundary)
{
    const Channel fields[] = {{0,16,0}, {1,16,16}};
    Boundaries<NormalizedShort2>(Encoding::SNorm,fields,{0,0,0,1});
}

// XNA Microsoft.Xna.Framework.dll: NormalizedShort4::PackHelper IL_0000..IL_0044; NormalizedShort4::ToVector4 IL_0000..IL_006a.
TEST(Tq040PackedChannels, NormalizedShort4EveryChannelBoundary)
{
    const Channel fields[] = {{0,16,0}, {1,16,16}, {2,16,32}, {3,16,48}};
    Boundaries<NormalizedShort4>(Encoding::SNorm,fields,{0,0,0,0});
}

// XNA Microsoft.Xna.Framework.dll: Rg32::PackHelper IL_0000..IL_001e; Rg32::ToVector2 IL_0000..IL_0032; Rg32::Microsoft.Xna.Framework.Graphics.PackedVector.IPackedVector.ToVector4 IL_0000..IL_0024.
TEST(Tq040PackedChannels, Rg32EveryChannelBoundary)
{
    const Channel fields[] = {{0,16,0}, {1,16,16}};
    Boundaries<Rg32>(Encoding::UNorm,fields,{0,0,0,1});
}

// XNA Microsoft.Xna.Framework.dll: Rgba1010102::PackHelper IL_0000..IL_0040; Rgba1010102::ToVector4 IL_0000..IL_0062.
TEST(Tq040PackedChannels, Rgba1010102EveryChannelBoundary)
{
    const Channel fields[] = {{0,10,0}, {1,10,10}, {2,10,20}, {3,2,30}};
    Boundaries<Rgba1010102>(Encoding::UNorm,fields,{0,0,0,0});
}

// XNA Microsoft.Xna.Framework.dll: Rgba64::PackHelper IL_0000..IL_0044; Rgba64::ToVector4 IL_0000..IL_006a.
TEST(Tq040PackedChannels, Rgba64EveryChannelBoundary)
{
    const Channel fields[] = {{0,16,0}, {1,16,16}, {2,16,32}, {3,16,48}};
    Boundaries<Rgba64>(Encoding::UNorm,fields,{0,0,0,0});
}

// XNA Microsoft.Xna.Framework.dll: Short2::PackHelper IL_0000..IL_001e; Short2::ToVector2 IL_0000..IL_0022; Short2::Microsoft.Xna.Framework.Graphics.PackedVector.IPackedVector.ToVector4 IL_0000..IL_0024.
TEST(Tq040PackedChannels, Short2EveryChannelBoundary)
{
    const Channel fields[] = {{0,16,0}, {1,16,16}};
    Boundaries<Short2>(Encoding::Signed,fields,{0,0,0,1});
}

// XNA Microsoft.Xna.Framework.dll: Short4::PackHelper IL_0000..IL_0044; Short4::ToVector4 IL_0000..IL_0046.
TEST(Tq040PackedChannels, Short4EveryChannelBoundary)
{
    const Channel fields[] = {{0,16,0}, {1,16,16}, {2,16,32}, {3,16,48}};
    Boundaries<Short4>(Encoding::Signed,fields,{0,0,0,0});
}

namespace
{
    template<class T, unsigned Width, unsigned Count> void MinimumCodes()
    {
        // PackUtils.UnpackSNorm IL_000b..0016: raw sign-only code -> -1.
        // PackSNorm IL_0000..0017: -1 -> -max, not the unused most-negative code.
        for (unsigned i=0;i<Count;++i)
        {
            T value;
            using Storage=decltype(value.getPackedValueProperty());
            const auto raw = (std::uint64_t{1} << (Width-1)) << (Width*i);
            value.setPackedValueProperty(static_cast<Storage>(raw));
            std::array<float,4> expected{0,0,0,Count==2?1.0f:0.0f};
            expected[i]=-1;
            Bits(value.ToVector4(),expected);
            value.PackFromVector4(value.ToVector4());
            EXPECT_EQ(value.getPackedValueProperty(),raw | (std::uint64_t{1} << (Width*i)));
        }
    }
    template<class T> void EveryBitChangesEquality(unsigned width)
    {
        // XNA typed Equals IL_0000..0012 compares storage; operators call Equals,
        // IL_0000..0008 (==), IL_0000..000b (!=), including the sign bit of half zero.
        const T zero;
        for (unsigned bit=0;bit<width;++bit)
        {
            T changed;
            changed.setPackedValueProperty(static_cast<decltype(changed.getPackedValueProperty())>(std::uint64_t{1}<<bit));
            EXPECT_FALSE(zero == changed); EXPECT_TRUE(zero != changed);
            EXPECT_FALSE(changed == zero); EXPECT_TRUE(changed != zero);
            EXPECT_TRUE(changed == changed); EXPECT_FALSE(changed != changed);
            // XNA GetHashCode IL_0000..000b delegates to the unsigned storage hash.
            // UInt64 folds low XOR high; the other widths retain the low 32 bits.
            const std::uint64_t raw = std::uint64_t{1} << bit;
            const std::uint32_t hash = static_cast<std::uint32_t>(raw) ^
                (width == 64 ? static_cast<std::uint32_t>(raw >> 32) : 0u);
            EXPECT_EQ(changed.GetHashCode(), std::bit_cast<std::int32_t>(hash));
            EXPECT_FALSE(zero.Equals(changed)); EXPECT_FALSE(zero.Equals(std::any(changed)));
        }
    }
}
TEST(Tq040PackedChannels, SignedNormalizedUnusedMinimumCanonicalizesEveryField)
{
    MinimumCodes<NormalizedByte2,8,2>(); MinimumCodes<NormalizedByte4,8,4>();
    MinimumCodes<NormalizedShort2,16,2>(); MinimumCodes<NormalizedShort4,16,4>();
}
TEST(Tq040PackedChannels, EveryStorageBitParticipatesInEquality)
{
    EveryBitChangesEquality<Alpha8>(8);
    EveryBitChangesEquality<Bgr565>(16);
    EveryBitChangesEquality<Bgra4444>(16);
    EveryBitChangesEquality<Bgra5551>(16);
    EveryBitChangesEquality<Byte4>(32);
    EveryBitChangesEquality<HalfSingle>(16);
    EveryBitChangesEquality<HalfVector2>(32);
    EveryBitChangesEquality<HalfVector4>(64);
    EveryBitChangesEquality<NormalizedByte2>(16);
    EveryBitChangesEquality<NormalizedByte4>(32);
    EveryBitChangesEquality<NormalizedShort2>(32);
    EveryBitChangesEquality<NormalizedShort4>(64);
    EveryBitChangesEquality<Rg32>(32);
    EveryBitChangesEquality<Rgba1010102>(32);
    EveryBitChangesEquality<Rgba64>(64);
    EveryBitChangesEquality<Short2>(32);
    EveryBitChangesEquality<Short4>(64);
}
