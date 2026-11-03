// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <cmath>
#include <limits>
#include <optional>
#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/Plane.hpp"
#include "Microsoft/Xna/Framework/Ray.hpp"

using namespace Microsoft::Xna::Framework;

TEST(RayBoundaryContractTest, PlaneTinyNegativeParameterDiffersBetweenValueAndOutOverloads)
{
    // XNA Ray.Intersects(Plane), IL_00bb–00da clamps [-1e-5,0).
    // Its out overload, IL_00c9–00e0, overwrites the clamp with the original t.
    const Plane plane(Vector3::UnitX, 0);
    for (float origin : {std::numeric_limits<float>::denorm_min(), 0.5e-5f, 1e-5f})
    {
        SCOPED_TRACE(origin);
        const Ray ray(Vector3(origin, 0, 0), Vector3::UnitX);
        std::optional<float> output = 99;
        ray.Intersects(plane, output);
        ASSERT_TRUE(output.has_value());
        EXPECT_EQ(*output, -origin);
        EXPECT_TRUE(std::signbit(*output));
        const auto value = ray.Intersects(plane);
        ASSERT_TRUE(value.has_value());
        EXPECT_EQ(*value, 0);
        EXPECT_FALSE(std::signbit(*value));
    }
    const Ray behind(Vector3(std::nextafter(1e-5f, 1.0f), 0, 0), Vector3::UnitX);
    std::optional<float> output = 99;
    behind.Intersects(plane, output);
    EXPECT_FALSE(output.has_value());
    EXPECT_FALSE(behind.Intersects(plane).has_value());
}

TEST(RayBoundaryContractTest, PlaneParallelThresholdIsStrictAndOutResultIsOverwrittenOnMiss)
{
    // XNA Ray.Intersects(Plane), IL_004b–0062 (out: IL_0048–005c): abs(den)<1e-5.
    const Plane plane(Vector3::UnitX, 0);
    for (float sign : {-1.0f, 1.0f})
    {
        const Ray parallel(Vector3(-sign, 0, 0),
            Vector3(sign * std::nextafter(1e-5f, 0.0f), 0, 0));
        std::optional<float> output = 99;
        parallel.Intersects(plane, output);
        EXPECT_FALSE(output.has_value());
        EXPECT_FALSE(parallel.Intersects(plane).has_value());
        const Ray threshold(Vector3(-sign, 0, 0), Vector3(sign * 1e-5f, 0, 0));
        threshold.Intersects(plane, output);
        EXPECT_EQ(output, 100000.0f); // t = 1 / 1e-5.
        EXPECT_EQ(threshold.Intersects(plane), 100000.0f);
    }
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const Ray unordered(Vector3::Zero, Vector3(nan, 0, 0));
    std::optional<float> output = 99;
    unordered.Intersects(plane, output);
    ASSERT_TRUE(output.has_value());
    EXPECT_TRUE(std::isnan(*output));
    const auto value = unordered.Intersects(plane);
    ASSERT_TRUE(value.has_value());
    EXPECT_TRUE(std::isnan(*value)); // XNA unordered branches fall through to division.
}

TEST(RayBoundaryContractTest, SphereBoundaryOriginReturnsZeroEvenForOutwardOrStationaryDirection)
{
    // XNA Ray.Intersects(BoundingSphere), value IL_006b–007a, out IL_0066–007b:
    // distanceSquared <= radiusSquared precedes the direction/projection tests.
    const BoundingSphere sphere(Vector3::Zero, 1);
    for (const auto axis : {Vector3::UnitX, Vector3::UnitY, Vector3::UnitZ})
        for (float sign : {-1.0f, 1.0f})
            for (const auto direction : {axis, -axis, Vector3::Zero})
            {
                const Ray ray(axis * sign, direction);
                std::optional<float> output = 99;
                ray.Intersects(sphere, output);
                EXPECT_EQ(output, 0);
                EXPECT_EQ(ray.Intersects(sphere), 0);
                output = 99;
                sphere.Intersects(ray, output);
                EXPECT_EQ(output, 0);
                EXPECT_EQ(sphere.Intersects(ray), 0);
            }
    const BoundingSphere point(Vector3(2, 3, 4), 0);
    const Ray stationary(point.Center, Vector3::Zero);
    EXPECT_EQ(stationary.Intersects(point), 0);
    std::optional<float> output = 99;
    stationary.Intersects(point, output);
    EXPECT_EQ(output, 0);
}

