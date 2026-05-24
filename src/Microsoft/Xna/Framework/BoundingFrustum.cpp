#include "Microsoft/Xna/Framework/BoundingFrustum.hpp"

#include <cmath>
#include <sstream>
#include <stdexcept>

#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/Ray.hpp"
#include "System/NotImplementedException.hpp"

namespace Microsoft::Xna::Framework
{
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
        if (this == &frustum)
        {
            return ContainmentType::Contains;
        }

        bool intersects = false;
        for (int i = 0; i < PlaneCount; i += 1)
        {
            PlaneIntersectionType planeIntersectionType;
            frustum.Intersects(planes[i], planeIntersectionType);

            if (planeIntersectionType == PlaneIntersectionType::Front)
            {
                return ContainmentType::Disjoint;
            }
            if (planeIntersectionType == PlaneIntersectionType::Intersecting)
            {
                intersects = true;
            }
        }

        return intersects ? ContainmentType::Intersects : ContainmentType::Contains;
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
        bool intersects = false;

        for (int i = 0; i < PlaneCount; i += 1)
        {
            PlaneIntersectionType planeIntersectionType;
            sphere.Intersects(planes[i], planeIntersectionType);

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

    ContainmentType BoundingFrustum::Contains(Vector3 point) const
    {
        ContainmentType result;
        Contains(point, result);
        return result;
    }

    void BoundingFrustum::Contains(const Vector3& point, ContainmentType& result) const
    {
        bool intersects = false;

        for (int i = 0; i < PlaneCount; i += 1)
        {
            float classifyPoint = (point.X * planes[i].Normal.X) +
                                  (point.Y * planes[i].Normal.Y) +
                                  (point.Z * planes[i].Normal.Z) +
                                  planes[i].D;

            if (classifyPoint > 0.0f)
            {
                result = ContainmentType::Disjoint;
                return;
            }
            if (classifyPoint == 0.0f)
            {
                intersects = true;
                break;
            }
        }

        result = intersects ? ContainmentType::Intersects : ContainmentType::Contains;
    }

    std::vector<Vector3> BoundingFrustum::GetCorners() const
    {
        return std::vector<Vector3>(corners.begin(), corners.end());
    }

    void BoundingFrustum::GetCorners(std::vector<Vector3>& outputCorners) const
    {
        if (outputCorners.size() < CornerCount)
        {
            throw std::out_of_range("corners");
        }

        for (int i = 0; i < CornerCount; i += 1)
        {
            outputCorners[i] = corners[i];
        }
    }

    bool BoundingFrustum::Intersects(const BoundingFrustum& frustum) const
    {
        return Contains(frustum) != ContainmentType::Disjoint;
    }

    bool BoundingFrustum::Intersects(BoundingBox box) const
    {
        bool result;
        Intersects(box, result);
        return result;
    }

    void BoundingFrustum::Intersects(const BoundingBox& box, bool& result) const
    {
        ContainmentType containment;
        Contains(box, containment);
        result = containment != ContainmentType::Disjoint;
    }

    bool BoundingFrustum::Intersects(BoundingSphere sphere) const
    {
        bool result;
        Intersects(sphere, result);
        return result;
    }

    void BoundingFrustum::Intersects(const BoundingSphere& sphere, bool& result) const
    {
        ContainmentType containment;
        Contains(sphere, containment);
        result = containment != ContainmentType::Disjoint;
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

        if (ctype == ContainmentType::Disjoint)
        {
            result = std::nullopt;
            return;
        }
        if (ctype == ContainmentType::Contains)
        {
            result = 0.0f;
            return;
        }
        if (ctype != ContainmentType::Intersects)
        {
            throw std::out_of_range("ctype");
        }

        throw System::NotImplementedException("BoundingFrustum::Intersects(Ray) is not implemented");
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
        planes[1] = Plane(matrix.M13 - matrix.M14, matrix.M23 - matrix.M24, matrix.M33 - matrix.M34, matrix.M43 - matrix.M44);
        planes[2] = Plane(-matrix.M14 - matrix.M11, -matrix.M24 - matrix.M21, -matrix.M34 - matrix.M31, -matrix.M44 - matrix.M41);
        planes[3] = Plane(matrix.M11 - matrix.M14, matrix.M21 - matrix.M24, matrix.M31 - matrix.M34, matrix.M41 - matrix.M44);
        planes[4] = Plane(matrix.M12 - matrix.M14, matrix.M22 - matrix.M24, matrix.M32 - matrix.M34, matrix.M42 - matrix.M44);
        planes[5] = Plane(-matrix.M14 - matrix.M12, -matrix.M24 - matrix.M22, -matrix.M34 - matrix.M32, -matrix.M44 - matrix.M42);

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
        return a.getMatrixProperty() == b.getMatrixProperty();
    }

    bool operator!=(const BoundingFrustum& a, const BoundingFrustum& b)
    {
        return !(a == b);
    }
}
