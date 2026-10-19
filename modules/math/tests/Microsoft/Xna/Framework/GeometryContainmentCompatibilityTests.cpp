// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/BoundingFrustum.hpp"
#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/ContainmentType.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include <bit>
#include <cmath>
#include <cstdint>
#include <gtest/gtest.h>
#include <limits>

using namespace Microsoft::Xna::Framework;

namespace
{
template <class Check> void ForSquaredArithmeticSamples(Check check)
{
    std::uint32_t state = 0x12345678;
    const auto sample = [&state]() {
        state = state * 1664525U + 1013904223U;
        return (static_cast<int>(state >> 8) % 8193 - 4096) / 1024.0f;
    };
    const auto vector = [&sample](float scale) {
        const float x = sample() * scale, y = sample() * scale, z = sample() * scale;
        return Vector3(x, y, z);
    };
    for (float scale : {1.0f, 0.00001f, 100000.0f, 1e20f, 1e-20f})
        for (int i = 0; i < 256; ++i)
        {
            const auto center = vector(scale), point = vector(scale);
            const double x = static_cast<double>(point.X) - center.X;
            const double y = static_cast<double>(point.Y) - center.Y;
            const double z = static_cast<double>(point.Z) - center.Z;
            check(center, point, x * x + y * y + z * z);
        }
}
} // namespace

TEST(GeometryContainmentCompatibilityTest, SquaredVectorArithmeticRetainsWideAccumulation)
{
    // SphereSquaredArithmeticOracle.cs: 19,200 genuine method-entry checks.
    ForSquaredArithmeticSamples([](const Vector3 &center, const Vector3 &point, double wide) {
        const float expected = static_cast<float>(wide);
        EXPECT_EQ(Vector3::DistanceSquared(point, center), expected);
        float output;
        Vector3::DistanceSquared(point, center, output);
        EXPECT_EQ(output, expected);
        auto alias = point;
        Vector3::DistanceSquared(alias, center, alias.X);
        EXPECT_EQ(alias.X, expected);
        const double norm = static_cast<double>(point.X) * point.X + static_cast<double>(point.Y) * point.Y +
                            static_cast<double>(point.Z) * point.Z;
        EXPECT_EQ(point.LengthSquared(), static_cast<float>(norm));
    });
}

TEST(GeometryContainmentCompatibilityTest, SpherePointOverloadsRetainTheirMeasuredStorageBoundary)
{
    ForSquaredArithmeticSamples([](const Vector3 &center, const Vector3 &point, double wide) {
        const float stored = static_cast<float>(wide);
        const float baseRadius = static_cast<float>(std::sqrt(wide));
        const auto bits = std::bit_cast<std::uint32_t>(baseRadius);
        for (int delta : {-1, 0, 1})
        {
            const float radius = std::bit_cast<float>(bits + delta);
            const BoundingSphere sphere(center, radius), other(point, 0);
            const double radiusSquared = static_cast<double>(radius) * radius;
            EXPECT_EQ(sphere.Contains(point),
                      wide < radiusSquared ? ContainmentType::Contains : ContainmentType::Disjoint);
            ContainmentType output;
            sphere.Contains(point, output);
            EXPECT_EQ(output, stored < radiusSquared ? ContainmentType::Contains : ContainmentType::Disjoint);
            EXPECT_EQ(sphere.Intersects(other), radiusSquared > stored);
            bool hit;
            sphere.Intersects(other, hit);
            EXPECT_EQ(hit, radiusSquared > stored);
        }
    });
}

