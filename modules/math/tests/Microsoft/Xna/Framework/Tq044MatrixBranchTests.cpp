// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Plane.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
using namespace Microsoft::Xna::Framework;

namespace {
constexpr std::array<float Matrix::*,16> Tq044MatrixBranchTestsFields = {
    &Matrix::M11,&Matrix::M12,&Matrix::M13,&Matrix::M14,
    &Matrix::M21,&Matrix::M22,&Matrix::M23,&Matrix::M24,
    &Matrix::M31,&Matrix::M32,&Matrix::M33,&Matrix::M34,
    &Matrix::M41,&Matrix::M42,&Matrix::M43,&Matrix::M44};
void Tq044MatrixBranchTestsExpectMatrix(const Matrix& actual, const Matrix& expected) {
    for (const auto f : Tq044MatrixBranchTestsFields) EXPECT_EQ(actual.*f, expected.*f);
}
}

TEST(Tq044MatrixBranchTest, LookAtRotatedUpValue)
{
    // Oracle: XNA Matrix.CreateLookAt, transpose of right=-Y, up=X, back=Z; translation=-dot(axis,p).
    const Vector3 p(3,4,5), target(3,4,4), up(1,0,0);
    const Matrix actual=Matrix::CreateLookAt(p,target,up);
    Tq044MatrixBranchTestsExpectMatrix(actual,Matrix(0,1,0,0, -1,0,0,0, 0,0,1,0, 4,-3,-5,1));
}

TEST(Tq044MatrixBranchTest, WorldRotatedUpValue)
{
    // Oracle: XNA Matrix.CreateWorld: right=-Y, up=X, back=Z are rows, position is row 4.
    const Vector3 p(3,4,5), forward(0,0,-2), up(1,0,0);
    const Matrix actual=Matrix::CreateWorld(p,forward,up);
    Tq044MatrixBranchTestsExpectMatrix(actual,Matrix(0,-1,0,0, 1,0,0,0, 0,0,1,0, 3,4,5,1));
}

TEST(Tq044MatrixBranchTest, ConstrainedUsableObjectForwardValue)
{
    // Oracle: XNA parallel-axis branch: Up cross Right=-Z; -Z cross Up=Right.
    const Vector3 p(3,4,5), camera(3,3,5);
    const Matrix actual=Matrix::CreateConstrainedBillboard(p,camera,Vector3::Up,std::nullopt,Vector3::Right);
    Tq044MatrixBranchTestsExpectMatrix(actual,Matrix(0,0,-1,0, 0,1,0,0, 1,0,0,0, 3,4,5,1));
}

TEST(Tq044MatrixBranchTest, ConstrainedCoincidentCameraForwardValue)
{
    // Oracle: XNA negates camera forward (Down -> Up) before selecting the object-forward fallback.
    const Vector3 p(3,4,5);
    const Matrix actual=Matrix::CreateConstrainedBillboard(p,p,Vector3::Up,Vector3::Down,Vector3::Right);
    Tq044MatrixBranchTestsExpectMatrix(actual,Matrix(0,0,-1,0, 0,1,0,0, 1,0,0,0, 3,4,5,1));
}

TEST(Tq044MatrixBranchTest, ConstrainedForwardAxisDefaultValue)
{
    // Oracle: XNA default forward is parallel to rotate axis; secondary fallback is Right.
    const Vector3 p(0,0,-1);
    const Matrix actual=Matrix::CreateConstrainedBillboard(p,Vector3::Zero,Vector3::Forward,std::nullopt,std::nullopt);
    Tq044MatrixBranchTestsExpectMatrix(actual,Matrix(0,-1,0,0, 0,0,-1,0, 1,0,0,0, 0,0,-1,1));
}

TEST(Tq044MatrixBranchTest, BillboardInsideDistanceThresholdValue)
{
    // Oracle: (1/128)^2=1/16384 < Single(0.0001), so XNA uses Forward.
    const Vector3 p(0,0,1.0f/128);
    const Matrix actual=Matrix::CreateBillboard(p,Vector3::Zero,Vector3::Up,std::nullopt);
    Tq044MatrixBranchTestsExpectMatrix(actual,Matrix(-1,0,0,0, 0,1,0,0, 0,0,-1,0, 0,0,1.0f/128,1));
}

