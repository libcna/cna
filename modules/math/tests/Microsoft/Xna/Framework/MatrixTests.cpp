// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include <cmath>
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Plane.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
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

// --- Equals / GetHashCode / ToString ---

TEST(MatrixTest, EqualsReturnsTrueForEqualMatrices)
{
    Matrix a = Matrix::CreateTranslation(1.0f, 2.0f, 3.0f);
    Matrix b = Matrix::CreateTranslation(1.0f, 2.0f, 3.0f);
    EXPECT_TRUE(a.Equals(b));
}

TEST(MatrixTest, EqualsReturnsFalseForDifferentMatrices)
{
    Matrix a = Matrix::CreateTranslation(1.0f, 0.0f, 0.0f);
    Matrix b = Matrix::CreateTranslation(2.0f, 0.0f, 0.0f);
    EXPECT_FALSE(a.Equals(b));
}

TEST(MatrixTest, GetHashCodeEqualMatricesHaveEqualHash)
{
    Matrix a = Matrix::getIdentityProperty();
    Matrix b = Matrix::getIdentityProperty();
    EXPECT_EQ(a.GetHashCode(), b.GetHashCode());
}

TEST(MatrixTest, GetHashCodeDifferentMatricesTypicallyDiffer)
{
    // Use scale(2) vs identity: M11/M22/M33 differ, so hash differs.
    Matrix a = Matrix::getIdentityProperty();
    Matrix b = Matrix::CreateScale(2.0f);
    EXPECT_NE(a.GetHashCode(), b.GetHashCode());
}

TEST(MatrixTest, ToStringContainsM11)
{
    Matrix id = Matrix::getIdentityProperty();
    std::string s = id.ToString();
    EXPECT_NE(s.find("M11"), std::string::npos);
    EXPECT_NE(s.find("M44"), std::string::npos);
}

// --- Out-ref overloads ---

TEST(MatrixTest, AddRefOverload)
{
    Matrix a = Matrix::getIdentityProperty();
    Matrix b = Matrix::getIdentityProperty();
    Matrix result;
    Matrix::Add(a, b, result);
    EXPECT_NEAR(result.M11, 2.0f, kEps);
    EXPECT_NEAR(result.M12, 0.0f, kEps);
}

TEST(MatrixTest, MultiplyRefOverload)
{
    Matrix m(1,2,3,4, 5,6,7,8, 9,10,11,12, 13,14,15,16);
    Matrix id = Matrix::getIdentityProperty();
    Matrix result;
    Matrix::Multiply(m, id, result);
    EXPECT_NEAR(result.M11, 1.0f, kEps);
    EXPECT_NEAR(result.M44, 16.0f, kEps);
}

TEST(MatrixTest, TransposeRefOverload)
{
    Matrix m(1,2,0,0, 3,4,0,0, 0,0,1,0, 0,0,0,1);
    Matrix result;
    Matrix::Transpose(m, result);
    EXPECT_NEAR(result.M12, 3.0f, kEps);
    EXPECT_NEAR(result.M21, 2.0f, kEps);
}

TEST(MatrixTest, LerpRefOverload)
{
    Matrix a = Matrix::CreateScale(0.0f);
    Matrix b = Matrix::CreateScale(2.0f);
    Matrix result;
    Matrix::Lerp(a, b, 0.5f, result);
    EXPECT_NEAR(result.M11, 1.0f, kEps);
}

// --- Lerp ---

TEST(MatrixTest, LerpAtZeroReturnsFirst)
{
    Matrix a = Matrix::getIdentityProperty();
    Matrix b = Matrix::CreateScale(3.0f);
    Matrix result = Matrix::Lerp(a, b, 0.0f);
    EXPECT_NEAR(result.M11, 1.0f, kEps);
}

TEST(MatrixTest, LerpAtOneReturnsSecond)
{
    Matrix a = Matrix::getIdentityProperty();
    Matrix b = Matrix::CreateScale(3.0f);
    Matrix result = Matrix::Lerp(a, b, 1.0f);
    EXPECT_NEAR(result.M11, 3.0f, kEps);
}

TEST(MatrixTest, LerpAtHalfReturnsMidpoint)
{
    Matrix a = Matrix::CreateScale(0.0f);
    Matrix b = Matrix::CreateScale(2.0f);
    Matrix result = Matrix::Lerp(a, b, 0.5f);
    EXPECT_NEAR(result.M11, 1.0f, kEps);
}

