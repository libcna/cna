// SPDX-License-Identifier: MS-PL

#include "Microsoft/Xna/Framework/BoundingFrustum.hpp"
#include "Gjk.hpp"

#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>

#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/Ray.hpp"
#include "System/ArgumentOutOfRangeException.hpp"

namespace Microsoft::Xna::Framework
{
namespace
{
Vector3 Support(const std::array<Vector3, 8> &vertices, const Vector3 &direction)
{
    std::size_t best = 0;
    float maximum = Vector3::Dot(vertices[0], direction);
    for (std::size_t i = 1; i < vertices.size(); ++i)
    {
        const float projection = Vector3::Dot(vertices[i], direction);
        if (projection > maximum)
        {
            maximum = projection;
            best = i;
        }
    }
    return vertices[best];
}

template <class OtherSupport>
bool ConvexIntersects(const std::array<Vector3, 8> &vertices, Vector3 direction,
                      const OtherSupport &otherSupport)
{
    // XNA BoundingFrustum.Intersects uses Gjk, with these relative progress
    // and simplex-distance thresholds, not its conservative Contains planes.
    CNA::Internal::Math::Gjk simplex;
    float distanceSquared = std::numeric_limits<float>::max();
    float threshold = 0;
    do
    {
        const Vector3 opposite(0.0f - direction.X, 0.0f - direction.Y, 0.0f - direction.Z);
        const Vector3 point = Support(vertices, opposite) - otherSupport(direction);
        const double projection = static_cast<double>(direction.X) * point.X +
                                  static_cast<double>(direction.Y) * point.Y +
                                  static_cast<double>(direction.Z) * point.Z;
        if (projection > 0)
            return false;
        simplex.AddSupportPoint(point);
        direction = simplex.ClosestPoint();
        const float previous = distanceSquared;
        distanceSquared = Vector3::Dot(direction, direction);
        if (static_cast<double>(previous) - distanceSquared <= static_cast<double>(1e-5f) * previous)
            return false;
        threshold = 4e-5f * simplex.MaxLengthSquared();
    } while (!simplex.FullSimplex() && distanceSquared >= threshold);
    return true;
}
} // namespace

    BoundingFrustum::BoundingFrustum(Matrix value)
        : matrix(value)
    {
        CreatePlanes();
        CreateCorners();
    }

    Matrix BoundingFrustum::getMatrixProperty() const
    {
        return matrix;
    }

    void BoundingFrustum::setMatrixProperty(Matrix value)
    {
        matrix = value;
        CreatePlanes();
        CreateCorners();
    }

    Plane BoundingFrustum::getNearProperty() const { return planes[0]; }
    Plane BoundingFrustum::getFarProperty() const { return planes[1]; }
    Plane BoundingFrustum::getLeftProperty() const { return planes[2]; }
    Plane BoundingFrustum::getRightProperty() const { return planes[3]; }
    Plane BoundingFrustum::getTopProperty() const { return planes[4]; }
    Plane BoundingFrustum::getBottomProperty() const { return planes[5]; }

    ContainmentType BoundingFrustum::Contains(const BoundingFrustum& frustum) const
    {
        if (!Intersects(frustum))
            return ContainmentType::Disjoint;
        for (const Vector3 &corner : frustum.corners)
            if (Contains(corner) == ContainmentType::Disjoint)
                return ContainmentType::Intersects;
        return ContainmentType::Contains;
    }

    ContainmentType BoundingFrustum::Contains(BoundingBox box) const
    {
        ContainmentType result;
        Contains(box, result);
        return result;
    }

    void BoundingFrustum::Contains(const BoundingBox& box, ContainmentType& result) const
    {
        bool intersects = false;

        for (int i = 0; i < PlaneCount; i += 1)
        {
            PlaneIntersectionType planeIntersectionType;
            box.Intersects(planes[i], planeIntersectionType);

            switch (planeIntersectionType)
            {
            case PlaneIntersectionType::Front:
                result = ContainmentType::Disjoint;
                return;
            case PlaneIntersectionType::Intersecting:
                intersects = true;
                break;
            case PlaneIntersectionType::Back:
                break;
            }
        }

        result = intersects ? ContainmentType::Intersects : ContainmentType::Contains;
    }

