// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
using namespace Microsoft::Xna::Framework;

TEST(Tq044MathHashTest, Vector2EveryFieldContributes)
{
    // XNA GetHashCode sums Single hash words modulo 2^32. Single(1) word=0x3f800000.
    // Isolate every field so omission or duplication cannot hide in a combined total.
    constexpr float Vector2::* fields[] = {&Vector2::X,&Vector2::Y};
    for (const auto f : fields) {
        Vector2 input;
        input.*f=1;
        EXPECT_EQ(input.GetHashCode(),1065353216);
    }
}

TEST(Tq044MathHashTest, Vector3EveryFieldContributes)
{
    // XNA GetHashCode sums Single hash words modulo 2^32. Single(1) word=0x3f800000.
    // Isolate every field so omission or duplication cannot hide in a combined total.
    constexpr float Vector3::* fields[] = {&Vector3::X,&Vector3::Y,&Vector3::Z};
    for (const auto f : fields) {
        Vector3 input;
        input.*f=1;
        EXPECT_EQ(input.GetHashCode(),1065353216);
    }
}

TEST(Tq044MathHashTest, Vector4EveryFieldContributes)
{
    // XNA GetHashCode sums Single hash words modulo 2^32. Single(1) word=0x3f800000.
    // Isolate every field so omission or duplication cannot hide in a combined total.
    constexpr float Vector4::* fields[] = {&Vector4::X,&Vector4::Y,&Vector4::Z,&Vector4::W};
    for (const auto f : fields) {
        Vector4 input;
        input.*f=1;
        EXPECT_EQ(input.GetHashCode(),1065353216);
    }
}

TEST(Tq044MathHashTest, QuaternionEveryFieldContributes)
{
    // XNA GetHashCode sums Single hash words modulo 2^32. Single(1) word=0x3f800000.
    // Isolate every field so omission or duplication cannot hide in a combined total.
    constexpr float Quaternion::* fields[] = {&Quaternion::X,&Quaternion::Y,&Quaternion::Z,&Quaternion::W};
    for (const auto f : fields) {
        Quaternion input;
        input.*f=1;
        EXPECT_EQ(input.GetHashCode(),1065353216);
    }
}

TEST(Tq044MathHashTest, MatrixEveryFieldContributes)
{
    // XNA GetHashCode sums Single hash words modulo 2^32. Single(1) word=0x3f800000.
    // Isolate every field so omission or duplication cannot hide in a combined total.
    constexpr float Matrix::* fields[] = {&Matrix::M11,&Matrix::M12,&Matrix::M13,&Matrix::M14,&Matrix::M21,&Matrix::M22,&Matrix::M23,&Matrix::M24,&Matrix::M31,&Matrix::M32,&Matrix::M33,&Matrix::M34,&Matrix::M41,&Matrix::M42,&Matrix::M43,&Matrix::M44};
    for (const auto f : fields) {
        Matrix input;
        input.*f=1;
        EXPECT_EQ(input.GetHashCode(),1065353216);
    }
}