// --- Subtract / Negate / Divide ---

TEST(MatrixTest, SubtractIdentityMinusIdentityIsZero)
{
    Matrix result = Matrix::Subtract(Matrix::getIdentityProperty(), Matrix::getIdentityProperty());
    EXPECT_NEAR(result.M11, 0.0f, kEps);
    EXPECT_NEAR(result.M22, 0.0f, kEps);
}

TEST(MatrixTest, SubtractOperator)
{
    Matrix a = Matrix::CreateScale(3.0f);
    Matrix b = Matrix::CreateScale(1.0f);
    Matrix result = a - b;
    EXPECT_NEAR(result.M11, 2.0f, kEps);
}

TEST(MatrixTest, NegateFlipsSign)
{
    Matrix result = Matrix::Negate(Matrix::getIdentityProperty());
    EXPECT_NEAR(result.M11, -1.0f, kEps);
    EXPECT_NEAR(result.M22, -1.0f, kEps);
}

TEST(MatrixTest, UnaryNegateOperator)
{
    Matrix m = Matrix::getIdentityProperty();
    Matrix result = -m;
    EXPECT_NEAR(result.M11, -1.0f, kEps);
}

TEST(MatrixTest, DivideMatrixByMatrix)
{
    Matrix a = Matrix::CreateScale(4.0f);
    Matrix b = Matrix::CreateScale(2.0f);
    // Divide is element-wise, not matrix inverse
    Matrix result = Matrix::Divide(a, b);
    EXPECT_NEAR(result.M11, 2.0f, kEps);
    EXPECT_NEAR(result.M44, 1.0f, kEps); // b.M44=1, so 1/1=1
}

TEST(MatrixTest, DivideMatrixByScalar)
{
    Matrix a = Matrix::CreateScale(4.0f);
    Matrix result = Matrix::Divide(a, 2.0f);
    EXPECT_NEAR(result.M11, 2.0f, kEps);
}

TEST(MatrixTest, DivideOperatorMatrix)
{
    Matrix a = Matrix::CreateScale(6.0f);
    Matrix b = Matrix::CreateScale(2.0f);
    Matrix result = a / b;
    EXPECT_NEAR(result.M11, 3.0f, kEps);
}

TEST(MatrixTest, DivideOperatorScalar)
{
    Matrix m = Matrix::CreateScale(6.0f);
    Matrix result = m / 2.0f;
    EXPECT_NEAR(result.M11, 3.0f, kEps);
}

// --- Direction property setters ---

TEST(MatrixTest, SetRightPropertyRoundTrip)
{
    Matrix m = Matrix::getIdentityProperty();
    m.setRightProperty(Vector3(2.0f, 0.0f, 0.0f));
    EXPECT_NEAR(m.getRightProperty().X, 2.0f, kEps);
}

TEST(MatrixTest, SetUpPropertyRoundTrip)
{
    Matrix m = Matrix::getIdentityProperty();
    m.setUpProperty(Vector3(0.0f, 5.0f, 0.0f));
    EXPECT_NEAR(m.getUpProperty().Y, 5.0f, kEps);
}

TEST(MatrixTest, SetForwardPropertyNegatesRow3)
{
    Matrix m = Matrix::getIdentityProperty();
    m.setForwardProperty(Vector3(0.0f, 0.0f, -1.0f));
    EXPECT_NEAR(m.M33, 1.0f, kEps); // stored as -(-1) = 1
}

TEST(MatrixTest, SetBackwardPropertyRow3)
{
    Matrix m = Matrix::getIdentityProperty();
    m.setBackwardProperty(Vector3(0.0f, 0.0f, 3.0f));
    EXPECT_NEAR(m.M33, 3.0f, kEps);
}

TEST(MatrixTest, SetDownPropertyNegatesRow2)
{
    Matrix m = Matrix::getIdentityProperty();
    m.setDownProperty(Vector3(0.0f, -1.0f, 0.0f));
    EXPECT_NEAR(m.M22, 1.0f, kEps); // stored as -(-1) = 1
}

TEST(MatrixTest, SetLeftPropertyNegatesRow1)
{
    Matrix m = Matrix::getIdentityProperty();
    m.setLeftProperty(Vector3(-1.0f, 0.0f, 0.0f));
    EXPECT_NEAR(m.M11, 1.0f, kEps); // stored as -(-1) = 1
}

