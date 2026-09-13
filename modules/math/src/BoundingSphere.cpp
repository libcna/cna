// SPDX-License-Identifier: MS-PL

#include "Microsoft/Xna/Framework/BoundingSphere.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <sstream>
#include <stdexcept>

#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/BoundingFrustum.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Plane.hpp"
#include "Microsoft/Xna/Framework/Ray.hpp"

namespace Microsoft::Xna::Framework
{
    BoundingSphere::BoundingSphere(Vector3 center, float radius)
        : Center(center), Radius(radius)
    {
    }

    BoundingSphere BoundingSphere::Transform(Matrix matrix) const
    {
        BoundingSphere result;
        Transform(matrix, result);
        return result;
    }

    void BoundingSphere::Transform(const Matrix& matrix, BoundingSphere& result) const
    {
        result.Center = Vector3::Transform(Center, matrix);
        result.Radius = Radius * std::sqrt(std::max(
            (matrix.M11 * matrix.M11) + (matrix.M12 * matrix.M12) + (matrix.M13 * matrix.M13),
            std::max(
                (matrix.M21 * matrix.M21) + (matrix.M22 * matrix.M22) + (matrix.M23 * matrix.M23),
                (matrix.M31 * matrix.M31) + (matrix.M32 * matrix.M32) + (matrix.M33 * matrix.M33)
            )
        ));
    }

    void BoundingSphere::Contains(const BoundingBox& box, ContainmentType& result) const
    {
        result = Contains(box);
    }

    void BoundingSphere::Contains(const BoundingSphere& sphere, ContainmentType& result) const
    {
        result = Contains(sphere);
    }

    void BoundingSphere::Contains(const Vector3& point, ContainmentType& result) const
    {
        result = Contains(point);
    }

    ContainmentType BoundingSphere::Contains(BoundingBox box) const
    {
        bool inside = true;
        for (const Vector3& corner : box.GetCorners())
        {
            if (Contains(corner) == ContainmentType::Disjoint)
            {
                inside = false;
                break;
            }
        }

        if (inside)
        {
            return ContainmentType::Contains;
        }

        double dmin = 0.0;

        if (Center.X < box.Min.X)
        {
            dmin += (Center.X - box.Min.X) * (Center.X - box.Min.X);
        }
        else if (Center.X > box.Max.X)
        {
            dmin += (Center.X - box.Max.X) * (Center.X - box.Max.X);
        }

        if (Center.Y < box.Min.Y)
        {
            dmin += (Center.Y - box.Min.Y) * (Center.Y - box.Min.Y);
        }
        else if (Center.Y > box.Max.Y)
        {
            dmin += (Center.Y - box.Max.Y) * (Center.Y - box.Max.Y);
        }

        if (Center.Z < box.Min.Z)
        {
            dmin += (Center.Z - box.Min.Z) * (Center.Z - box.Min.Z);
        }
        else if (Center.Z > box.Max.Z)
        {
            dmin += (Center.Z - box.Max.Z) * (Center.Z - box.Max.Z);
        }

        if (dmin <= Radius * Radius)
        {
            return ContainmentType::Intersects;
        }

        return ContainmentType::Disjoint;
    }

    ContainmentType BoundingSphere::Contains(const BoundingFrustum& frustum) const
    {
        bool inside = true;
        for (const Vector3& corner : frustum.GetCorners())
        {
            if (Contains(corner) == ContainmentType::Disjoint)
            {
                inside = false;
                break;
            }
        }

        if (inside)
        {
            return ContainmentType::Contains;
        }

        double dmin = 0.0;
        if (dmin <= Radius * Radius)
        {
            return ContainmentType::Intersects;
        }

        return ContainmentType::Disjoint;
    }

    ContainmentType BoundingSphere::Contains(BoundingSphere sphere) const
    {
        float sqDistance;
        Vector3::DistanceSquared(sphere.Center, Center, sqDistance);

        if (sqDistance > (sphere.Radius + Radius) * (sphere.Radius + Radius))
        {
            return ContainmentType::Disjoint;
        }
        if (sqDistance <= (Radius - sphere.Radius) * (Radius - sphere.Radius))
        {
            return ContainmentType::Contains;
        }
        return ContainmentType::Intersects;
    }

    ContainmentType BoundingSphere::Contains(Vector3 point) const
    {
        float sqRadius = Radius * Radius;
        float sqDistance;
        Vector3::DistanceSquared(point, Center, sqDistance);

        if (sqDistance > sqRadius)
        {
            return ContainmentType::Disjoint;
        }
        if (sqDistance < sqRadius)
        {
            return ContainmentType::Contains;
        }
        return ContainmentType::Intersects;
    }

    bool BoundingSphere::Equals(const BoundingSphere& other) const
    {
        return Center == other.Center && Radius == other.Radius;
    }

    BoundingSphere BoundingSphere::CreateFromBoundingBox(BoundingBox box)
    {
        BoundingSphere result;
        CreateFromBoundingBox(box, result);
        return result;
    }