// Measured against genuine XNA in cna-demos geometry/ContainmentOracle.cs.
// This is separate from ray/frustum intersection, reserved for the human author.
TEST(GeometryContainmentCompatibilityTest, SpherePointExcludesSurfaceAndUnorderedDistance)
{
    const BoundingSphere sphere(Vector3::Zero, 5);
    for (float x : {-6.0f, -5.0f, -4.0f, 0.0f, 4.0f, 5.0f, 6.0f, std::numeric_limits<float>::quiet_NaN(),
                    std::numeric_limits<float>::infinity()})
    {
        const Vector3 point(x, 0, 0);
        const auto expected = std::abs(x) < 5 ? ContainmentType::Contains : ContainmentType::Disjoint;
        ContainmentType output;
        sphere.Contains(point, output);
        EXPECT_EQ(sphere.Contains(point), expected);
        EXPECT_EQ(output, expected);
    }
    EXPECT_EQ(BoundingSphere(Vector3::Zero, 0).Contains(Vector3::Zero), ContainmentType::Disjoint);
    EXPECT_EQ(BoundingSphere(Vector3::Zero, std::numeric_limits<float>::quiet_NaN()).Contains(Vector3::Zero),
              ContainmentType::Disjoint);
}

TEST(GeometryContainmentCompatibilityTest, SphereContainmentUsesSignedRadiusDifference)
{
    const BoundingSphere outer(Vector3::Zero, 5);
    for (float radius : {0, 1, 2, 5, 6, 10})
        for (float x : {0, 1, 3, 5, 6, 7, 10, 15, 16})
        {
            SCOPED_TRACE(::testing::Message() << "radius=" << radius << " distance=" << x);
            const BoundingSphere inner(Vector3(x, 0, 0), radius);
            const auto expected = x > 5 + radius    ? ContainmentType::Disjoint
                                  : x <= 5 - radius ? ContainmentType::Contains
                                                    : ContainmentType::Intersects;
            ContainmentType output;
            outer.Contains(inner, output);
            EXPECT_EQ(outer.Contains(inner), expected);
            EXPECT_EQ(output, expected);
        }
    const BoundingSphere unordered(Vector3::Zero, std::numeric_limits<float>::quiet_NaN());
    EXPECT_EQ(outer.Contains(unordered), ContainmentType::Disjoint);
}

TEST(GeometryContainmentCompatibilityTest, SphereIntersectionExcludesExternalTangency)
{
    const BoundingSphere outer(Vector3::Zero, 5);
    for (float radius : {0, 1, 2, 5, 6, 10})
        for (float x : {0, 1, 3, 5, 6, 7, 10, 15, 16})
        {
            const BoundingSphere inner(Vector3(x, 0, 0), radius);
            bool output;
            outer.Intersects(inner, output);
            EXPECT_EQ(outer.Intersects(inner), x < 5 + radius);
            EXPECT_EQ(output, x < 5 + radius);
        }
    EXPECT_FALSE(outer.Intersects(BoundingSphere(Vector3::Zero, std::numeric_limits<float>::quiet_NaN())));
}

TEST(GeometryContainmentCompatibilityTest, VolumeCornersRemainInclusiveDespiteStrictPointApi)
{
    const BoundingSphere sphere(Vector3::Zero, 5);
    const BoundingBox flatBox(Vector3(-3, -4, 0), Vector3(3, 4, 0));
    ContainmentType output;
    sphere.Contains(flatBox, output);
    EXPECT_EQ(sphere.Contains(flatBox), ContainmentType::Contains);
    EXPECT_EQ(output, ContainmentType::Contains);
    const BoundingFrustum frustum(Matrix::getIdentityProperty());
    EXPECT_EQ(BoundingSphere(Vector3(0, 0, 0.5f), 1.5f).Contains(frustum), ContainmentType::Contains);
}

TEST(GeometryContainmentCompatibilityTest, FrustumPointChecksAllPlanesAndXnaTolerance)
{
    const BoundingFrustum frustum(Matrix::getIdentityProperty());
    for (float x : {-2, -1, 0, 1, 2})
        for (float y : {-2, -1, 0, 1, 2})
            for (float z : {-1.0f, 0.0f, 0.5f, 1.0f, 2.0f})
            {
                const Vector3 point(x, y, z);
                const auto expected = std::abs(x) <= 1 && std::abs(y) <= 1 && z >= 0 && z <= 1
                                          ? ContainmentType::Contains
                                          : ContainmentType::Disjoint;
                ContainmentType output;
                frustum.Contains(point, output);
                EXPECT_EQ(frustum.Contains(point), expected);
                EXPECT_EQ(output, expected);
            }
    EXPECT_EQ(frustum.Contains(Vector3(1 + 0.5e-5f, 0, 0.5f)), ContainmentType::Contains);
    EXPECT_EQ(frustum.Contains(Vector3(1 + 2e-5f, 0, 0.5f)), ContainmentType::Disjoint);
}