TEST(MatrixTest, SetTranslationPropertyRoundTrip)
{
    Matrix m = Matrix::getIdentityProperty();
    m.setTranslationProperty(Vector3(7.0f, 8.0f, 9.0f));
    Vector3 t = m.getTranslationProperty();
    EXPECT_NEAR(t.X, 7.0f, kEps);
    EXPECT_NEAR(t.Y, 8.0f, kEps);
    EXPECT_NEAR(t.Z, 9.0f, kEps);
}

// --- CreateRotationX / Y ---

TEST(MatrixTest, CreateRotationXByZeroIsIdentity)
{
    Matrix m = Matrix::CreateRotationX(0.0f);
    EXPECT_NEAR(m.M22, 1.0f, kEps);
    EXPECT_NEAR(m.M23, 0.0f, kEps);
    EXPECT_NEAR(m.M32, 0.0f, kEps);
    EXPECT_NEAR(m.M33, 1.0f, kEps);
}

TEST(MatrixTest, CreateRotationXByHalfPi)
{
    Matrix m = Matrix::CreateRotationX(static_cast<float>(M_PI) / 2.0f);
    EXPECT_NEAR(m.M22,  0.0f, kEps);
    EXPECT_NEAR(m.M23,  1.0f, kEps);
    EXPECT_NEAR(m.M32, -1.0f, kEps);
    EXPECT_NEAR(m.M33,  0.0f, kEps);
}

TEST(MatrixTest, CreateRotationYByZeroIsIdentity)
{
    Matrix m = Matrix::CreateRotationY(0.0f);
    EXPECT_NEAR(m.M11, 1.0f, kEps);
    EXPECT_NEAR(m.M13, 0.0f, kEps);
    EXPECT_NEAR(m.M31, 0.0f, kEps);
    EXPECT_NEAR(m.M33, 1.0f, kEps);
}

TEST(MatrixTest, CreateRotationYByHalfPi)
{
    Matrix m = Matrix::CreateRotationY(static_cast<float>(M_PI) / 2.0f);
    EXPECT_NEAR(m.M11,  0.0f, kEps);
    EXPECT_NEAR(m.M13, -1.0f, kEps);
    EXPECT_NEAR(m.M31,  1.0f, kEps);
    EXPECT_NEAR(m.M33,  0.0f, kEps);
}

// --- CreateScale(Vector3) ---

TEST(MatrixTest, CreateScaleVector3)
{
    Matrix m = Matrix::CreateScale(Vector3(2.0f, 3.0f, 4.0f));
    EXPECT_NEAR(m.M11, 2.0f, kEps);
    EXPECT_NEAR(m.M22, 3.0f, kEps);
    EXPECT_NEAR(m.M33, 4.0f, kEps);
    EXPECT_NEAR(m.M44, 1.0f, kEps);
}

TEST(MatrixTest, CreateScaleVector3RefOverload)
{
    Vector3 s(5.0f, 6.0f, 7.0f);
    Matrix result;
    Matrix::CreateScale(s, result);
    EXPECT_NEAR(result.M11, 5.0f, kEps);
    EXPECT_NEAR(result.M22, 6.0f, kEps);
    EXPECT_NEAR(result.M33, 7.0f, kEps);
}

// --- CreateFromAxisAngle ---

TEST(MatrixTest, CreateFromAxisAngleZeroRotationIsIdentity)
{
    Matrix m = Matrix::CreateFromAxisAngle(Vector3(0.0f, 1.0f, 0.0f), 0.0f);
    EXPECT_NEAR(m.M11, 1.0f, kEps);
    EXPECT_NEAR(m.M22, 1.0f, kEps);
    EXPECT_NEAR(m.M33, 1.0f, kEps);
}

// --- CreateFromQuaternion ---

TEST(MatrixTest, CreateFromIdentityQuaternionIsIdentityMatrix)
{
    using Microsoft::Xna::Framework::Quaternion;
    Matrix m = Matrix::CreateFromQuaternion(Quaternion::Identity);
    EXPECT_NEAR(m.M11, 1.0f, kEps);
    EXPECT_NEAR(m.M22, 1.0f, kEps);
    EXPECT_NEAR(m.M33, 1.0f, kEps);
    EXPECT_NEAR(m.M44, 1.0f, kEps);
    EXPECT_NEAR(m.M12, 0.0f, kEps);
}

// --- CreateFromYawPitchRoll ---

