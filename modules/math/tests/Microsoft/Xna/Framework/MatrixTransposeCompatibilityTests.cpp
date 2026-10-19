// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include <array>
#include <bit>
#include <cstdint>
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Plane.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

using namespace Microsoft::Xna::Framework;

namespace
{
    constexpr std::array<float Matrix::*, 16> fields = {
        &Matrix::M11,&Matrix::M12,&Matrix::M13,&Matrix::M14,
        &Matrix::M21,&Matrix::M22,&Matrix::M23,&Matrix::M24,
        &Matrix::M31,&Matrix::M32,&Matrix::M33,&Matrix::M34,
        &Matrix::M41,&Matrix::M42,&Matrix::M43,&Matrix::M44
    };

    std::array<Matrix, 4> Samples()
    {
        Matrix sequential(1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16);
        Matrix ieee = sequential;
        constexpr std::array<std::uint32_t, 6> bits = {
            0u,0x80000000u,0x7fc00001u,0xffc12345u,0x7f800000u,0xff800000u
        };
        for (std::size_t i = 0; i < bits.size(); ++i) ieee.*fields[i] = std::bit_cast<float>(bits[i]);
        return {sequential, ieee, Matrix::getIdentityProperty(), Matrix()};
    }

    void ExpectTransposeBits(const Matrix& actual, const Matrix& source)
    {
        for (std::size_t row = 0; row < 4; ++row) for (std::size_t column = 0; column < 4; ++column)
            EXPECT_EQ(std::bit_cast<std::uint32_t>(actual.*fields[row*4+column]),
                      std::bit_cast<std::uint32_t>(source.*fields[column*4+row]));
    }

    void ExpectSameBits(const Matrix& actual, const Matrix& expected)
    {
        for (const auto field : fields)
            EXPECT_EQ(std::bit_cast<std::uint32_t>(actual.*field), std::bit_cast<std::uint32_t>(expected.*field));
    }
}

TEST(MatrixTransposeCompatibilityTest, ValueAndSeparateOutputPreserveBitsAndSource)
{
    for (const auto& original : Samples())
    {
        auto source = original;
        ExpectTransposeBits(Matrix::Transpose(source), original);
        Matrix result;
        Matrix::Transpose(source, result);
        ExpectTransposeBits(result, original);
        ExpectSameBits(source, original);
    }
}

TEST(MatrixTransposeCompatibilityTest, AliasedOutputPreservesBitsAndIsAnInvolution)
{
    for (const auto& original : Samples())
    {
        auto aliased = original;
        Matrix::Transpose(aliased, aliased);
        ExpectTransposeBits(aliased, original);
        Matrix::Transpose(aliased, aliased);
        ExpectSameBits(aliased, original);
    }
}

TEST(MatrixTransposeCompatibilityTest, PlaneTransformRetainsTranslatedNonzeroOffset)
{
    const auto matrix = Matrix::CreateTranslation(0,5,0);
    const Plane source(Vector3::Up,-2), expected(Vector3::Up,-7);
    EXPECT_EQ(Plane::Transform(source,matrix), expected);
    Plane output;
    Plane::Transform(source,matrix,output);
    EXPECT_EQ(output, expected);
    auto aliased = source;
    Plane::Transform(aliased,matrix,aliased);
    EXPECT_EQ(aliased, expected);
    EXPECT_EQ(source, Plane(Vector3::Up,-2));
    EXPECT_EQ(matrix, Matrix::CreateTranslation(0,5,0));
}
