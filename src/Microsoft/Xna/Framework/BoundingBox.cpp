// SPDX-License-Identifier: MS-PL

#include "Microsoft/Xna/Framework/BoundingBox.hpp"

#include "Microsoft/Xna/Framework/BoundingFrustum.hpp"
#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/ContainmentType.hpp"
#include "Microsoft/Xna/Framework/Plane.hpp"
#include "Microsoft/Xna/Framework/PlaneIntersectionType.hpp"
#include "Microsoft/Xna/Framework/Ray.hpp"

#include "System/ArgumentException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"

#include <algorithm>
#include <functional>
#include <limits>
#include <sstream>

namespace Microsoft::Xna::Framework
{
    const Vector3 BoundingBox::MaxVector3(
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max()
    );

    const Vector3 BoundingBox::MinVector3(
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest()
    );

    std::string BoundingBox::getDebugDisplayStringProperty() const
    {
        std::ostringstream stream;
        stream
            << "Min( " << Min.X << " " << Min.Y << " " << Min.Z << " ) \r\n"
            << "Max( " << Max.X << " " << Max.Y << " " << Max.Z << " )";
        return stream.str();
    }

    BoundingBox::BoundingBox(const Vector3& min, const Vector3& max)
        : Min(min), Max(max)
    {
    }

    void BoundingBox::Contains(const BoundingBox& box, ContainmentType& result) const
    {
        result = Contains(box);
    }

    void BoundingBox::Contains(const BoundingSphere& sphere, ContainmentType& result) const
    {
        result = Contains(sphere);
    }

    ContainmentType BoundingBox::Contains(const Vector3& point) const
    {
        ContainmentType result;
        Contains(point, result);
        return result;
    }

    ContainmentType BoundingBox::Contains(const BoundingBox& box) const
    {
        // Min/max checks are enough for an axis-aligned box test.
        if (box.Max.X < Min.X ||
            box.Min.X > Max.X ||
            box.Max.Y < Min.Y ||
            box.Min.Y > Max.Y ||
            box.Max.Z < Min.Z ||
            box.Min.Z > Max.Z)
        {
            return ContainmentType::Disjoint;
        }

        if (box.Min.X >= Min.X &&
            box.Max.X <= Max.X &&
            box.Min.Y >= Min.Y &&
            box.Max.Y <= Max.Y &&
            box.Min.Z >= Min.Z &&
            box.Max.Z <= Max.Z)
        {
            return ContainmentType::Contains;
        }

        return ContainmentType::Intersects;
    }

    ContainmentType BoundingBox::Contains(const BoundingFrustum& frustum) const
    {
        // Legacy behavior: classify the frustum by testing its corners against this box.
        auto corners = frustum.GetCorners();

        std::size_t i = 0;
        ContainmentType contained = ContainmentType::Disjoint;

        for (; i < corners.size(); i += 1)
        {
            Contains(corners[i], contained);
            if (contained == ContainmentType::Disjoint)
            {
                break;
            }
        }

        if (i == corners.size())
        {
            return ContainmentType::Contains;
        }

        if (i != 0)
        {
            return ContainmentType::Intersects;
        }

        i += 1;
        for (; i < corners.size(); i += 1)
        {
            Contains(corners[i], contained);
            if (contained != ContainmentType::Contains)
            {
                return ContainmentType::Intersects;
            }
        }

        return ContainmentType::Contains;
    }