    ContainmentType BoundingFrustum::Contains(BoundingSphere sphere) const
    {
        ContainmentType result;
        Contains(sphere, result);
        return result;
    }

    void BoundingFrustum::Contains(const BoundingSphere& sphere, ContainmentType& result) const
    {
        // XNA inlines the plane expression here: a public Dot result would add
        // a Single rounding boundary and misclassify near-tangent spheres.
        int insidePlanes = 0;
        for (const Plane &plane : planes)
        {
            const double distance = static_cast<double>(plane.Normal.X) * sphere.Center.X +
                                    static_cast<double>(plane.Normal.Y) * sphere.Center.Y +
                                    static_cast<double>(plane.Normal.Z) * sphere.Center.Z + plane.D;
            if (distance > sphere.Radius)
            {
                result = ContainmentType::Disjoint;
                return;
            }
            if (distance < -sphere.Radius)
                ++insidePlanes;
        }
        result = insidePlanes == PlaneCount ? ContainmentType::Contains : ContainmentType::Intersects;
    }

    ContainmentType BoundingFrustum::Contains(Vector3 point) const
    {
        ContainmentType result;
        Contains(point, result);
        return result;
    }

    void BoundingFrustum::Contains(const Vector3& point, ContainmentType& result) const
    {
        // XNA Contains(Vector3) checks all six planes against this tolerance.
        // An exact plane never terminates the search or changes the result to Intersects.
        for (const Plane &plane : planes)
        {
            const float distance =
                static_cast<float>(static_cast<double>(plane.Normal.X) * point.X +
                                   static_cast<double>(plane.Normal.Y) * point.Y +
                                   static_cast<double>(plane.Normal.Z) * point.Z + plane.D);
            if (distance > 1e-5f)
            {
                result = ContainmentType::Disjoint;
                return;
            }
        }
        result = ContainmentType::Contains;
    }

    std::vector<Vector3> BoundingFrustum::GetCorners() const
    {
        return std::vector<Vector3>(corners.begin(), corners.end());
    }

    void BoundingFrustum::GetCorners(std::vector<Vector3>& outputCorners) const
    {
        if (outputCorners.size() < CornerCount)
        {
            throw System::ArgumentOutOfRangeException("corners");
        }

        for (int i = 0; i < CornerCount; i += 1)
        {
            outputCorners[i] = corners[i];
        }
    }

    bool BoundingFrustum::Intersects(const BoundingFrustum& frustum) const
    {
        Vector3 direction = corners[0] - frustum.corners[0];
        if (Vector3::Dot(direction, direction) < 1e-5f)
            direction = corners[0] - frustum.corners[1];
        return ConvexIntersects(corners, direction,
                                [&frustum](const Vector3 &v) { return Support(frustum.corners, v); });
    }

    bool BoundingFrustum::Intersects(BoundingBox box) const
    {
        bool result;
        Intersects(box, result);
        return result;
    }

    void BoundingFrustum::Intersects(const BoundingBox& box, bool& result) const
    {
        Vector3 direction = corners[0] - box.Min;
        if (Vector3::Dot(direction, direction) < 1e-5f)
            direction = corners[0] - box.Max;
        result = ConvexIntersects(corners, direction, [&box](const Vector3 &v) {
            return Vector3(v.X >= 0 ? box.Max.X : box.Min.X, v.Y >= 0 ? box.Max.Y : box.Min.Y,
                           v.Z >= 0 ? box.Max.Z : box.Min.Z);
        });
    }

    bool BoundingFrustum::Intersects(BoundingSphere sphere) const
    {
        bool result;
        Intersects(sphere, result);
        return result;
    }

