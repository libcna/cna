// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"

using namespace Microsoft::Xna::Framework;

namespace
{
    float Tq072VectorFloat(std::uint32_t word) { return std::bit_cast<float>(word); }
    void Tq072VectorBits(float actual, std::uint32_t expected)
    {
        EXPECT_EQ(std::bit_cast<std::uint32_t>(actual), expected);
    }
    template<class V> void Tq072Components(const V& actual, std::uint32_t expected)
    {
        Tq072VectorBits(actual.X, expected);
        Tq072VectorBits(actual.Y, expected);
        if constexpr (requires { actual.Z; }) Tq072VectorBits(actual.Z, expected);
        if constexpr (requires { actual.W; }) Tq072VectorBits(actual.W, expected);
    }
    template<class V> class Tq072VectorArithmeticTest : public testing::Test {};
    using Tq072Vectors = testing::Types<Vector2, Vector3, Vector4>;
    TYPED_TEST_SUITE(Tq072VectorArithmeticTest, Tq072Vectors);
}

TYPED_TEST(Tq072VectorArithmeticTest, CatmullRomUsesItsOwnSinglePowerStores)
{
    // Vector2/3/4.CatmullRom IL_0000-000a has the same Single power stores
    // as scalar Refresh. These fixtures extend TQ-044's exactly dyadic t=1/2 coverage.
    using V = TypeParam;
    const float t = Tq072VectorFloat(0x3f2d0798u);
    Tq072Components(V::CatmullRom(V(0), V(0), V(0), V(2), t), 0xbe179d9cu);
    V out(99);
    V::CatmullRom(V(0), V(0), V(0), V(2), t, out);
    Tq072Components(out, 0xbe179d9cu);
}

TYPED_TEST(Tq072VectorArithmeticTest, HermiteRoundsTheCompleteBasisBeforeComponentProducts)
{
    // Vector2/3/4.Hermite IL_0000-0043: power and basis stores match scalar Refresh.
    using V = TypeParam;
    for (const auto c : {std::array<std::uint32_t, 2>{0x3f60a069u, 0x3d2979e0u},
                         std::array<std::uint32_t, 2>{0x3e603d61u, 0x3f608c43u}})
    {
        SCOPED_TRACE(c[0]);
        Tq072Components(V::Hermite(V(1), V(0), V(0), V(0), Tq072VectorFloat(c[0])), c[1]);
        V out(99);
        V::Hermite(V(1), V(0), V(0), V(0), Tq072VectorFloat(c[0]), out);
        Tq072Components(out, c[1]);
    }
}

TYPED_TEST(Tq072VectorArithmeticTest, SmoothStepStoresTheCompleteCubicWeight)
{
    // Vector2/3/4.SmoothStep IL_0021-0032: no intermediate product stores.
    using V = TypeParam;
    const float t = Tq072VectorFloat(0x3d4b305bu);
    Tq072Components(V::SmoothStep(V(0), V(1), t), 0x3be9e88au);
    V out(99);
    V::SmoothStep(V(0), V(1), t, out);
    Tq072Components(out, 0x3be9e88au);
}