TEST(MatrixTest, CreateFromYawPitchRollAllZeroIsIdentity)
{
    Matrix m = Matrix::CreateFromYawPitchRoll(0.0f, 0.0f, 0.0f);
    EXPECT_NEAR(m.M11, 1.0f, kEps);
    EXPECT_NEAR(m.M22, 1.0f, kEps);
    EXPECT_NEAR(m.M33, 1.0f, kEps);
}

// --- CreateLookAt ---

TEST(MatrixTest, CreateLookAtM44IsOne)
{
    Matrix m = Matrix::CreateLookAt(
        Vector3(0.0f, 0.0f, 5.0f),
        Vector3(0.0f, 0.0f, 0.0f),
        Vector3(0.0f, 1.0f, 0.0f)
    );
    EXPECT_NEAR(m.M44, 1.0f, kEps);
}

// --- CreateOrthographic ---

TEST(MatrixTest, CreateOrthographicM11)
{
    // M11 = 2/width
    Matrix m = Matrix::CreateOrthographic(4.0f, 3.0f, 0.1f, 100.0f);
    EXPECT_NEAR(m.M11, 0.5f, kEps);
    EXPECT_NEAR(m.M22, 2.0f / 3.0f, kEps);
    EXPECT_NEAR(m.M44, 1.0f, kEps);
}

TEST(MatrixTest, CreateOrthographicOffCenterM44)
{
    Matrix m = Matrix::CreateOrthographicOffCenter(-2.0f, 2.0f, -1.5f, 1.5f, 0.1f, 100.0f);
    EXPECT_NEAR(m.M44, 1.0f, kEps);
    EXPECT_NEAR(m.M11, 0.5f, kEps); // 2/(2-(-2)) = 0.5
}

// --- CreatePerspective ---

TEST(MatrixTest, CreatePerspectiveThrowsForBadNear)
{
    Matrix result;
    EXPECT_THROW(Matrix::CreatePerspective(1.0f, 1.0f, 0.0f, 100.0f, result), std::invalid_argument);
}

TEST(MatrixTest, CreatePerspectiveThrowsWhenNearGreaterFar)
{
    Matrix result;
    EXPECT_THROW(Matrix::CreatePerspective(1.0f, 1.0f, 10.0f, 5.0f, result), std::invalid_argument);
}

TEST(MatrixTest, CreatePerspectiveM34IsMinusOne)
{
    Matrix m = Matrix::CreatePerspective(2.0f, 2.0f, 1.0f, 100.0f);
    EXPECT_NEAR(m.M34, -1.0f, kEps);
}

TEST(MatrixTest, CreatePerspectiveFieldOfViewThrowsBadFov)
{
    Matrix result;
    EXPECT_THROW(Matrix::CreatePerspectiveFieldOfView(0.0f, 1.0f, 1.0f, 100.0f, result), std::invalid_argument);
}

TEST(MatrixTest, CreatePerspectiveFieldOfViewM34IsMinusOne)
{
    Matrix m = Matrix::CreatePerspectiveFieldOfView(
        static_cast<float>(M_PI) / 4.0f, 16.0f / 9.0f, 0.1f, 1000.0f
    );
    EXPECT_NEAR(m.M34, -1.0f, kEps);
}

TEST(MatrixTest, CreatePerspectiveOffCenterThrowsBadNear)
{
    Matrix result;
    EXPECT_THROW(Matrix::CreatePerspectiveOffCenter(-1.0f, 1.0f, -1.0f, 1.0f, 0.0f, 100.0f, result),
                 std::invalid_argument);
}

TEST(MatrixTest, CreatePerspectiveOffCenterM34IsMinusOne)
{
    Matrix m = Matrix::CreatePerspectiveOffCenter(-1.0f, 1.0f, -1.0f, 1.0f, 1.0f, 100.0f);
    EXPECT_NEAR(m.M34, -1.0f, kEps);
}

// --- CreateShadow ---

TEST(MatrixTest, CreateShadowM44EqualsDot)
{
    using Microsoft::Xna::Framework::Plane;
    // Ground plane at y=0, light from above
    Vector3 lightDir(0.0f, -1.0f, 0.0f);
    Plane plane(Vector3(0.0f, 1.0f, 0.0f), 0.0f);
    Matrix m = Matrix::CreateShadow(lightDir, plane);
    // dot = (0*0 + 1*(-1) + 0*0) = -1
    EXPECT_NEAR(m.M44, -1.0f, kEps);
}

// --- CreateReflection ---

