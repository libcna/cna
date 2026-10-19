// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/BoundingFrustum.hpp"
#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Plane.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Ray.hpp"
#include "System/ArgumentException.hpp"

using namespace Microsoft::Xna::Framework;

namespace
{
    void ExpectParameter(const std::optional<float>& actual, const std::optional<float>& expected)
    {
        ASSERT_EQ(actual.has_value(), expected.has_value());
        if (actual && expected)
            EXPECT_EQ(std::bit_cast<std::uint32_t>(*actual), std::bit_cast<std::uint32_t>(*expected));
    }

    void ExpectHit(const Ray& ray, const BoundingBox& box, std::optional<float> expected)
    {
        ExpectParameter(ray.Intersects(box), expected);
        std::optional<float> result;
        ray.Intersects(box, result);
        ExpectParameter(result, expected);
        ExpectParameter(box.Intersects(ray), expected);
        box.Intersects(ray, result);
        ExpectParameter(result, expected);
    }
}

TEST(GeometryValuesCompatibilityTest, SphereRejectsNegativeRadiusAndPreservesOtherValues)
{
    for (float radius : {-1.0f, -std::numeric_limits<float>::denorm_min(),
                         -std::numeric_limits<float>::infinity()})
        EXPECT_THROW((void)BoundingSphere(Vector3::Zero, radius), System::ArgumentException);
    for (float radius : {0.0f, -0.0f, 1.0f, std::numeric_limits<float>::max(),
                         std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
    {
        const BoundingSphere sphere(Vector3(1,2,3), radius);
        EXPECT_EQ(sphere.Center, Vector3(1,2,3));
        EXPECT_EQ(std::bit_cast<std::uint32_t>(sphere.Radius), std::bit_cast<std::uint32_t>(radius));
    }
    EXPECT_THROW((void)BoundingSphere::CreateFromPoints({}), System::ArgumentException);
    const auto singleton = BoundingSphere::CreateFromPoints({Vector3(3,-4,5)});
    EXPECT_EQ(singleton.Center, Vector3(3,-4,5));
    EXPECT_EQ(singleton.Radius, 0.0f);
}

TEST(GeometryValuesCompatibilityTest, StationaryRayHitsEveryContainedBoxBoundary)
{
    const BoundingBox box(Vector3(4,-1,-1), Vector3(6,1,1));
    for (float x : {3,4,5,6,7}) for (float y : {-2,-1,0,1,2}) for (float z : {-2,-1,0,1,2})
    {
        const bool inside = x >= 4 && x <= 6 && std::abs(y) <= 1 && std::abs(z) <= 1;
        const std::optional<float> expected = inside ? std::optional<float>(0.0f) : std::nullopt;
        ExpectHit(Ray(Vector3(x,y,z), Vector3::Zero), box, expected);
        ExpectHit(Ray(Vector3(x,y,z), Vector3(-0.0f)), box, expected);
    }
}

TEST(GeometryValuesCompatibilityTest, RayBoxUsesNonnegativeIntervalAndXnaParallelThreshold)
{
    const BoundingBox box(Vector3(4,-1,-1), Vector3(6,1,1));
    ExpectHit(Ray(Vector3::Zero, Vector3::UnitX), box, 4);
    ExpectHit(Ray(Vector3::Zero, Vector3(2,0,0)), box, 2);
    ExpectHit(Ray(Vector3(4,0,0), -Vector3::UnitX), box, 0);
    ExpectHit(Ray(Vector3(6,0,0), Vector3::UnitX), box, 0);
    ExpectHit(Ray(Vector3(5,0,0), -Vector3::UnitX), box, 0);
    ExpectHit(Ray(Vector3::Zero, -Vector3::UnitX), box, std::nullopt);
    ExpectHit(Ray(Vector3::Zero, Vector3(0.5e-6f,0,0)), box, std::nullopt);
    ExpectHit(Ray(Vector3(5,0,0), Vector3(0.5e-6f,0,0)), box, 0);
    ExpectHit(Ray(Vector3::Zero, Vector3(1e-6f,0,0)), box, 4000000);
    ExpectHit(Ray(Vector3(0,2,0), Vector3::UnitX), box, std::nullopt);
}

TEST(GeometryValuesCompatibilityTest, BoxAndRayUseOneLiteralOuterBracePair)
{
    EXPECT_EQ(BoundingBox(Vector3(-3,-1,-6), Vector3(2,4,2)).ToString(),
        "{Min:{X:-3 Y:-1 Z:-6} Max:{X:2 Y:4 Z:2}}");
    EXPECT_EQ(Ray(Vector3(-1,2,-3), Vector3(4,-5,6)).ToString(),
        "{Position:{X:-1 Y:2 Z:-3} Direction:{X:4 Y:-5 Z:6}}");
}

TEST(GeometryValuesCompatibilityTest, IdentityFrustumPlaneBitsAndTextMatchXna)
{
    const BoundingFrustum frustum(Matrix::getIdentityProperty());
    const std::array<Plane,6> planes{frustum.getNearProperty(),frustum.getFarProperty(),
        frustum.getLeftProperty(),frustum.getRightProperty(),frustum.getTopProperty(),frustum.getBottomProperty()};
    // Genuine XNA retains seven negative-zero fields, but formats them as "0".
    const float expected[6][4] = {{-0.0f,-0.0f,-1,-0.0f},{0,0,1,-1},{-1,-0.0f,-0.0f,-1},
        {1,0,0,-1},{0,1,0,-1},{-0.0f,-1,-0.0f,-1}};
    for (std::size_t i=0;i<planes.size();++i)
    {
        const float fields[]{planes[i].Normal.X,planes[i].Normal.Y,planes[i].Normal.Z,planes[i].D};
        for (int j=0;j<4;++j)
            EXPECT_EQ(std::bit_cast<std::uint32_t>(fields[j]),std::bit_cast<std::uint32_t>(expected[i][j]));
    }
    EXPECT_EQ(frustum.ToString(),
        "{Near:{Normal:{X:0 Y:0 Z:-1} D:0} Far:{Normal:{X:0 Y:0 Z:1} D:-1} "
        "Left:{Normal:{X:-1 Y:0 Z:0} D:-1} Right:{Normal:{X:1 Y:0 Z:0} D:-1} "
        "Top:{Normal:{X:0 Y:1 Z:0} D:-1} Bottom:{Normal:{X:0 Y:-1 Z:0} D:-1}}");
}

TEST(GeometryValuesCompatibilityTest, FloatTextHidesNegativeZeroWithoutMutatingValues)
{
    const float nz = std::bit_cast<float>(std::uint32_t{0x80000000});
    EXPECT_EQ(Vector2(nz).ToString(),Vector2::Zero.ToString());
    const Vector3 vector(nz);
    EXPECT_EQ(vector.ToString(),Vector3::Zero.ToString());
    EXPECT_EQ(std::bit_cast<std::uint32_t>(vector.X),std::uint32_t{0x80000000});
    EXPECT_EQ(Vector4(nz).ToString(),Vector4::Zero.ToString());
    EXPECT_EQ(Quaternion(nz,nz,nz,nz).ToString(),Quaternion().ToString());
    EXPECT_EQ(Plane(nz,nz,nz,nz).ToString(),Plane().ToString());
    EXPECT_EQ(BoundingSphere(Vector3(nz),nz).ToString(),BoundingSphere().ToString());
    EXPECT_EQ(Matrix(nz,nz,nz,nz,nz,nz,nz,nz,nz,nz,nz,nz,nz,nz,nz,nz).ToString(),Matrix().ToString());
}