    ContainmentType BoundingBox::Contains(const BoundingSphere& sphere) const
    {
        if (sphere.Center.X - Min.X >= sphere.Radius &&
            sphere.Center.Y - Min.Y >= sphere.Radius &&
            sphere.Center.Z - Min.Z >= sphere.Radius &&
            Max.X - sphere.Center.X >= sphere.Radius &&
            Max.Y - sphere.Center.Y >= sphere.Radius &&
            Max.Z - sphere.Center.Z >= sphere.Radius)
        {
            return ContainmentType::Contains;
        }

        double dmin = 0.0;

        double e = sphere.Center.X - Min.X;
        if (e < 0.0)
        {
            if (e < -sphere.Radius)
            {
                return ContainmentType::Disjoint;
            }
            dmin += e * e;
        }
        else
        {
            e = sphere.Center.X - Max.X;
            if (e > 0.0)
            {
                if (e > sphere.Radius)
                {
                    return ContainmentType::Disjoint;
                }
                dmin += e * e;
            }
        }

        e = sphere.Center.Y - Min.Y;
        if (e < 0.0)
        {
            if (e < -sphere.Radius)
            {
                return ContainmentType::Disjoint;
            }
            dmin += e * e;
        }
        else
        {
            e = sphere.Center.Y - Max.Y;
            if (e > 0.0)
            {
                if (e > sphere.Radius)
                {
                    return ContainmentType::Disjoint;
                }
                dmin += e * e;
            }
        }

        e = sphere.Center.Z - Min.Z;
        if (e < 0.0)
        {
            if (e < -sphere.Radius)
            {
                return ContainmentType::Disjoint;
            }
            dmin += e * e;
        }
        else
        {
            e = sphere.Center.Z - Max.Z;
            if (e > 0.0)
            {
                if (e > sphere.Radius)
                {
                    return ContainmentType::Disjoint;
                }
                dmin += e * e;
            }
        }

        if (dmin <= sphere.Radius * sphere.Radius)
        {
            return ContainmentType::Intersects;
        }

        return ContainmentType::Disjoint;
    }

    void BoundingBox::Contains(const Vector3& point, ContainmentType& result) const
    {
        if (point.X < Min.X ||
            point.X > Max.X ||
            point.Y < Min.Y ||
            point.Y > Max.Y ||
            point.Z < Min.Z ||
            point.Z > Max.Z)
        {
            result = ContainmentType::Disjoint;
        }
        else
        {
            result = ContainmentType::Contains;
        }
    }

    std::vector<Vector3> BoundingBox::GetCorners() const
    {
        return {
            Vector3(Min.X, Max.Y, Max.Z),
            Vector3(Max.X, Max.Y, Max.Z),
            Vector3(Max.X, Min.Y, Max.Z),
            Vector3(Min.X, Min.Y, Max.Z),
            Vector3(Min.X, Max.Y, Min.Z),
            Vector3(Max.X, Max.Y, Min.Z),
            Vector3(Max.X, Min.Y, Min.Z),
            Vector3(Min.X, Min.Y, Min.Z)
        };
    }

    void BoundingBox::GetCorners(std::vector<Vector3>& corners) const
    {
        if (corners.size() < CornerCount)
        {
            throw System::ArgumentOutOfRangeException("corners must contain at least 8 elements");
        }

        corners[0].X = Min.X;
        corners[0].Y = Max.Y;
        corners[0].Z = Max.Z;

        corners[1].X = Max.X;
        corners[1].Y = Max.Y;
        corners[1].Z = Max.Z;

        corners[2].X = Max.X;
        corners[2].Y = Min.Y;
        corners[2].Z = Max.Z;

        corners[3].X = Min.X;
        corners[3].Y = Min.Y;
        corners[3].Z = Max.Z;

        corners[4].X = Min.X;
        corners[4].Y = Max.Y;
        corners[4].Z = Min.Z;

        corners[5].X = Max.X;
        corners[5].Y = Max.Y;
        corners[5].Z = Min.Z;

        corners[6].X = Max.X;
        corners[6].Y = Min.Y;
        corners[6].Z = Min.Z;

        corners[7].X = Min.X;
        corners[7].Y = Min.Y;
        corners[7].Z = Min.Z;
    }

    std::optional<float> BoundingBox::Intersects(const Ray& ray) const
    {
        return ray.Intersects(*this);
    }

    void BoundingBox::Intersects(const Ray& ray, std::optional<float>& result) const
    {
        result = Intersects(ray);
    }

