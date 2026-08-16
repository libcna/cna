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

        float sqDistX = Vector3::DistanceSquared(maxx, minx);
        float sqDistY = Vector3::DistanceSquared(maxy, miny);
        float sqDistZ = Vector3::DistanceSquared(maxz, minz);

        Vector3 min = minx;
        Vector3 max = maxx;

        if (sqDistY > sqDistX && sqDistY > sqDistZ)
        {
            max = maxy;
            min = miny;
        }
        if (sqDistZ > sqDistX && sqDistZ > sqDistY)
        {
            max = maxz;
            min = minz;
        }

        Vector3 center = (min + max) * 0.5f;
        float radius = Vector3::Distance(max, center);

        float sqRadius = radius * radius;
        for (const Vector3& pt : points)
        {
            Vector3 diff = pt - center;
            float sqDist = diff.LengthSquared();
            if (sqDist > sqRadius)
            {
                float distance = std::sqrt(sqDist);
                Vector3 direction = diff / distance;
                Vector3 g = center - radius * direction;
                center = (g + pt) / 2.0f;
                radius = Vector3::Distance(pt, center);
                sqRadius = radius * radius;
            }
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