TEST(GeometryContainmentCompatibilityTest, FrustumContainmentIsGeometricNotPointerIdentity)
{
    const BoundingFrustum frustum(Matrix::getIdentityProperty());
    EXPECT_EQ(frustum.Contains(frustum), ContainmentType::Contains);
    EXPECT_EQ(frustum.Contains(BoundingFrustum(Matrix::getIdentityProperty())), ContainmentType::Contains);
    const BoundingBox matchingBox(Vector3(-1, -1, 0), Vector3(1, 1, 1));
    EXPECT_EQ(matchingBox.Contains(frustum), ContainmentType::Contains);
    EXPECT_EQ(BoundingBox(Vector3(10), Vector3(11)).Contains(frustum), ContainmentType::Disjoint);
    EXPECT_EQ(BoundingSphere(Vector3(10), 1).Contains(frustum), ContainmentType::Disjoint);
}

TEST(GeometryContainmentCompatibilityTest, FrustumIntersectionRequiresActualConvexSeparation)
{
    const BoundingFrustum frustum(Matrix::getIdentityProperty());
    const BoundingSphere cornerMiss(Vector3(1.8f, 1.8f, 0.5f), 1);
    // XNA intentionally distinguishes conservative plane containment from GJK intersection.
    EXPECT_EQ(frustum.Contains(cornerMiss), ContainmentType::Intersects);
    EXPECT_FALSE(frustum.Intersects(cornerMiss));
    bool output;
    frustum.Intersects(cornerMiss, output);
    EXPECT_FALSE(output);
    EXPECT_EQ(cornerMiss.Contains(frustum), ContainmentType::Disjoint);
}

TEST(GeometryContainmentCompatibilityTest, SphereRadiusArithmeticRetainsWideComparisons)
{
    const float infinity = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float maximum = std::numeric_limits<float>::max();
    const float positions[] = {0, -0.0f, 1, 5, 1e20f, -1e20f, maximum, -maximum, infinity, nan};
    const float radii[] = {0, 1, 1e20f, maximum, infinity, nan, -1, -infinity};
    // Genuine reflected method-entry probe: 12,800 checks, including mutable
    // radii. Radius sums must not overflow to Single infinity before comparison.
    for (float x : positions)
        for (float y : positions)
            for (float a : radii)
                for (float b : radii)
                {
                    SCOPED_TRACE(::testing::Message()
                                 << "centers=" << x << ',' << y << " radii=" << a << ',' << b);
                    BoundingSphere outer, inner;
                    outer.Center = Vector3(x, 0, 0);
                    outer.Radius = a;
                    inner.Center = Vector3(y, 0, 0);
                    inner.Radius = b;
                    const float distance = Vector3::Distance(outer.Center, inner.Center);
                    const float square = Vector3::DistanceSquared(outer.Center, inner.Center);
                    const double da = a, db = b;
                    const auto expected = !(da + db >= distance)   ? ContainmentType::Disjoint
                                          : !(da - db >= distance) ? ContainmentType::Intersects
                                                                   : ContainmentType::Contains;
                    const bool expectedHit = da * da + 2.0 * da * db + db * db > square;
                    ContainmentType containment;
                    bool hit;
                    outer.Contains(inner, containment);
                    outer.Intersects(inner, hit);
                    EXPECT_EQ(outer.Contains(inner), expected);
                    EXPECT_EQ(containment, expected);
                    EXPECT_EQ(outer.Intersects(inner), expectedHit);
                    EXPECT_EQ(hit, expectedHit);
                }
}
