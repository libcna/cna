#include "Microsoft/Xna/Framework/Plane.hpp"

#include <cmath>
#include <functional>
#include <sstream>

#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/BoundingFrustum.hpp"
#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"

namespace Microsoft::Xna::Framework
{
    Plane::Plane(Vector4 value)
        : Plane(Vector3(value.X, value.Y, value.Z), value.W)
    {
    }

    Plane::Plane(Vector3 normal, float d)
        : Normal(normal), D(d)
    {
    }

    Plane::Plane(Vector3 a, Vector3 b, Vector3 c)
    {
        Vector3 ab = b - a;
        Vector3 ac = c - a;

        Vector3 cross = Vector3::Cross(ab, ac);
        Vector3::Normalize(cross, Normal);
        D = -Vector3::Dot(Normal, a);
    }

    Plane::Plane(float a, float b, float c, float d)
        : Plane(Vector3(a, b, c), d)
    {
    }

    float Plane::Dot(Vector4 value) const
    {
        return (Normal.X * value.X) + (Normal.Y * value.Y) + (Normal.Z * value.Z) + (D * value.W);
    }

    void Plane::Dot(const Vector4& value, float& result) const
    {
        result = Dot(value);
    }

    float Plane::DotCoordinate(Vector3 value) const
    {
        return (Normal.X * value.X) + (Normal.Y * value.Y) + (Normal.Z * value.Z) + D;
    }

    void Plane::DotCoordinate(const Vector3& value, float& result) const
    {
        result = DotCoordinate(value);
    }

    float Plane::DotNormal(Vector3 value) const
    {
        return (Normal.X * value.X) + (Normal.Y * value.Y) + (Normal.Z * value.Z);
    }

    void Plane::DotNormal(const Vector3& value, float& result) const
    {
        result = DotNormal(value);
    }

    void Plane::Normalize()
    {
        float length = Normal.Length();
        float factor = 1.0f / length;
        Vector3::Multiply(Normal, factor, Normal);
        D *= factor;
    }

    PlaneIntersectionType Plane::Intersects(BoundingBox box) const
    {
        PlaneIntersectionType result;
        Intersects(box, result);
        return result;
    }

    void Plane::Intersects(const BoundingBox& box, PlaneIntersectionType& result) const
    {
        box.Intersects(*this, result);
    }

    PlaneIntersectionType Plane::Intersects(BoundingSphere sphere) const
    {
        PlaneIntersectionType result;
        Intersects(sphere, result);
        return result;
    }

    void Plane::Intersects(const BoundingSphere& sphere, PlaneIntersectionType& result) const
    {
        sphere.Intersects(*this, result);
    }

    PlaneIntersectionType Plane::Intersects(const BoundingFrustum& frustum) const
    {
        return frustum.Intersects(*this);
    }

    PlaneIntersectionType Plane::IntersectsPoint(const Vector3& point) const
    {
        float distance;
        DotCoordinate(point, distance);

        if (distance > 0.0f)
        {
            return PlaneIntersectionType::Front;
        }
        if (distance < 0.0f)
        {
            return PlaneIntersectionType::Back;
        }
        return PlaneIntersectionType::Intersecting;
    }

    Plane Plane::Normalize(Plane value)
    {
        Plane result;
        Normalize(value, result);
        return result;
    }

    void Plane::Normalize(const Plane& value, Plane& result)
    {
        float length = value.Normal.Length();
        float factor = 1.0f / length;
        Vector3::Multiply(value.Normal, factor, result.Normal);
        result.D = value.D * factor;
    }

    Plane Plane::Transform(Plane plane, Matrix matrix)
    {
        Plane result;
        Transform(plane, matrix, result);
        return result;
    }

    void Plane::Transform(const Plane& plane, const Matrix& matrix, Plane& result)
    {
        Matrix transformedMatrix;
        Matrix::Invert(matrix, transformedMatrix);
        Matrix::Transpose(transformedMatrix, transformedMatrix);

        Vector4 vector(plane.Normal, plane.D);
        Vector4 transformedVector;
        Vector4::Transform(vector, transformedMatrix, transformedVector);

        result = Plane(transformedVector);
    }

    Plane Plane::Transform(Plane plane, Quaternion rotation)
    {
        Plane result;
        Transform(plane, rotation, result);
        return result;
    }

    void Plane::Transform(const Plane& plane, const Quaternion& rotation, Plane& result)
    {
        Vector3::Transform(plane.Normal, rotation, result.Normal);
        result.D = plane.D;
    }

    bool Plane::Equals(Plane other) const
    {
        return Normal == other.Normal && D == other.D;
    }

    std::size_t Plane::GetHashCode() const
    {
        return Normal.GetHashCode() ^ std::hash<float>{}(D);
    }

    std::string Plane::ToString() const
    {
        std::ostringstream ss;
        ss << "{Normal:" << Normal.ToString() << " D:" << D << "}";
        return ss.str();
    }

    bool operator==(Plane plane1, Plane plane2)
    {
        return plane1.Equals(plane2);
    }

    bool operator!=(Plane plane1, Plane plane2)
    {
        return !plane1.Equals(plane2);
    }
}
