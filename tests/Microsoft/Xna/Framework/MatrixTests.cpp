#include <gtest/gtest.h>
#include <cmath>
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector3;

static constexpr float kEps = 1e-5f;

// --- Identity ---

TEST(MatrixTest, IdentityHasOnesOnDiagonal)
{
    Matrix id = Matrix::getIdentityProperty();
    EXPECT_FLOAT_EQ(id.M11, 1.0f);
    EXPECT_FLOAT_EQ(id.M22, 1.0f);
    EXPECT_FLOAT_EQ(id.M33, 1.0f);
    EXPECT_FLOAT_EQ(id.M44, 1.0f);
}

TEST(MatrixTest, IdentityHasZeroOffDiagonal)
{
    Matrix id = Matrix::getIdentityProperty();
    EXPECT_FLOAT_EQ(id.M12, 0.0f);
    EXPECT_FLOAT_EQ(id.M13, 0.0f);
    EXPECT_FLOAT_EQ(id.M14, 0.0f);
    EXPECT_FLOAT_EQ(id.M21, 0.0f);
    EXPECT_FLOAT_EQ(id.M41, 0.0f);
    EXPECT_FLOAT_EQ(id.M42, 0.0f);
    EXPECT_FLOAT_EQ(id.M43, 0.0f);
}

TEST(MatrixTest, DefaultConstructorIsAllZeros)
{
    Matrix m;
    EXPECT_FLOAT_EQ(m.M11, 0.0f);
    EXPECT_FLOAT_EQ(m.M22, 0.0f);
    EXPECT_FLOAT_EQ(m.M44, 0.0f);
}

// --- Determinant ---

TEST(MatrixTest, DeterminantOfIdentityIsOne)
{
    EXPECT_NEAR(Matrix::getIdentityProperty().Determinant(), 1.0f, kEps);
}

TEST(MatrixTest, DeterminantOfScaleMatrix)
{
    // Scale(2,3,4) → det = 2*3*4 = 24
    Matrix m = Matrix::CreateScale(2.0f, 3.0f, 4.0f);
    EXPECT_NEAR(m.Determinant(), 24.0f, kEps);
}

// --- CreateTranslation ---

TEST(MatrixTest, CreateTranslationStoresInRow4)
{
    Matrix m = Matrix::CreateTranslation(5.0f, 6.0f, 7.0f);
    EXPECT_FLOAT_EQ(m.M41, 5.0f);
    EXPECT_FLOAT_EQ(m.M42, 6.0f);
    EXPECT_FLOAT_EQ(m.M43, 7.0f);
    EXPECT_FLOAT_EQ(m.M44, 1.0f);
    // Diagonal ones
    EXPECT_FLOAT_EQ(m.M11, 1.0f);
    EXPECT_FLOAT_EQ(m.M22, 1.0f);
    EXPECT_FLOAT_EQ(m.M33, 1.0f);
}

TEST(MatrixTest, CreateTranslationVectorOverload)
{
    Matrix m = Matrix::CreateTranslation(Vector3(1.0f, 2.0f, 3.0f));
    EXPECT_FLOAT_EQ(m.M41, 1.0f);
    EXPECT_FLOAT_EQ(m.M42, 2.0f);
    EXPECT_FLOAT_EQ(m.M43, 3.0f);
}

TEST(MatrixTest, TranslationPropertyRoundTrip)
{
    Matrix m = Matrix::CreateTranslation(9.0f, 8.0f, 7.0f);
    Vector3 t = m.getTranslationProperty();
    EXPECT_FLOAT_EQ(t.X, 9.0f);
    EXPECT_FLOAT_EQ(t.Y, 8.0f);
    EXPECT_FLOAT_EQ(t.Z, 7.0f);
}

// --- CreateScale ---

TEST(MatrixTest, CreateUniformScaleSetsDiagonal)
{
    Matrix m = Matrix::CreateScale(3.0f);
    EXPECT_FLOAT_EQ(m.M11, 3.0f);
    EXPECT_FLOAT_EQ(m.M22, 3.0f);
    EXPECT_FLOAT_EQ(m.M33, 3.0f);
    EXPECT_FLOAT_EQ(m.M44, 1.0f);
    EXPECT_FLOAT_EQ(m.M12, 0.0f);
}

TEST(MatrixTest, CreateNonUniformScale)
{
    Matrix m = Matrix::CreateScale(2.0f, 3.0f, 4.0f);
    EXPECT_FLOAT_EQ(m.M11, 2.0f);
    EXPECT_FLOAT_EQ(m.M22, 3.0f);
    EXPECT_FLOAT_EQ(m.M33, 4.0f);
    EXPECT_FLOAT_EQ(m.M44, 1.0f);
}

// --- Multiply ---

TEST(MatrixTest, MultiplyByIdentityPreservesMatrix)
{
    Matrix m(
        1,2,3,4, 5,6,7,8,
        9,10,11,12, 13,14,15,16
    );
    Matrix id = Matrix::getIdentityProperty();
    Matrix result = Matrix::Multiply(m, id);
    EXPECT_NEAR(result.M11, 1.0f, kEps);
    EXPECT_NEAR(result.M44, 16.0f, kEps);
}

TEST(MatrixTest, MultiplyIdentityByMatrixPreservesMatrix)
{
    Matrix m(
        1,2,3,4, 5,6,7,8,
        9,10,11,12, 13,14,15,16
    );
    Matrix id = Matrix::getIdentityProperty();
    Matrix result = id * m;
    EXPECT_NEAR(result.M11, 1.0f, kEps);
    EXPECT_NEAR(result.M23, 7.0f, kEps);
}

