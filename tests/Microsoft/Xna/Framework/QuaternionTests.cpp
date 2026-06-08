#include <gtest/gtest.h>
#include <cmath>
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"

using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Quaternion;
using Microsoft::Xna::Framework::Vector3;

static constexpr float kEps = 1e-5f;

// --- Identity ---

TEST(QuaternionTest, IdentityHasZeroVectorPartAndOneW)
{
    EXPECT_FLOAT_EQ(Quaternion::Identity.X, 0.0f);
    EXPECT_FLOAT_EQ(Quaternion::Identity.Y, 0.0f);
    EXPECT_FLOAT_EQ(Quaternion::Identity.Z, 0.0f);
    EXPECT_FLOAT_EQ(Quaternion::Identity.W, 1.0f);
}

TEST(QuaternionTest, IdentityHasUnitLength)
{
    EXPECT_NEAR(Quaternion::Identity.Length(), 1.0f, kEps);
}

// --- Construction ---

TEST(QuaternionTest, FourComponentConstructor)
{
    Quaternion q(1.0f, 2.0f, 3.0f, 4.0f);
    EXPECT_FLOAT_EQ(q.X, 1.0f);
    EXPECT_FLOAT_EQ(q.Y, 2.0f);
    EXPECT_FLOAT_EQ(q.Z, 3.0f);
    EXPECT_FLOAT_EQ(q.W, 4.0f);
}

TEST(QuaternionTest, VectorScalarConstructor)
{
    Quaternion q(Vector3(1.0f, 2.0f, 3.0f), 4.0f);
    EXPECT_FLOAT_EQ(q.X, 1.0f);
    EXPECT_FLOAT_EQ(q.Y, 2.0f);
    EXPECT_FLOAT_EQ(q.Z, 3.0f);
    EXPECT_FLOAT_EQ(q.W, 4.0f);
}

// --- Length ---

TEST(QuaternionTest, LengthOfKnownQuaternion)
{
    // sqrt(0+0+0+16) = 4
    Quaternion q(0.0f, 0.0f, 0.0f, 4.0f);
    EXPECT_FLOAT_EQ(q.Length(), 4.0f);
}

TEST(QuaternionTest, LengthSquaredOfKnownQuaternion)
{
    Quaternion q(1.0f, 2.0f, 2.0f, 0.0f);
    EXPECT_FLOAT_EQ(q.LengthSquared(), 9.0f);
}

// --- Normalize ---

TEST(QuaternionTest, NormalizeInPlaceProducesUnitQuaternion)
{
    Quaternion q(0.0f, 0.0f, 0.0f, 5.0f);
    q.Normalize();
    EXPECT_NEAR(q.Length(), 1.0f, kEps);
    EXPECT_NEAR(q.W, 1.0f, kEps);
}

TEST(QuaternionTest, NormalizeStaticPreservesOriginal)
{
    Quaternion original(1.0f, 2.0f, 3.0f, 4.0f);
    Quaternion result = Quaternion::Normalize(original);
    EXPECT_NEAR(result.Length(), 1.0f, kEps);
    EXPECT_FLOAT_EQ(original.X, 1.0f);
}

// --- Conjugate ---

TEST(QuaternionTest, ConjugateNegatesVectorPart)
{
    Quaternion result = Quaternion::Conjugate(Quaternion(1.0f, 2.0f, 3.0f, 4.0f));
    EXPECT_FLOAT_EQ(result.X, -1.0f);
    EXPECT_FLOAT_EQ(result.Y, -2.0f);
    EXPECT_FLOAT_EQ(result.Z, -3.0f);
    EXPECT_FLOAT_EQ(result.W, 4.0f);
}

TEST(QuaternionTest, ConjugateInPlaceNegatesVectorPart)
{
    Quaternion q(1.0f, -2.0f, 3.0f, 5.0f);
    q.Conjugate();
    EXPECT_FLOAT_EQ(q.X, -1.0f);
    EXPECT_FLOAT_EQ(q.Y, 2.0f);
    EXPECT_FLOAT_EQ(q.Z, -3.0f);
    EXPECT_FLOAT_EQ(q.W, 5.0f);
}

// --- Dot ---

TEST(QuaternionTest, DotOfIdentityWithItselfIsOne)
{
    EXPECT_NEAR(Quaternion::Dot(Quaternion::Identity, Quaternion::Identity), 1.0f, kEps);
}

TEST(QuaternionTest, DotProductValue)
{
    Quaternion a(1.0f, 0.0f, 0.0f, 0.0f);
    Quaternion b(0.0f, 1.0f, 0.0f, 0.0f);
    EXPECT_NEAR(Quaternion::Dot(a, b), 0.0f, kEps);
}

// --- CreateFromAxisAngle ---

TEST(QuaternionTest, CreateFromAxisAngleZeroAngleIsIdentity)
{
    Quaternion q = Quaternion::CreateFromAxisAngle(Vector3::UnitZ, 0.0f);
    EXPECT_NEAR(q.X, 0.0f, kEps);
    EXPECT_NEAR(q.Y, 0.0f, kEps);
    EXPECT_NEAR(q.Z, 0.0f, kEps);
    EXPECT_NEAR(q.W, 1.0f, kEps);
}

TEST(QuaternionTest, CreateFromAxisAngleHalfTurnAroundZ)
{
    // angle = π → sin(π/2)=1, cos(π/2)≈0
    Quaternion q = Quaternion::CreateFromAxisAngle(Vector3::UnitZ, static_cast<float>(M_PI));
    EXPECT_NEAR(q.X, 0.0f, kEps);
    EXPECT_NEAR(q.Y, 0.0f, kEps);
    EXPECT_NEAR(q.Z, 1.0f, kEps);
    EXPECT_NEAR(q.W, 0.0f, kEps);
}

