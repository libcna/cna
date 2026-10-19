// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include <any>
#include <bit>
#include <cstdint>
#include <unordered_set>
#include "Microsoft/Xna/Framework/CurveKey.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"

using namespace Microsoft::Xna::Framework;

namespace
{
    template<class T>
    void ExpectHashEquivalent(const T& a, const T& b)
    {
        EXPECT_EQ(a, b);
        EXPECT_TRUE(a.Equals(b));
        EXPECT_TRUE(a.Equals(std::any{b}));
        EXPECT_EQ(a.GetHashCode(), b.GetHashCode());
        const auto hash = [](const T& v) { return static_cast<std::size_t>(v.GetHashCode()); };
        std::unordered_set<T, decltype(hash)> values;
        values.insert(a);
        EXPECT_TRUE(values.contains(b));
        EXPECT_FALSE(values.insert(b).second);
    }

    float Zero(unsigned mask, unsigned bit) { return (mask & bit) ? -0.0f : 0.0f; }
}

TEST(MathValueHashTest, Vector2CanonicalizesEveryZeroSign)
{
    for (unsigned mask = 0; mask < 4; ++mask)
        ExpectHashEquivalent(Vector2::Zero, Vector2(Zero(mask, 1), Zero(mask, 2)));
    ExpectHashEquivalent(Vector2::UnitX, Vector2(1, -0.0f));
    EXPECT_EQ(Vector2(1, -0.0f).GetHashCode(), 1065353216);
}

TEST(MathValueHashTest, Vector3CanonicalizesEveryZeroSign)
{
    for (unsigned mask = 0; mask < 8; ++mask)
        ExpectHashEquivalent(Vector3::Zero, Vector3(Zero(mask, 1), Zero(mask, 2), Zero(mask, 4)));
    ExpectHashEquivalent(Vector3::UnitX, Vector3(1, -0.0f, 0));
    EXPECT_EQ(Vector3(1, -0.0f, 0).GetHashCode(), 1065353216);
}

TEST(MathValueHashTest, Vector4CanonicalizesEveryZeroSign)
{
    for (unsigned mask = 0; mask < 16; ++mask)
        ExpectHashEquivalent(Vector4::Zero, Vector4(Zero(mask, 1), Zero(mask, 2), Zero(mask, 4), Zero(mask, 8)));
    ExpectHashEquivalent(Vector4::UnitX, Vector4(1, -0.0f, 0, 0));
    EXPECT_EQ(Vector4(1, -0.0f, 0, 0).GetHashCode(), 1065353216);
}

TEST(MathValueHashTest, QuaternionCanonicalizesEveryZeroSign)
{
    for (unsigned mask = 0; mask < 16; ++mask)
        ExpectHashEquivalent(Quaternion(), Quaternion(Zero(mask, 1), Zero(mask, 2), Zero(mask, 4), Zero(mask, 8)));
    ExpectHashEquivalent(Quaternion::Identity, Quaternion(-0.0f, 0, 0, 1));
    EXPECT_EQ(Quaternion(-0.0f, 0, 0, 1).GetHashCode(), 1065353216);
}

TEST(MathValueHashTest, MatrixCanonicalizesZeroInEveryField)
{
    constexpr float Matrix::* fields[] = {
        &Matrix::M11, &Matrix::M12, &Matrix::M13, &Matrix::M14,
        &Matrix::M21, &Matrix::M22, &Matrix::M23, &Matrix::M24,
        &Matrix::M31, &Matrix::M32, &Matrix::M33, &Matrix::M34,
        &Matrix::M41, &Matrix::M42, &Matrix::M43, &Matrix::M44
    };
    for (auto field : fields)
    {
        Matrix changed;
        changed.*field = -0.0f;
        ExpectHashEquivalent(Matrix(), changed);
    }
    Matrix changed = Matrix::getIdentityProperty();
    changed.M12 = -0.0f;
    ExpectHashEquivalent(Matrix::getIdentityProperty(), changed);
    EXPECT_EQ(changed.GetHashCode(), -33554432);
}

TEST(MathValueHashTest, CurveKeyCanonicalizesEveryZeroField)
{
    for (unsigned mask = 0; mask < 16; ++mask)
        ExpectHashEquivalent(CurveKey(0, 0, 0, 0, CurveContinuity::Step),
            CurveKey(Zero(mask, 1), Zero(mask, 2), Zero(mask, 4), Zero(mask, 8), CurveContinuity::Step));
    ExpectHashEquivalent(CurveKey(0, 1, 2, 3), CurveKey(-0.0f, 1, 2, 3));
    EXPECT_EQ(CurveKey(-0.0f, 1, 2, 3).GetHashCode(), static_cast<std::size_t>(0xbfc00000u));
}

TEST(MathValueHashTest, CurveKeyCombinesFieldsWithWrappedAddition)
{
    for (const auto continuity : {CurveContinuity::Smooth, CurveContinuity::Step})
    {
        const CurveKey key(-1, -2, -3, -4, continuity);
        const std::uint32_t expected = std::bit_cast<std::uint32_t>(-1.0f) +
            std::bit_cast<std::uint32_t>(-2.0f) + std::bit_cast<std::uint32_t>(-3.0f) +
            std::bit_cast<std::uint32_t>(-4.0f) + static_cast<std::uint32_t>(continuity);
        EXPECT_EQ(key.GetHashCode(), static_cast<std::size_t>(expected));
    }
}

TEST(MathValueHashTest, NonzeroBitsRetainNet4HashIncludingNanPayloads)
{
    // Modern System.Single canonicalizes NaNs; XNA's .NET 4 contract retains them.
    for (const std::uint32_t bits : {0x3f800000u, 0xbf800000u, 0x7f800000u,
                                    0xff800000u, 0x7fc00001u, 0xffc12345u})
    {
        const float value = std::bit_cast<float>(bits);
        EXPECT_EQ(static_cast<std::uint32_t>(Vector2(value, 0).GetHashCode()), bits);
        EXPECT_EQ(static_cast<std::uint32_t>(Vector3(value, 0, 0).GetHashCode()), bits);
        EXPECT_EQ(static_cast<std::uint32_t>(Vector4(value, 0, 0, 0).GetHashCode()), bits);
        EXPECT_EQ(static_cast<std::uint32_t>(Quaternion(value, 0, 0, 0).GetHashCode()), bits);
        EXPECT_EQ(static_cast<std::uint32_t>(CurveKey(value, 0).GetHashCode()), bits);
        Matrix matrix; matrix.M11 = value;
        EXPECT_EQ(static_cast<std::uint32_t>(matrix.GetHashCode()), bits);
    }
}