TEST(MatrixTest, MultiplyTwoTranslationsAddsThem)
{
    Matrix t1 = Matrix::CreateTranslation(1.0f, 0.0f, 0.0f);
    Matrix t2 = Matrix::CreateTranslation(2.0f, 0.0f, 0.0f);
    Matrix combined = t1 * t2;
    EXPECT_NEAR(combined.M41, 3.0f, kEps);
}

// --- Invert ---

TEST(MatrixTest, InvertOfIdentityIsIdentity)
{
    Matrix result = Matrix::Invert(Matrix::getIdentityProperty());
    EXPECT_NEAR(result.M11, 1.0f, kEps);
    EXPECT_NEAR(result.M22, 1.0f, kEps);
    EXPECT_NEAR(result.M33, 1.0f, kEps);
    EXPECT_NEAR(result.M44, 1.0f, kEps);
    EXPECT_NEAR(result.M12, 0.0f, kEps);
}

TEST(MatrixTest, MatrixTimesItsInverseIsIdentity)
{
    Matrix m = Matrix::CreateTranslation(3.0f, -1.0f, 2.0f);
    Matrix inv = Matrix::Invert(m);
    Matrix product = m * inv;
    EXPECT_NEAR(product.M11, 1.0f, kEps);
    EXPECT_NEAR(product.M22, 1.0f, kEps);
    EXPECT_NEAR(product.M33, 1.0f, kEps);
    EXPECT_NEAR(product.M44, 1.0f, kEps);
    EXPECT_NEAR(product.M41, 0.0f, kEps);
    EXPECT_NEAR(product.M42, 0.0f, kEps);
    EXPECT_NEAR(product.M43, 0.0f, kEps);
}

// --- Transpose ---

TEST(MatrixTest, TransposeOfIdentityIsIdentity)
{
    Matrix result = Matrix::Transpose(Matrix::getIdentityProperty());
    EXPECT_NEAR(result.M11, 1.0f, kEps);
    EXPECT_NEAR(result.M12, 0.0f, kEps);
    EXPECT_NEAR(result.M21, 0.0f, kEps);
}

TEST(MatrixTest, TransposeSwapsOffDiagonalElements)
{
    Matrix m(
        1,2,0,0,
        3,4,0,0,
        0,0,1,0,
        0,0,0,1
    );
    Matrix t = Matrix::Transpose(m);
    EXPECT_NEAR(t.M12, 3.0f, kEps); // original M21=3 goes to M12
    EXPECT_NEAR(t.M21, 2.0f, kEps); // original M12=2 goes to M21
}

// --- CreateRotation ---

TEST(MatrixTest, CreateRotationZByZeroIsIdentity)
{
    Matrix m = Matrix::CreateRotationZ(0.0f);
    EXPECT_NEAR(m.M11, 1.0f, kEps);
    EXPECT_NEAR(m.M12, 0.0f, kEps);
    EXPECT_NEAR(m.M21, 0.0f, kEps);
    EXPECT_NEAR(m.M22, 1.0f, kEps);
    EXPECT_NEAR(m.M33, 1.0f, kEps);
    EXPECT_NEAR(m.M44, 1.0f, kEps);
}

TEST(MatrixTest, CreateRotationZByHalfPi)
{
    // cos(π/2)≈0, sin(π/2)=1 → M11=0, M12=1, M21=-1, M22=0
    Matrix m = Matrix::CreateRotationZ(static_cast<float>(M_PI) / 2.0f);
    EXPECT_NEAR(m.M11, 0.0f, kEps);
    EXPECT_NEAR(m.M12, 1.0f, kEps);
    EXPECT_NEAR(m.M21, -1.0f, kEps);
    EXPECT_NEAR(m.M22, 0.0f, kEps);
    EXPECT_NEAR(m.M33, 1.0f, kEps);
}

// --- Operators ---

TEST(MatrixTest, EqualityOperator)
{
    Matrix id1 = Matrix::getIdentityProperty();
    Matrix id2 = Matrix::getIdentityProperty();
    EXPECT_TRUE(id1 == id2);
    EXPECT_FALSE(id1 != id2);
}

TEST(MatrixTest, AdditionOperator)
{
    Matrix id = Matrix::getIdentityProperty();
    Matrix result = id + id;
    EXPECT_NEAR(result.M11, 2.0f, kEps);
    EXPECT_NEAR(result.M22, 2.0f, kEps);
    EXPECT_NEAR(result.M12, 0.0f, kEps);
}

TEST(MatrixTest, ScalarMultiplicationOperator)
{
    Matrix id = Matrix::getIdentityProperty();
    Matrix result = id * 3.0f;
    EXPECT_NEAR(result.M11, 3.0f, kEps);
    EXPECT_NEAR(result.M22, 3.0f, kEps);
}

// --- Direction properties ---

TEST(MatrixTest, IdentityRightForwardUp)
{
    Matrix id = Matrix::getIdentityProperty();
    Vector3 right = id.getRightProperty();
    EXPECT_NEAR(right.X, 1.0f, kEps);
    EXPECT_NEAR(right.Y, 0.0f, kEps);
    EXPECT_NEAR(right.Z, 0.0f, kEps);

    Vector3 up = id.getUpProperty();
    EXPECT_NEAR(up.X, 0.0f, kEps);
    EXPECT_NEAR(up.Y, 1.0f, kEps);
    EXPECT_NEAR(up.Z, 0.0f, kEps);
}