    void BoundingSphere::CreateFromBoundingBox(const BoundingBox& box, BoundingSphere& result)
    {
        Vector3 center(
            (box.Min.X + box.Max.X) / 2.0f,
            (box.Min.Y + box.Max.Y) / 2.0f,
            (box.Min.Z + box.Max.Z) / 2.0f
        );

        float radius = Vector3::Distance(center, box.Max);
        result = BoundingSphere(center, radius);
    }

    BoundingSphere BoundingSphere::CreateFromFrustum(const BoundingFrustum& frustum)
    {
        return CreateFromPoints(frustum.GetCorners());
    }

    namespace
    {
        /**
         * @brief The squared length of a difference, accumulated the way XNA's own answer is.
         *
         * XNA 4.0 ships as a 32-bit assembly and its `Vector3` arithmetic runs on the x87 unit,
         * where `x*x + y*y + z*z` is accumulated at extended precision and rounded to `float`
         * only when the result is stored. Accumulating in `float` instead gives a different
         * `Length`, and `CreateFromPoints` amplifies the difference: a point that lands exactly
         * on the sphere either grows it or does not. Measured against the genuine assemblies over
         * 200 random point pairs -- `Vector3.Distance` and `Vector3.DistanceSquared` agree with a
         * double accumulation on every one and with a float accumulation on none
         * (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-134`).
         */
        [[nodiscard]] inline double SquaredLength(const Vector3& value) noexcept
        {
            const double x = static_cast<double>(value.X);
            const double y = static_cast<double>(value.Y);
            const double z = static_cast<double>(value.Z);
            return x * x + y * y + z * z;
        }

        /** @brief `Vector3.Length()` as XNA answers it: accumulated wide, stored narrow. */
        [[nodiscard]] inline float WideLength(const Vector3& value) noexcept
        {
            return static_cast<float>(std::sqrt(SquaredLength(value)));
        }

        /** @brief `Vector3.Distance(a, b)`: the difference is not stored, so it is not narrowed. */
        [[nodiscard]] inline float WideDistance(const Vector3& left, const Vector3& right) noexcept
        {
            const double x = static_cast<double>(left.X) - static_cast<double>(right.X);
            const double y = static_cast<double>(left.Y) - static_cast<double>(right.Y);
            const double z = static_cast<double>(left.Z) - static_cast<double>(right.Z);
            return static_cast<float>(std::sqrt(x * x + y * y + z * z));
        }

    }

    BoundingSphere BoundingSphere::CreateFromPoints(const std::vector<Vector3>& points)
    {
        if (points.empty())
        {
            throw std::invalid_argument("points must contain at least one point");
        }

        Vector3 minx(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                     std::numeric_limits<float>::max());
        Vector3 maxx = -minx;
        Vector3 miny = minx;
        Vector3 maxy = -minx;
        Vector3 minz = minx;
        Vector3 maxz = -minx;

        for (const Vector3& pt : points)
        {
            if (pt.X < minx.X) minx = pt;
            if (pt.X > maxx.X) maxx = pt;
            if (pt.Y < miny.Y) miny = pt;
            if (pt.Y > maxy.Y) maxy = pt;
            if (pt.Z < minz.Z) minz = pt;
            if (pt.Z > maxz.Z) maxz = pt;
        }

        // The widest axis is chosen by the *distance* between its two extreme points, not by the
        // square of it. The two readings differ only where one squared span rounds above another's
        // and the single-precision square root of both is the same float, which is a real shape
        // and not a contrived one: SAMPLE-142's cylinder has its Y extremes 6.2500005 apart
        // squared and its Z extremes 6.25, and 2.5 is the root of both, so squaring seeds the
        // sphere from Y and XNA seeds it from Z. Measured on the genuine framework over the
        // `span/*` sets of tests/reference/xna40/framework/bounding-sphere-oracle.txt: fourteen
        // point sets built to separate the two, all fourteen answered by the distance and none by
        // its square, with the 431 sets that were already there unchanged (`XNASWEEP-168`).
        const float spanX = WideDistance(maxx, minx);
        const float spanY = WideDistance(maxy, miny);
        const float spanZ = WideDistance(maxz, minz);

        Vector3 min = minx;
        Vector3 max = maxx;

        // The comparisons are not-less-than, not greater-than: when two axes are equally wide XNA
        // seeds the sphere from the *later* one, and the difference is visible -- a right triangle's
        // sphere comes out mirrored otherwise. Measured on the genuine runtime
        // (tests/reference/xna40/framework/framework-packing-oracle.json, cases boundingsphere/*),
        // after a model's mesh bounding sphere disagreed with XNA's in the build differential.
        // FNA uses greater-than here, and this is one of the places FNA and XNA differ
        // (plans/plan_xnapipeline_parity.md XNAPP-266).
        float span = spanX;
        if (spanY >= spanX && spanY >= spanZ)
        {
            max = maxy;
            min = miny;
            span = spanY;
        }
        if (spanZ >= spanX && spanZ >= spanY)
        {
            max = maxz;
            min = minz;
            span = spanZ;
        }

        // The seed is the midpoint of that pair and *half their distance* -- not the distance from
        // the midpoint to either end, which differs from it by an ulp often enough to change which
        // points the growth pass then touches. Measured: over 200 random pairs the genuine answer
        // is `Vector3.Distance(max, min) * 0.5f` on every one (`XNASWEEP-134`).
        Vector3 center(
            static_cast<float>((static_cast<double>(min.X) + static_cast<double>(max.X)) * 0.5),
            static_cast<float>((static_cast<double>(min.Y) + static_cast<double>(max.Y)) * 0.5),
            static_cast<float>((static_cast<double>(min.Z) + static_cast<double>(max.Z)) * 0.5));
        float radius = span * 0.5f;

        for (const Vector3& pt : points)
        {
            // Every value here is a `float` the moment it is stored, and the arithmetic between
            // stores is wide. The growth is XNA's own: the new radius is the midpoint of the old
            // one and the distance, and the centre slides along the difference by the fraction
            // that leaves the far side where it was. CNA's earlier form -- move by half the
            // overshoot along the unit direction, then take the radius as the distance to the
            // point -- agrees algebraically and not in floating point, and it is the reason 123
            // of the corpus's model references carried a bounding-sphere centre a few parts in
            // 10^7 away from XNA's. Measured exactly over 560 point sets (`XNASWEEP-134`).
            const Vector3 difference = pt - center;
            const float distance = WideLength(difference);
            if (!(distance > radius))
            {
                continue;
            }
            const float grown = (radius + distance) * 0.5f;
            const float share = static_cast<float>(
                1.0 - static_cast<double>(grown) / static_cast<double>(distance));
            const Vector3 offset = difference * share;
            center = center + offset;
            radius = grown;
        }

        return BoundingSphere(center, radius);
    }