TEST(Tq044MatrixBranchTest, BillboardOutsideDistanceThresholdValue)
{
    // Oracle: (1/64)^2=1/4096 > Single(0.0001), so optional forward is ignored.
    const Vector3 p(0,0,1.0f/64);
    const Matrix actual=Matrix::CreateBillboard(p,Vector3::Zero,Vector3::Up,Vector3::Backward);
    Tq044MatrixBranchTestsExpectMatrix(actual,Matrix(1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,1.0f/64,1));
}

TEST(Tq044MatrixBranchTest, LookAtRotatedUpOut)
{
    // Oracle: XNA Matrix.CreateLookAt, transpose of right=-Y, up=X, back=Z; translation=-dot(axis,p).
    const Vector3 p(3,4,5), target(3,4,4), up(1,0,0);
    Matrix actual(99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99);
    Matrix::CreateLookAt(p,target,up,actual);
    Tq044MatrixBranchTestsExpectMatrix(actual,Matrix(0,1,0,0, -1,0,0,0, 0,0,1,0, 4,-3,-5,1));
}

TEST(Tq044MatrixBranchTest, WorldRotatedUpOut)
{
    // Oracle: XNA Matrix.CreateWorld: right=-Y, up=X, back=Z are rows, position is row 4.
    const Vector3 p(3,4,5), forward(0,0,-2), up(1,0,0);
    Matrix actual(99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99);
    Matrix::CreateWorld(p,forward,up,actual);
    Tq044MatrixBranchTestsExpectMatrix(actual,Matrix(0,-1,0,0, 1,0,0,0, 0,0,1,0, 3,4,5,1));
}

TEST(Tq044MatrixBranchTest, ConstrainedUsableObjectForwardOut)
{
    // Oracle: XNA parallel-axis branch: Up cross Right=-Z; -Z cross Up=Right.
    const Vector3 p(3,4,5), camera(3,3,5);
    Matrix actual(99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99);
    Matrix::CreateConstrainedBillboard(p,camera,Vector3::Up,std::nullopt,Vector3::Right,actual);
    Tq044MatrixBranchTestsExpectMatrix(actual,Matrix(0,0,-1,0, 0,1,0,0, 1,0,0,0, 3,4,5,1));
}

TEST(Tq044MatrixBranchTest, ConstrainedCoincidentCameraForwardOut)
{
    // Oracle: XNA negates camera forward (Down -> Up) before selecting the object-forward fallback.
    const Vector3 p(3,4,5);
    Matrix actual(99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99);
    Matrix::CreateConstrainedBillboard(p,p,Vector3::Up,Vector3::Down,Vector3::Right,actual);
    Tq044MatrixBranchTestsExpectMatrix(actual,Matrix(0,0,-1,0, 0,1,0,0, 1,0,0,0, 3,4,5,1));
}

TEST(Tq044MatrixBranchTest, ConstrainedForwardAxisDefaultOut)
{
    // Oracle: XNA default forward is parallel to rotate axis; secondary fallback is Right.
    const Vector3 p(0,0,-1);
    Matrix actual(99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99);
    Matrix::CreateConstrainedBillboard(p,Vector3::Zero,Vector3::Forward,std::nullopt,std::nullopt,actual);
    Tq044MatrixBranchTestsExpectMatrix(actual,Matrix(0,-1,0,0, 0,0,-1,0, 1,0,0,0, 0,0,-1,1));
}

TEST(Tq044MatrixBranchTest, BillboardInsideDistanceThresholdOut)
{
    // Oracle: (1/128)^2=1/16384 < Single(0.0001), so XNA uses Forward.
    const Vector3 p(0,0,1.0f/128);
    Matrix actual(99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99);
    Matrix::CreateBillboard(p,Vector3::Zero,Vector3::Up,std::nullopt,actual);
    Tq044MatrixBranchTestsExpectMatrix(actual,Matrix(-1,0,0,0, 0,1,0,0, 0,0,-1,0, 0,0,1.0f/128,1));
}