TEST(QuaternionTest, CreateFromAxisAngleResultIsUnitLength)
{
    Quaternion q = Quaternion::CreateFromAxisAngle(Vector3::UnitX, 1.2f);
    EXPECT_NEAR(q.Length(), 1.0f, kEps);
}

// --- Multiply ---

TEST(QuaternionTest, MultiplyByIdentityPreservesQuaternion)
{
    Quaternion q(0.5f, 0.5f, 0.5f, 0.5f);
    Quaternion result = Quaternion::Multiply(Quaternion::Identity, q);
    EXPECT_NEAR(result.X, q.X, kEps);
    EXPECT_NEAR(result.Y, q.Y, kEps);
    EXPECT_NEAR(result.Z, q.Z, kEps);
    EXPECT_NEAR(result.W, q.W, kEps);
}

TEST(QuaternionTest, MultiplyQuaternionByItsConjugateGivesScalar)
{
    // q * q* = (0, 0, 0, |q|^2)
    Quaternion q = Quaternion::CreateFromAxisAngle(Vector3::UnitY, 0.7f);
    Quaternion conj = Quaternion::Conjugate(q);
    Quaternion result = Quaternion::Multiply(q, conj);
    EXPECT_NEAR(result.X, 0.0f, kEps);
    EXPECT_NEAR(result.Y, 0.0f, kEps);
    EXPECT_NEAR(result.Z, 0.0f, kEps);
    EXPECT_NEAR(result.W, 1.0f, kEps); // unit quat
}

// --- Inverse ---

TEST(QuaternionTest, InverseOfIdentityIsIdentity)
{
    Quaternion result = Quaternion::Inverse(Quaternion::Identity);
    EXPECT_NEAR(result.W, 1.0f, kEps);
    EXPECT_NEAR(result.X, 0.0f, kEps);
}

TEST(QuaternionTest, QuaternionTimesItsInverseIsIdentity)
{
    Quaternion q = Quaternion::CreateFromAxisAngle(Vector3::UnitX, 1.0f);
    Quaternion inv = Quaternion::Inverse(q);
    Quaternion result = Quaternion::Multiply(q, inv);
    EXPECT_NEAR(result.W, 1.0f, kEps);
    EXPECT_NEAR(result.X, 0.0f, kEps);
    EXPECT_NEAR(result.Y, 0.0f, kEps);
    EXPECT_NEAR(result.Z, 0.0f, kEps);
}

// --- Lerp / Slerp ---

TEST(QuaternionTest, LerpAtZeroReturnsFirst)
{
    Quaternion q1 = Quaternion::CreateFromAxisAngle(Vector3::UnitX, 0.0f);
    Quaternion q2 = Quaternion::CreateFromAxisAngle(Vector3::UnitX, static_cast<float>(M_PI));
    Quaternion result = Quaternion::Lerp(q1, q2, 0.0f);
    EXPECT_NEAR(result.W, q1.W, kEps);
}

TEST(QuaternionTest, LerpResultIsUnitLength)
{
    Quaternion q1 = Quaternion::CreateFromAxisAngle(Vector3::UnitX, 0.0f);
    Quaternion q2 = Quaternion::CreateFromAxisAngle(Vector3::UnitZ, 1.0f);
    Quaternion result = Quaternion::Lerp(q1, q2, 0.5f);
    EXPECT_NEAR(result.Length(), 1.0f, kEps);
}

TEST(QuaternionTest, SlerpResultIsUnitLength)
{
    Quaternion q1 = Quaternion::CreateFromAxisAngle(Vector3::UnitY, 0.0f);
    Quaternion q2 = Quaternion::CreateFromAxisAngle(Vector3::UnitY, 1.0f);
    Quaternion result = Quaternion::Slerp(q1, q2, 0.5f);
    EXPECT_NEAR(result.Length(), 1.0f, kEps);
}

// --- Operators ---

TEST(QuaternionTest, EqualityOperator)
{
    Quaternion a(1.0f, 2.0f, 3.0f, 4.0f);
    Quaternion b(1.0f, 2.0f, 3.0f, 4.0f);
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
}

TEST(QuaternionTest, InequalityOperator)
{
    Quaternion a(1.0f, 2.0f, 3.0f, 4.0f);
    Quaternion b(1.0f, 2.0f, 3.0f, 5.0f);
    EXPECT_FALSE(a == b);
    EXPECT_TRUE(a != b);
}

TEST(QuaternionTest, NegationOperator)
{
    Quaternion result = -Quaternion(1.0f, -2.0f, 3.0f, -4.0f);
    EXPECT_FLOAT_EQ(result.X, -1.0f);
    EXPECT_FLOAT_EQ(result.Y, 2.0f);
    EXPECT_FLOAT_EQ(result.Z, -3.0f);
    EXPECT_FLOAT_EQ(result.W, 4.0f);
}

// --- CreateFromRotationMatrix round-trip ---

TEST(QuaternionTest, CreateFromIdentityMatrixGivesIdentityQuaternion)
{
    Quaternion q = Quaternion::CreateFromRotationMatrix(Matrix::getIdentityProperty());
    EXPECT_NEAR(q.X, 0.0f, kEps);
    EXPECT_NEAR(q.Y, 0.0f, kEps);
    EXPECT_NEAR(q.Z, 0.0f, kEps);
    EXPECT_NEAR(q.W, 1.0f, kEps);
}