TEST(MatrixTest, CreateReflectionIdentityM11)
{
    using Microsoft::Xna::Framework::Plane;
    // Reflect across XZ plane (normal = up)
    Plane plane(Vector3(0.0f, 1.0f, 0.0f), 0.0f);
    Matrix m = Matrix::CreateReflection(plane);
    EXPECT_NEAR(m.M11, 1.0f, kEps);
    EXPECT_NEAR(m.M22, -1.0f, kEps); // Y flipped
    EXPECT_NEAR(m.M33, 1.0f, kEps);
}

// --- CreateWorld ---

TEST(MatrixTest, CreateWorldM44IsOne)
{
    Matrix m = Matrix::CreateWorld(
        Vector3(0.0f, 0.0f, 0.0f),
        Vector3(0.0f, 0.0f, -1.0f),
        Vector3(0.0f, 1.0f, 0.0f)
    );
    EXPECT_NEAR(m.M44, 1.0f, kEps);
}

// --- CreateBillboard ---

TEST(MatrixTest, CreateBillboardM44IsOne)
{
    Matrix m = Matrix::CreateBillboard(
        Vector3(0.0f, 0.0f, 0.0f),
        Vector3(0.0f, 0.0f, 5.0f),
        Vector3(0.0f, 1.0f, 0.0f),
        std::nullopt
    );
    EXPECT_NEAR(m.M44, 1.0f, kEps);
    EXPECT_NEAR(m.M14, 0.0f, kEps);
}

// --- CreateConstrainedBillboard ---

TEST(MatrixTest, CreateConstrainedBillboardM44IsOne)
{
    Matrix m = Matrix::CreateConstrainedBillboard(
        Vector3(0.0f, 0.0f, 0.0f),
        Vector3(0.0f, 0.0f, 5.0f),
        Vector3(0.0f, 1.0f, 0.0f),
        std::nullopt,
        std::nullopt
    );
    EXPECT_NEAR(m.M44, 1.0f, kEps);
}

// --- Transform ---

TEST(MatrixTest, TransformByIdentityQuaternionPreservesMatrix)
{
    using Microsoft::Xna::Framework::Quaternion;
    Matrix m = Matrix::CreateTranslation(1.0f, 2.0f, 3.0f);
    Matrix result = Matrix::Transform(m, Quaternion::Identity);
    EXPECT_NEAR(result.M41, 1.0f, kEps);
    EXPECT_NEAR(result.M42, 2.0f, kEps);
    EXPECT_NEAR(result.M43, 3.0f, kEps);
}

// --- Decompose ---

TEST(MatrixTest, DecomposeIdentityReturnsUnitScaleAndNoTranslation)
{
    using Microsoft::Xna::Framework::Quaternion;
    Vector3 scale(0,0,0), translation(0,0,0);
    Quaternion rotation = Quaternion::Identity;
    bool ok = Matrix::getIdentityProperty().Decompose(scale, rotation, translation);
    EXPECT_TRUE(ok);
    EXPECT_NEAR(scale.X, 1.0f, kEps);
    EXPECT_NEAR(scale.Y, 1.0f, kEps);
    EXPECT_NEAR(scale.Z, 1.0f, kEps);
    EXPECT_NEAR(translation.X, 0.0f, kEps);
}

TEST(MatrixTest, DecomposeScaleMatrix)
{
    using Microsoft::Xna::Framework::Quaternion;
    Vector3 scale(0,0,0), translation(0,0,0);
    Quaternion rotation = Quaternion::Identity;
    Matrix::CreateScale(2.0f, 3.0f, 4.0f).Decompose(scale, rotation, translation);
    EXPECT_NEAR(scale.X, 2.0f, kEps);
    EXPECT_NEAR(scale.Y, 3.0f, kEps);
    EXPECT_NEAR(scale.Z, 4.0f, kEps);
}