    void BoundingFrustum::Intersects(const BoundingSphere& sphere, bool& result) const
    {
        Vector3 direction = corners[0] - sphere.Center;
        if (Vector3::Dot(direction, direction) < 1e-5f)
            direction = Vector3::UnitX;
        result = ConvexIntersects(corners, direction, [&sphere](const Vector3 &v) {
            const float scale = sphere.Radius / v.Length();
            return Vector3(static_cast<float>(sphere.Center.X + static_cast<double>(v.X) * scale),
                           static_cast<float>(sphere.Center.Y + static_cast<double>(v.Y) * scale),
                           static_cast<float>(sphere.Center.Z + static_cast<double>(v.Z) * scale));
        });
    }

    PlaneIntersectionType BoundingFrustum::Intersects(Plane plane) const
    {
        PlaneIntersectionType result;
        Intersects(plane, result);
        return result;
    }

    void BoundingFrustum::Intersects(const Plane& plane, PlaneIntersectionType& result) const
    {
        result = plane.IntersectsPoint(corners[0]);

        for (int i = 1; i < CornerCount; i += 1)
        {
            if (plane.IntersectsPoint(corners[i]) != result)
            {
                result = PlaneIntersectionType::Intersecting;
                return;
            }
        }
    }

    std::optional<float> BoundingFrustum::Intersects(Ray ray) const
    {
        std::optional<float> result;
        Intersects(ray, result);
        return result;
    }

    void BoundingFrustum::Intersects(const Ray& ray, std::optional<float>& result) const
    {
        ContainmentType ctype;
        Contains(ray.Position, ctype);

        if (ctype == ContainmentType::Contains)
        {
            result = 0.0f;
            return;
        }
        // XNA clips outside origins against the plane interval. FNA's origin-only
        // shortcut misses entering rays. Keep XNA's separate containment and
        // parallel-plane thresholds; do not normalize the caller's direction.
        float entry = std::numeric_limits<float>::lowest();
        float exit = std::numeric_limits<float>::max();
        result = std::nullopt;
        for (const Plane& plane : planes)
        {
            const float direction = Vector3::Dot(ray.Direction, plane.Normal);
            const float distance = Vector3::Dot(ray.Position, plane.Normal) + plane.D;
            if (std::abs(direction) < 1e-5f)
            {
                if (distance > 0.0f) return;
                continue;
            }

            const float parameter = (0.0f - distance) / direction;
            if (direction < 0.0f)
            {
                if (parameter > exit) return;
                if (parameter > entry) entry = parameter;
            }
            else
            {
                if (parameter < entry) return;
                if (parameter < exit) exit = parameter;
            }
        }
        const float parameter = entry >= 0.0f ? entry : exit;
        if (parameter >= 0.0f) result = parameter;
    }

    void BoundingFrustum::CreateCorners()
    {
        IntersectionPoint(planes[0], planes[2], planes[4], corners[0]);
        IntersectionPoint(planes[0], planes[3], planes[4], corners[1]);
        IntersectionPoint(planes[0], planes[3], planes[5], corners[2]);
        IntersectionPoint(planes[0], planes[2], planes[5], corners[3]);
        IntersectionPoint(planes[1], planes[2], planes[4], corners[4]);
        IntersectionPoint(planes[1], planes[3], planes[4], corners[5]);
        IntersectionPoint(planes[1], planes[3], planes[5], corners[6]);
        IntersectionPoint(planes[1], planes[2], planes[5], corners[7]);
    }