    bool BoundingBox::Intersects(const BoundingFrustum& frustum) const
    {
        return frustum.Intersects(*this);
    }

    void BoundingBox::Intersects(const BoundingSphere& sphere, bool& result) const
    {
        result = Intersects(sphere);
    }

    bool BoundingBox::Intersects(const BoundingBox& box) const
    {
        bool result;
        Intersects(box, result);
        return result;
    }

    PlaneIntersectionType BoundingBox::Intersects(const Plane& plane) const
    {
        PlaneIntersectionType result;
        Intersects(plane, result);
        return result;
    }

    void BoundingBox::Intersects(const BoundingBox& box, bool& result) const
    {
        if ((Max.X >= box.Min.X) && (Min.X <= box.Max.X))
        {
            if ((Max.Y < box.Min.Y) || (Min.Y > box.Max.Y))
            {
                result = false;
                return;
            }

            result = (Max.Z >= box.Min.Z) && (Min.Z <= box.Max.Z);
            return;
        }

        result = false;
    }

    bool BoundingBox::Intersects(const BoundingSphere& sphere) const
    {
        if (sphere.Center.X - Min.X > sphere.Radius &&
            sphere.Center.Y - Min.Y > sphere.Radius &&
            sphere.Center.Z - Min.Z > sphere.Radius &&
            Max.X - sphere.Center.X > sphere.Radius &&
            Max.Y - sphere.Center.Y > sphere.Radius &&
            Max.Z - sphere.Center.Z > sphere.Radius)
        {
            return true;
        }

        const float radiusSq = sphere.Radius * sphere.Radius;
        double dmin = 0.0;

        if (sphere.Center.X < Min.X)
        {
            dmin += (sphere.Center.X - Min.X) * (sphere.Center.X - Min.X);
        }
        else if (sphere.Center.X > Max.X)
        {
            dmin += (sphere.Center.X - Max.X) * (sphere.Center.X - Max.X);
        }

        if (sphere.Center.Y < Min.Y)
        {
            dmin += (sphere.Center.Y - Min.Y) * (sphere.Center.Y - Min.Y);
        }
        else if (sphere.Center.Y > Max.Y)
        {
            dmin += (sphere.Center.Y - Max.Y) * (sphere.Center.Y - Max.Y);
        }

        if (sphere.Center.Z < Min.Z)
        {
            dmin += (sphere.Center.Z - Min.Z) * (sphere.Center.Z - Min.Z);
        }
        else if (sphere.Center.Z > Max.Z)
        {
            dmin += (sphere.Center.Z - Max.Z) * (sphere.Center.Z - Max.Z);
        }

        return dmin <= radiusSq;
    }

    void BoundingBox::Intersects(const Plane& plane, PlaneIntersectionType& result) const
    {
        Vector3 positiveVertex;
        Vector3 negativeVertex;

        if (plane.Normal.X >= 0.0f)
        {
            positiveVertex.X = Max.X;
            negativeVertex.X = Min.X;
        }
        else
        {
            positiveVertex.X = Min.X;
            negativeVertex.X = Max.X;
        }

        if (plane.Normal.Y >= 0.0f)
        {
            positiveVertex.Y = Max.Y;
            negativeVertex.Y = Min.Y;
        }
        else
        {
            positiveVertex.Y = Min.Y;
            negativeVertex.Y = Max.Y;
        }

        if (plane.Normal.Z >= 0.0f)
        {
            positiveVertex.Z = Max.Z;
            negativeVertex.Z = Min.Z;
        }
        else
        {
            positiveVertex.Z = Min.Z;
            negativeVertex.Z = Max.Z;
        }

        float distance =
            plane.Normal.X * negativeVertex.X +
            plane.Normal.Y * negativeVertex.Y +
            plane.Normal.Z * negativeVertex.Z +
            plane.D;

        if (distance > 0.0f)
        {
            result = PlaneIntersectionType::Front;
            return;
        }

        distance =
            plane.Normal.X * positiveVertex.X +
            plane.Normal.Y * positiveVertex.Y +
            plane.Normal.Z * positiveVertex.Z +
            plane.D;

        if (distance < 0.0f)
        {
            result = PlaneIntersectionType::Back;
            return;
        }

        result = PlaneIntersectionType::Intersecting;
    }