TEST(MatrixTest, DecomposeAxisAlignedQuarterTurnRecoversTheRotationRatherThanIdentity)
{
    // A quarter turn about Z has exact zeros in every row, so each row product is +/-0.0 -- and
    // (-1) * 0 is NEGATIVE zero. Testing that with std::signbit (true for -0.0) flips the row's
    // sign, making the normalised 3x3 a reflection rather than a rotation, and
    // CreateFromRotationMatrix of a reflection is not a rotation at all: the quarter turn came
    // back as the identity quaternion, so a node authored with a 90-degree rotation lost its
    // orientation with no error anywhere. FNA's own guard is `Math.Sign(product) < 0`, and
    // Math.Sign returns 0 for -0.0f, so FNA takes the +1 branch.
    //
    // Asserted for all three axes because the zero pattern differs per row, and one axis passing
    // is exactly what made this look fine.
    using Microsoft::Xna::Framework::Quaternion;
    const std::pair<Quaternion, const char*> cases[] = {
        {Quaternion::CreateFromAxisAngle(Vector3(1.0f, 0.0f, 0.0f), 1.57079633f), "about X"},
        {Quaternion::CreateFromAxisAngle(Vector3(0.0f, 1.0f, 0.0f), 1.57079633f), "about Y"},
        {Quaternion::CreateFromAxisAngle(Vector3(0.0f, 0.0f, 1.0f), 1.57079633f), "about Z"},
    };

    for (const auto& [expected, what] : cases)
    {
        SCOPED_TRACE(what);
        Vector3 scale(0, 0, 0), translation(0, 0, 0);
        Quaternion rotation = Quaternion::Identity;
        const Matrix m = Matrix::CreateFromQuaternion(expected);
        EXPECT_TRUE(m.Decompose(scale, rotation, translation));

        // Unit scale, and POSITIVE on every axis: a negative component here is the sign flip
        // itself, reported before the quaternion comparison so the cause is legible.
        EXPECT_NEAR(1.0f, scale.X, kEps);
        EXPECT_NEAR(1.0f, scale.Y, kEps);
        EXPECT_NEAR(1.0f, scale.Z, kEps);

        // q and -q are the same rotation, so compare on the sign-normalised form.
        const float flip = (rotation.W * expected.W) < 0.0f ? -1.0f : 1.0f;
        EXPECT_NEAR(expected.X, flip * rotation.X, 1e-5f);
        EXPECT_NEAR(expected.Y, flip * rotation.Y, 1e-5f);
        EXPECT_NEAR(expected.Z, flip * rotation.Z, 1e-5f);
        EXPECT_NEAR(expected.W, flip * rotation.W, 1e-5f);
    }
}

TEST(MatrixTest, DecomposeReportsPositiveScaleForAMirroredAffineTransform)
{
    // The boundary of the fix above, stated so nobody has to rediscover it.
    //
    // XNA's per-row sign heuristic multiplies the FOURTH column into each product -- M14, M24,
    // M34 -- and those are zero in every affine transform. The product is therefore always zero
    // for the matrices anyone actually decomposes, so the sign is always +1 and a mirrored
    // transform decomposes to POSITIVE scales with the reflection folded into the quaternion.
    // That is FNA's behaviour, not a CNA shortcut, and behaviour fidelity wins over the more
    // obviously useful answer.
    //
    // The practical consequence: Decompose is not a mirroring test. Anything that needs to know
    // whether a transform reverses handedness -- glTF's own winding rule among them -- must take
    // the determinant of the 3x3 instead, which is unambiguous.
    using Microsoft::Xna::Framework::Quaternion;
    Vector3 scale(0, 0, 0), translation(0, 0, 0);
    Quaternion rotation = Quaternion::Identity;

    const Matrix m = Matrix::CreateScale(-2.0f, 3.0f, 4.0f) *
                     Matrix::CreateFromAxisAngle(Vector3(0.0f, 1.0f, 0.0f), 0.7f);
    EXPECT_TRUE(m.Decompose(scale, rotation, translation));
    EXPECT_NEAR(2.0f, scale.X, 1e-4f);
    EXPECT_NEAR(3.0f, scale.Y, 1e-4f);
    EXPECT_NEAR(4.0f, scale.Z, 1e-4f);

    // ...while the determinant does answer the question the scales cannot.
    EXPECT_LT(m.Determinant(), 0.0f) << "the transform really does mirror";
}

// --- ToColumnMajor (CNAEXT) ---

TEST(MatrixTest, ToColumnMajorIdentity)
{
    float data[16];
    Matrix::getIdentityProperty().ToColumnMajor(data);
    EXPECT_NEAR(data[0], 1.0f, kEps);  // M11
    EXPECT_NEAR(data[5], 1.0f, kEps);  // M22
    EXPECT_NEAR(data[10], 1.0f, kEps); // M33
    EXPECT_NEAR(data[15], 1.0f, kEps); // M44
    EXPECT_NEAR(data[1], 0.0f, kEps);  // M12
}