TEST(Tq044MatrixBranchTest, BillboardOutsideDistanceThresholdOut)
{
    // Oracle: (1/64)^2=1/4096 > Single(0.0001), so optional forward is ignored.
    const Vector3 p(0,0,1.0f/64);
    Matrix actual(99,99,99,99,99,99,99,99,99,99,99,99,99,99,99,99);
    Matrix::CreateBillboard(p,Vector3::Zero,Vector3::Up,Vector3::Backward,actual);
    Tq044MatrixBranchTestsExpectMatrix(actual,Matrix(1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,1.0f/64,1));
}

TEST(Tq044MatrixBranchTest, ReflectionObliquePlaneValue)
{
    // Oracle: n=(2,1,2)/3,d=-1; reflection I-2*n*n^T, translation=-2*d*n.
    // Shadow: dot(n,L)=1; upper block dot*I-n*L^T, row4=-d*L.
    // 5e-7 absolute allows <5 binary32 rounding units in normalization and products.
    const Matrix actual=Matrix::CreateReflection(Plane(Vector3(2,1,2),-3));
    const Matrix expected=Matrix(1.0f/9,-4.0f/9,-8.0f/9,0, -4.0f/9,7.0f/9,-4.0f/9,0, -8.0f/9,-4.0f/9,1.0f/9,0, 4.0f/3,2.0f/3,4.0f/3,1);
    for (const auto f : Tq044MatrixBranchTestsFields) EXPECT_NEAR(actual.*f,expected.*f,5e-7f);
}

TEST(Tq044MatrixBranchTest, ShadowObliquePlaneValue)
{
    // Oracle: n=(2,1,2)/3,d=-1; reflection I-2*n*n^T, translation=-2*d*n.
    // Shadow: dot(n,L)=1; upper block dot*I-n*L^T, row4=-d*L.
    // 5e-7 absolute allows <5 binary32 rounding units in normalization and products.
    const Matrix actual=Matrix::CreateShadow(Vector3(3,-3,0),Plane(Vector3(2,1,2),-3));
    const Matrix expected=Matrix(-1,2,0,0, -1,2,0,0, -2,2,1,0, 3,-3,0,1);
    for (const auto f : Tq044MatrixBranchTestsFields) EXPECT_NEAR(actual.*f,expected.*f,5e-7f);
}

TEST(Tq044MatrixBranchTest, ReflectionObliquePlaneOut)
{
    // Oracle: n=(2,1,2)/3,d=-1; reflection I-2*n*n^T, translation=-2*d*n.
    // Shadow: dot(n,L)=1; upper block dot*I-n*L^T, row4=-d*L.
    // 5e-7 absolute allows <5 binary32 rounding units in normalization and products.
    Matrix actual;
    Matrix::CreateReflection(Plane(Vector3(2,1,2),-3),actual);
    const Matrix expected=Matrix(1.0f/9,-4.0f/9,-8.0f/9,0, -4.0f/9,7.0f/9,-4.0f/9,0, -8.0f/9,-4.0f/9,1.0f/9,0, 4.0f/3,2.0f/3,4.0f/3,1);
    for (const auto f : Tq044MatrixBranchTestsFields) EXPECT_NEAR(actual.*f,expected.*f,5e-7f);
}

TEST(Tq044MatrixBranchTest, ShadowObliquePlaneOut)
{
    // Oracle: n=(2,1,2)/3,d=-1; reflection I-2*n*n^T, translation=-2*d*n.
    // Shadow: dot(n,L)=1; upper block dot*I-n*L^T, row4=-d*L.
    // 5e-7 absolute allows <5 binary32 rounding units in normalization and products.
    Matrix actual;
    Matrix::CreateShadow(Vector3(3,-3,0),Plane(Vector3(2,1,2),-3),actual);
    const Matrix expected=Matrix(-1,2,0,0, -1,2,0,0, -2,2,1,0, 3,-3,0,1);
    for (const auto f : Tq044MatrixBranchTestsFields) EXPECT_NEAR(actual.*f,expected.*f,5e-7f);
}