TEST(RayBoundaryContractTest, SphereProjectionDiscriminantAndTangentBranchesHaveLiteralParameters)
{
    // XNA Ray.Intersects(BoundingSphere), IL_00a6–00e7; FNA Ray.cs projection formula.
    const BoundingSphere sphere(Vector3::Zero, 1);
    struct Sample { Vector3 origin, direction; std::optional<float> parameter; };
    const Sample cases[] = {
        {{-3, 1, 0}, {1, 0, 0}, 3}, // Tangent: projection=3, perpendicular distance=1.
        {{-3, 2, 0}, {1, 0, 0}, std::nullopt}, // Negative discriminant.
        {{-3, 0, 0}, {-1, 0, 0}, std::nullopt}, // Sphere behind the ray.
        {{-3, 0, 0}, {0, 0, 0}, std::nullopt},
        {{0, 0, 0}, {0, 0, 0}, 0},
        {{-5, 0, 0}, {1, 0, 0}, 4},
    };
    for (const auto& sample : cases)
    {
        SCOPED_TRACE(sample.origin.ToString() + sample.direction.ToString());
        const Ray ray(sample.origin, sample.direction);
        std::optional<float> output = 99;
        ray.Intersects(sphere, output);
        EXPECT_EQ(output, sample.parameter);
        EXPECT_EQ(ray.Intersects(sphere), sample.parameter);
        output = 99;
        sphere.Intersects(ray, output);
        EXPECT_EQ(output, sample.parameter);
        EXPECT_EQ(sphere.Intersects(ray), sample.parameter);
    }
}

TEST(RayBoundaryContractTest, BoxClipsEveryAxisBothSignsAndRejectsDisjointIntervals)
{
    // XNA BoundingBox.Intersects(Ray), IL_0000–0293: nonnegative slab interval.
    // [-1,1]^3: start at +/-3, speed 2 toward a face => t=1; away => no hit.
    const BoundingBox box(Vector3(-1), Vector3(1));
    for (const auto axis : {Vector3::UnitX, Vector3::UnitY, Vector3::UnitZ})
        for (float sign : {-1.0f, 1.0f})
            for (bool toward : {false, true})
            {
                const Ray ray(axis * (3 * sign), axis * ((toward ? -2 : 2) * sign));
                const std::optional<float> expected = toward ? std::optional<float>(1) : std::nullopt;
                std::optional<float> output = 99;
                ray.Intersects(box, output);
                EXPECT_EQ(output, expected);
                EXPECT_EQ(ray.Intersects(box), expected);
                output = 99;
                box.Intersects(ray, output);
                EXPECT_EQ(output, expected);
                EXPECT_EQ(box.Intersects(ray), expected);
            }
    const Ray corner(Vector3(-2, 0, 0), Vector3(1, 1, 1));
    EXPECT_EQ(corner.Intersects(box), 1); // Slabs share exactly t=1 at (-1,1,1).
    std::optional<float> output = 99;
    corner.Intersects(box, output);
    EXPECT_EQ(output, 1);
    for (const auto direction : {Vector3(1, 2, 0), Vector3(1, 0, 2)})
    {
        const Ray miss(Vector3(-2, 0, 0), direction);
        output = 99;
        miss.Intersects(box, output);
        EXPECT_FALSE(output.has_value()); // Y/Z exits at .5 before X enters at 1.
        EXPECT_FALSE(miss.Intersects(box).has_value());
    }
}
