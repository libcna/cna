// SPDX-License-Identifier: MS-PL
//
// plans/plan_metal.md METAL-34-style extraction: MetalMat4Multiply/FromXna/Transpose are plain float
// arithmetic plus Microsoft::Xna::Framework::Matrix (itself plain C++, zero Objective-C/Metal-
// framework dependency), so they compile and run on any platform CnaTests already builds for -- no
// #if defined(CNA_RENDERER_METAL) gate.
//
// This is the exact helper set drawMetal3D() uses to build every 3D draw's WVP matrix
// (transpose(multiply(multiply(fromXna(world),fromXna(view)),fromXna(projection)))), so a real bug
// here would silently distort every single 3D draw the Metal renderer ever issues. The strongest
// available check is a cross-validation against Microsoft::Xna::Framework::Matrix's own real
// Multiply() implementation -- an independent, already-in-production implementation of the same
// mathematical operation -- rather than only re-deriving expected numbers by hand.
#include <gtest/gtest.h>
#include "CNA/Internal/Renderers/Metal/MetalMat4.hpp"
#include <cmath>

using namespace CNA::Internal::Renderers::Metal;
using Microsoft::Xna::Framework::Matrix;

namespace
{
    constexpr float kEps = 1e-4f;

    void ExpectMat4EqualsXnaMatrix(const MetalMat4& got, const Matrix& expected)
    {
        const float expectedM[16] = {
            expected.M11, expected.M12, expected.M13, expected.M14,
            expected.M21, expected.M22, expected.M23, expected.M24,
            expected.M31, expected.M32, expected.M33, expected.M34,
            expected.M41, expected.M42, expected.M43, expected.M44,
        };
        for (int i = 0; i < 16; ++i)
            EXPECT_NEAR(got.m[i], expectedM[i], kEps) << "at index " << i;
    }
}

TEST(MetalMat4, FromXnaCopiesEveryFieldInRowMajorOrder)
{
    // Every field distinct so a transposition/index-swap bug in FromXna would be caught -- e.g.
    // M12 and M21 (both hold different values here) landing in each other's slot.
    Matrix x(1,2,3,4, 5,6,7,8, 9,10,11,12, 13,14,15,16);
    MetalMat4 got = MetalMat4FromXna(x);
    const float expected[16] = {1,2,3,4, 5,6,7,8, 9,10,11,12, 13,14,15,16};
    for (int i = 0; i < 16; ++i)
        EXPECT_FLOAT_EQ(got.m[i], expected[i]) << "at index " << i;
}

TEST(MetalMat4, MultiplyByIdentityIsANoOp)
{
    Matrix x = Matrix::CreateTranslation(3, -4, 5) * Matrix::CreateScale(2, 3, 4);
    MetalMat4 mx = MetalMat4FromXna(x);
    MetalMat4 identity = MetalMat4FromXna(Matrix::getIdentityProperty());

    MetalMat4 leftIdentity = MetalMat4Multiply(identity, mx);
    MetalMat4 rightIdentity = MetalMat4Multiply(mx, identity);
    for (int i = 0; i < 16; ++i) {
        EXPECT_NEAR(leftIdentity.m[i], mx.m[i], kEps) << "at index " << i;
        EXPECT_NEAR(rightIdentity.m[i], mx.m[i], kEps) << "at index " << i;
    }
}

TEST(MetalMat4, MultiplyCrossValidatesAgainstXnaMatrixMultiply)
{
    // Translation then scale, and scale then translation -- matrix multiplication is not
    // commutative, so these two orderings genuinely differ, exercising real off-diagonal terms.
    Matrix a = Matrix::CreateTranslation(1, 2, 3);
    Matrix b = Matrix::CreateScale(2, 3, 4);

    MetalMat4 gotAB = MetalMat4Multiply(MetalMat4FromXna(a), MetalMat4FromXna(b));
    ExpectMat4EqualsXnaMatrix(gotAB, Matrix::Multiply(a, b));

    MetalMat4 gotBA = MetalMat4Multiply(MetalMat4FromXna(b), MetalMat4FromXna(a));
    ExpectMat4EqualsXnaMatrix(gotBA, Matrix::Multiply(b, a));

    // Sanity: the two orderings must actually differ (translation column depends on whether scale
    // was applied before or after it), otherwise this test wouldn't be exercising anything real.
    bool anyDiffer = false;
    for (int i = 0; i < 16; ++i)
        if (std::fabs(gotAB.m[i] - gotBA.m[i]) > kEps) anyDiffer = true;
    EXPECT_TRUE(anyDiffer) << "test setup: A*B and B*A should differ for non-commuting transforms";
}

TEST(MetalMat4, MultiplyCrossValidatesForRotationTranslationScaleChain)
{
    // A more realistic WVP-shaped chain: rotation * translation * scale, each a distinct
    // transform kind, cross-validated the same way against Matrix::Multiply.
    Matrix rot = Matrix::CreateRotationY(0.7f);
    Matrix trans = Matrix::CreateTranslation(10, -5, 2);
    Matrix scale = Matrix::CreateScale(1.5f, 0.5f, 2.0f);

    MetalMat4 got = MetalMat4Multiply(MetalMat4Multiply(MetalMat4FromXna(rot), MetalMat4FromXna(trans)), MetalMat4FromXna(scale));
    Matrix expected = Matrix::Multiply(Matrix::Multiply(rot, trans), scale);
    ExpectMat4EqualsXnaMatrix(got, expected);
}

TEST(MetalMat4, TransposeOfIdentityIsIdentity)
{
    MetalMat4 identity = MetalMat4FromXna(Matrix::getIdentityProperty());
    MetalMat4 got = MetalMat4Transpose(identity);
    for (int i = 0; i < 16; ++i)
        EXPECT_FLOAT_EQ(got.m[i], identity.m[i]) << "at index " << i;
}

TEST(MetalMat4, TransposeTwiceReturnsTheOriginal)
{
    Matrix x = Matrix::CreateRotationX(0.3f) * Matrix::CreateTranslation(1, 2, 3);
    MetalMat4 mx = MetalMat4FromXna(x);
    MetalMat4 got = MetalMat4Transpose(MetalMat4Transpose(mx));
    for (int i = 0; i < 16; ++i)
        EXPECT_NEAR(got.m[i], mx.m[i], kEps) << "at index " << i;
}

TEST(MetalMat4, TransposeSwapsOffDiagonalElementsForAnAsymmetricMatrix)
{
    // A translation matrix is a clean, hand-verifiable asymmetric case: row 4 (the translation
    // column, M41/M42/M43) moves to column 4 (M14/M24/M34) under transpose, and vice versa.
    Matrix x = Matrix::CreateTranslation(7, 8, 9);
    MetalMat4 got = MetalMat4Transpose(MetalMat4FromXna(x));
    // Row-major index row*4+col: M14 (row0,col3) should now hold the original M41 (row3,col0) = 7.
    EXPECT_NEAR(got.m[0*4+3], 7.0f, kEps);
    EXPECT_NEAR(got.m[1*4+3], 8.0f, kEps);
    EXPECT_NEAR(got.m[2*4+3], 9.0f, kEps);
    // And the original translation row (M41/M42/M43) becomes zero in the transposed result's own
    // row 3, columns 0-2 (translation matrices have zero off-diagonal upper-left).
    EXPECT_NEAR(got.m[3*4+0], 0.0f, kEps);
    EXPECT_NEAR(got.m[3*4+1], 0.0f, kEps);
    EXPECT_NEAR(got.m[3*4+2], 0.0f, kEps);
}
