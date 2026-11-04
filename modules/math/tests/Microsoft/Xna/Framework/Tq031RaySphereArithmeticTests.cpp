// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <cmath>
#include <limits>
#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/Ray.hpp"
using namespace Microsoft::Xna::Framework;

TEST(Tq031RaySphereArithmeticTest, DistantSphereKeepsRadiusWhenProjectionSquaresCancel)
{
    // XNA Ray.Intersects(Sphere) value IL_00b9..00e7, out IL_00b7..00e1:
    // perpendicularSquared=distanceSquared-projection^2 is stored BEFORE r^2 is subtracted.
    // For these powers of two, perpendicularSquared=0 exactly, sqrt(1-0)=1, t=distance-1.
    const BoundingSphere sphere(Vector3::Zero,1);
    for (float distance : {4096.0f,8192.0f,16384.0f})
        for (const auto axis : {Vector3::UnitX,Vector3::UnitY,Vector3::UnitZ})
            for (float sign : {-1.0f,1.0f})
            {
                const Ray ray(axis*(-sign*distance),axis*sign);
                std::optional<float> output=99;
                ray.Intersects(sphere,output); EXPECT_EQ(output,distance-1);
                EXPECT_EQ(ray.Intersects(sphere),distance-1);
                output=99;
                sphere.Intersects(ray,output); EXPECT_EQ(output,distance-1);
                EXPECT_EQ(sphere.Intersects(ray),distance-1);
            }
}

TEST(Tq031RaySphereArithmeticTest, UnorderedSphereInputsReturnAnEngagedNanParameter)
{
    // XNA Ray.Intersects(Sphere): bgt.un at value IL_006e bypasses inside,
    // bge.un at 00ac and ble.un at 00c4 fall through to sqrt/subtraction on NaN.
    const float nan=std::numeric_limits<float>::quiet_NaN();
    const Ray ray(Vector3(-3,0,0),Vector3::UnitX);
    for (const BoundingSphere sphere : {BoundingSphere(Vector3::Zero,nan),BoundingSphere(Vector3(nan,0,0),1)})
    {
        const auto check=[](const std::optional<float>& result) {
            ASSERT_TRUE(result.has_value()); EXPECT_TRUE(std::isnan(*result));
        };
        check(ray.Intersects(sphere)); check(sphere.Intersects(ray));
        std::optional<float> output=99;
        ray.Intersects(sphere,output); check(output);
        output=99; sphere.Intersects(ray,output); check(output);
    }
    // The inside test wins before an unordered direction is examined (IL_006b..007a).
    EXPECT_EQ(Ray(Vector3::Zero,Vector3(nan)).Intersects(BoundingSphere(Vector3::Zero,1)),0);
}