    bool BoundingBox::Equals(const BoundingBox& other) const
    {
        return Min.X == other.Min.X &&
            Min.Y == other.Min.Y &&
            Min.Z == other.Min.Z &&
            Max.X == other.Max.X &&
            Max.Y == other.Max.Y &&
            Max.Z == other.Max.Z;
    }

    BoundingBox BoundingBox::CreateFromPoints(const std::vector<Vector3>& points)
    {
        if (points.empty())
        {
            throw System::ArgumentException("Collection is empty");
        }

        Vector3 minVec = MaxVector3;
        Vector3 maxVec = MinVector3;

        for (const Vector3& point : points)
        {
            minVec.X = (minVec.X < point.X) ? minVec.X : point.X;
            minVec.Y = (minVec.Y < point.Y) ? minVec.Y : point.Y;
            minVec.Z = (minVec.Z < point.Z) ? minVec.Z : point.Z;

            maxVec.X = (maxVec.X > point.X) ? maxVec.X : point.X;
            maxVec.Y = (maxVec.Y > point.Y) ? maxVec.Y : point.Y;
            maxVec.Z = (maxVec.Z > point.Z) ? maxVec.Z : point.Z;
        }

        return BoundingBox(minVec, maxVec);
    }

    BoundingBox BoundingBox::CreateFromSphere(const BoundingSphere& sphere)
    {
        BoundingBox result;
        CreateFromSphere(sphere, result);
        return result;
    }

    void BoundingBox::CreateFromSphere(const BoundingSphere& sphere, BoundingBox& result)
    {
        const Vector3 corner(sphere.Radius, sphere.Radius, sphere.Radius);
        result.Min = sphere.Center - corner;
        result.Max = sphere.Center + corner;
    }

    BoundingBox BoundingBox::CreateMerged(const BoundingBox& original, const BoundingBox& additional)
    {
        BoundingBox result;
        CreateMerged(original, additional, result);
        return result;
    }

    void BoundingBox::CreateMerged(const BoundingBox& original, const BoundingBox& additional, BoundingBox& result)
    {
        result.Min.X = std::min(original.Min.X, additional.Min.X);
        result.Min.Y = std::min(original.Min.Y, additional.Min.Y);
        result.Min.Z = std::min(original.Min.Z, additional.Min.Z);

        result.Max.X = std::max(original.Max.X, additional.Max.X);
        result.Max.Y = std::max(original.Max.Y, additional.Max.Y);
        result.Max.Z = std::max(original.Max.Z, additional.Max.Z);
    }

    std::size_t BoundingBox::GetHashCode() const
    {
        auto combine = [](std::size_t seed, float value)
        {
            const std::size_t hashed = std::hash<float>{}(value);
            return seed ^ (hashed + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U));
        };

        std::size_t seed = 0;
        seed = combine(seed, Min.X);
        seed = combine(seed, Min.Y);
        seed = combine(seed, Min.Z);
        seed = combine(seed, Max.X);
        seed = combine(seed, Max.Y);
        seed = combine(seed, Max.Z);
        return seed;
    }

    std::string BoundingBox::ToString() const
    {
        std::ostringstream stream;
        stream
            << "{{Min:{X:" << Min.X << " Y:" << Min.Y << " Z:" << Min.Z << "}"
            << " Max:{X:" << Max.X << " Y:" << Max.Y << " Z:" << Max.Z << "}"
            << "}}";
        return stream.str();
    }

    bool operator==(const BoundingBox& a, const BoundingBox& b)
    {
        return a.Equals(b);
    }

    bool operator!=(const BoundingBox& a, const BoundingBox& b)
    {
        return !a.Equals(b);
    }
}