    BoundingSphere BoundingSphere::CreateMerged(BoundingSphere original, BoundingSphere additional)
    {
        BoundingSphere result;
        CreateMerged(original, additional, result);
        return result;
    }

    void BoundingSphere::CreateMerged(const BoundingSphere& original, const BoundingSphere& additional,
                                      BoundingSphere& result)
    {
        Vector3 ocenterToaCenter = Vector3::Subtract(additional.Center, original.Center);
        float distance = ocenterToaCenter.Length();

        if (distance <= original.Radius + additional.Radius)
        {
            if (distance <= original.Radius - additional.Radius)
            {
                result = original;
                return;
            }

            if (distance <= additional.Radius - original.Radius)
            {
                result = additional;
                return;
            }
        }

        float leftRadius = std::max(original.Radius - distance, additional.Radius);
        float rightRadius = std::max(original.Radius + distance, additional.Radius);

        ocenterToaCenter = ocenterToaCenter +
            (((leftRadius - rightRadius) / (2.0f * ocenterToaCenter.Length())) * ocenterToaCenter);

        result.Center = original.Center + ocenterToaCenter;
        result.Radius = (leftRadius + rightRadius) / 2.0f;
    }

    bool BoundingSphere::Intersects(BoundingBox box) const
    {
        return box.Intersects(*this);
    }

    void BoundingSphere::Intersects(const BoundingBox& box, bool& result) const
    {
        box.Intersects(*this, result);
    }

    bool BoundingSphere::Intersects(const BoundingFrustum& frustum) const
    {
        return frustum.Intersects(*this);
    }

    bool BoundingSphere::Intersects(BoundingSphere sphere) const
    {
        bool result;
        Intersects(sphere, result);
        return result;
    }

    void BoundingSphere::Intersects(const BoundingSphere& sphere, bool& result) const
    {
        float sqDistance;
        Vector3::DistanceSquared(sphere.Center, Center, sqDistance);
        result = !(sqDistance > (sphere.Radius + Radius) * (sphere.Radius + Radius));
    }

    std::optional<float> BoundingSphere::Intersects(Ray ray) const
    {
        return ray.Intersects(*this);
    }

    void BoundingSphere::Intersects(const Ray& ray, std::optional<float>& result) const
    {
        ray.Intersects(*this, result);
    }

    PlaneIntersectionType BoundingSphere::Intersects(Plane plane) const
    {
        PlaneIntersectionType result;
        Intersects(plane, result);
        return result;
    }

    void BoundingSphere::Intersects(const Plane& plane, PlaneIntersectionType& result) const
    {
        float distance;
        Vector3::Dot(plane.Normal, Center, distance);
        distance += plane.D;

        if (distance > Radius)
        {
            result = PlaneIntersectionType::Front;
        }
        else if (distance < -Radius)
        {
            result = PlaneIntersectionType::Back;
        }
        else
        {
            result = PlaneIntersectionType::Intersecting;
        }
    }

    std::size_t BoundingSphere::GetHashCode() const
    {
        return Center.GetHashCode() + std::hash<float>{}(Radius);
    }

    std::string BoundingSphere::ToString() const
    {
        std::ostringstream ss;
        ss << "{Center:" << Center.ToString() << " Radius:" << Radius << "}";
        return ss.str();
    }

    bool operator==(BoundingSphere a, BoundingSphere b)
    {
        return a.Equals(b);
    }

    bool operator!=(BoundingSphere a, BoundingSphere b)
    {
        return !a.Equals(b);
    }
}
