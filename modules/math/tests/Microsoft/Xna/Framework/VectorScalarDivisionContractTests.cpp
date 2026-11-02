// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"

using namespace Microsoft::Xna::Framework;

namespace
{
    template<class V> V Components(float x, float y, float z, float w)
    {
        V value;
        value.X = x;
        value.Y = y;
        if constexpr (requires { value.Z; }) value.Z = z;
        if constexpr (requires { value.W; }) value.W = w;
        return value;
    }

    template<class V> struct Case
    {
        V value;
        float divisor;
        std::array<std::uint32_t, 4> expected;
    };

    template<class V> std::vector<Case<V>> Cases()
    {
        const float tiny = std::numeric_limits<float>::denorm_min();
        const float infinity = std::numeric_limits<float>::infinity();
        // Single 1/3 is 0x3eaaaaab; multiplying 7 and 10 rounds one ULP above direct division.
        // 0x7fc00000 below denotes any NaN: XNA does not promise a payload/sign for 0 * infinity.
        return {{Components<V>(7, -7, 10, -10), 3,
                    {0x40155556u, 0xc0155556u, 0x40555556u, 0xc0555556u}},
                {Components<V>(tiny, -tiny, 0.0f, -0.0f), tiny,
                    {0x7f800000u, 0xff800000u, 0x7fc00000u, 0x7fc00000u}},
                {Components<V>(0.0f, -0.0f, 7, -7), -2,
                    {0x80000000u, 0x00000000u, 0xc0600000u, 0x40600000u}},
                {Components<V>(7, -7, 0.0f, -0.0f), infinity,
                    {0x00000000u, 0x80000000u, 0x00000000u, 0x80000000u}},
                {Components<V>(7, -7, 0.0f, -0.0f), -0.0f,
                    {0xff800000u, 0x7f800000u, 0x7fc00000u, 0x7fc00000u}}};
    }

    void ExpectComponent(float value, std::uint32_t expected)
    {
        if (expected == 0x7fc00000u) EXPECT_TRUE(std::isnan(value));
        else EXPECT_EQ(std::bit_cast<std::uint32_t>(value), expected);
    }

    template<class V> void ExpectComponents(const V& value, const std::array<std::uint32_t, 4>& expected)
    {
        ExpectComponent(value.X, expected[0]);
        ExpectComponent(value.Y, expected[1]);
        if constexpr (requires { value.Z; }) ExpectComponent(value.Z, expected[2]);
        if constexpr (requires { value.W; }) ExpectComponent(value.W, expected[3]);
    }

    template<class V> class VectorScalarDivisionContractTest : public testing::Test {};
    using Vectors = testing::Types<Vector2, Vector3, Vector4>;
    TYPED_TEST_SUITE(VectorScalarDivisionContractTest, Vectors);

    TYPED_TEST(VectorScalarDivisionContractTest, ValueFormUsesXnaReciprocalBitsAndClassifications)
    {
        for (const auto& test : Cases<TypeParam>())
        {
            SCOPED_TRACE(test.divisor);
            ExpectComponents(TypeParam::Divide(test.value, test.divisor), test.expected);
        }
    }

    TYPED_TEST(VectorScalarDivisionContractTest, OutFormUsesXnaReciprocalBitsIncludingInputAlias)
    {
        for (const auto& test : Cases<TypeParam>())
        {
            SCOPED_TRACE(test.divisor);
            TypeParam output(99);
            TypeParam::Divide(test.value, test.divisor, output);
            ExpectComponents(output, test.expected);
            auto alias = test.value;
            TypeParam::Divide(alias, test.divisor, alias);
            ExpectComponents(alias, test.expected);
        }
    }

    TYPED_TEST(VectorScalarDivisionContractTest, OperatorsUseXnaReciprocalBitsAndReturnCompoundReference)
    {
        for (const auto& test : Cases<TypeParam>())
        {
            SCOPED_TRACE(test.divisor);
            ExpectComponents(test.value / test.divisor, test.expected);
            if constexpr (requires(TypeParam value) { value /= test.divisor; })
            {
                auto compound = test.value;
                EXPECT_EQ(&(compound /= test.divisor), &compound);
                ExpectComponents(compound, test.expected);
            }
        }
    }
}