    void BoundingFrustum::CreatePlanes()
    {
        planes[0] = Plane(-matrix.M13, -matrix.M23, -matrix.M33, -matrix.M43);
        planes[1] = Plane(matrix.M13 - matrix.M14, matrix.M23 - matrix.M24, matrix.M33 - matrix.M34,
                          matrix.M43 - matrix.M44);
        planes[2] = Plane(-matrix.M14 - matrix.M11, -matrix.M24 - matrix.M21, -matrix.M34 - matrix.M31,
                          -matrix.M44 - matrix.M41);
        planes[3] = Plane(matrix.M11 - matrix.M14, matrix.M21 - matrix.M24, matrix.M31 - matrix.M34,
                          matrix.M41 - matrix.M44);
        planes[4] = Plane(matrix.M12 - matrix.M14, matrix.M22 - matrix.M24, matrix.M32 - matrix.M34,
                          matrix.M42 - matrix.M44);
        planes[5] = Plane(-matrix.M14 - matrix.M12, -matrix.M24 - matrix.M22, -matrix.M34 - matrix.M32,
                          -matrix.M44 - matrix.M42);

        for (Plane& plane : planes)
        {
            NormalizePlane(plane);
        }
    }

    void BoundingFrustum::NormalizePlane(Plane& p)
    {
        float factor = 1.0f / p.Normal.Length();
        p.Normal.X *= factor;
        p.Normal.Y *= factor;
        p.Normal.Z *= factor;
        p.D *= factor;
    }

    void BoundingFrustum::IntersectionPoint(const Plane& a, const Plane& b, const Plane& c, Vector3& result)
    {
        Vector3 v1;
        Vector3 v2;
        Vector3 v3;
        Vector3 cross;

        Vector3::Cross(b.Normal, c.Normal, cross);

        float f;
        Vector3::Dot(a.Normal, cross, f);
        f *= -1.0f;

        Vector3::Cross(b.Normal, c.Normal, cross);
        Vector3::Multiply(cross, a.D, v1);

        Vector3::Cross(c.Normal, a.Normal, cross);
        Vector3::Multiply(cross, b.D, v2);

        Vector3::Cross(a.Normal, b.Normal, cross);
        Vector3::Multiply(cross, c.D, v3);

        result.X = (v1.X + v2.X + v3.X) / f;
        result.Y = (v1.Y + v2.Y + v3.Y) / f;
        result.Z = (v1.Z + v2.Z + v3.Z) / f;
    }

    bool BoundingFrustum::Equals(const std::any& obj) const
    {
        const BoundingFrustum* other = std::any_cast<BoundingFrustum>(&obj);
        return other != nullptr && Equals(*other);
    }

    bool BoundingFrustum::Equals(const BoundingFrustum& other) const
    {
        return *this == other;
    }

    std::string BoundingFrustum::getDebugDisplayStringProperty() const
    {
        std::ostringstream ss;
        ss << "Near( " << planes[0].ToString() << " ) \r\n"
            << "Far( " << planes[1].ToString() << " ) \r\n"
            << "Left( " << planes[2].ToString() << " ) \r\n"
            << "Right( " << planes[3].ToString() << " ) \r\n"
            << "Top( " << planes[4].ToString() << " ) \r\n"
            << "Bottom( " << planes[5].ToString() << " ) ";
        return ss.str();
    }

    std::size_t BoundingFrustum::GetHashCode() const
    {
        // FNA: this.matrix.GetHashCode() returning int.
        // C++ uses platform-native std::size_t to match Matrix::GetHashCode().
        return matrix.GetHashCode();
    }

    std::string BoundingFrustum::ToString() const
    {
        std::ostringstream ss;
        ss << "{Near:" << planes[0].ToString()
            << " Far:" << planes[1].ToString()
            << " Left:" << planes[2].ToString()
            << " Right:" << planes[3].ToString()
            << " Top:" << planes[4].ToString()
            << " Bottom:" << planes[5].ToString()
            << "}";
        return ss.str();
    }

    bool operator==(const BoundingFrustum& a, const BoundingFrustum& b)
    {
        // FNA checks (object.Equals(a, null)) for null operands because BoundingFrustum is a C# class.
        // C++ references cannot be null, so the null guards are omitted.
        return a.getMatrixProperty() == b.getMatrixProperty();
    }

    bool operator!=(const BoundingFrustum& a, const BoundingFrustum& b)
    {
        return !(a == b);
    }
}
