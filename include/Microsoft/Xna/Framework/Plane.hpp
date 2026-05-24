#pragma once

#include <cstddef>
#include <string>

#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/PlaneIntersectionType.hpp"

namespace Microsoft::Xna::Framework
{
    struct BoundingBox;
    class BoundingFrustum;
    struct BoundingSphere;
    struct Matrix;
    struct Quaternion;

    /// Represents a plane in 3D space.
    struct Plane
    {
        Vector3 Normal;
        float D{0.0f};

        Plane() = default;
        explicit Plane(Vector4 value);
        Plane(Vector3 normal, float d);
        Plane(Vector3 a, Vector3 b, Vector3 c);
        Plane(float a, float b, float c, float d);

        [[nodiscard]] float Dot(Vector4 value) const;
        void Dot(const Vector4& value, float& result) const;

        [[nodiscard]] float DotCoordinate(Vector3 value) const;
        void DotCoordinate(const Vector3& value, float& result) const;

        [[nodiscard]] float DotNormal(Vector3 value) const;
        void DotNormal(const Vector3& value, float& result) const;

        void Normalize();

        [[nodiscard]] PlaneIntersectionType Intersects(BoundingBox box) const;
        void Intersects(const BoundingBox& box, PlaneIntersectionType& result) const;

        [[nodiscard]] PlaneIntersectionType Intersects(BoundingSphere sphere) const;
        void Intersects(const BoundingSphere& sphere, PlaneIntersectionType& result) const;

        [[nodiscard]] PlaneIntersectionType Intersects(const BoundingFrustum& frustum) const;

        [[nodiscard]] bool Equals(Plane other) const;
        [[nodiscard]] std::size_t GetHashCode() const;
        [[nodiscard]] std::string ToString() const;

        [[nodiscard]] static Plane Normalize(Plane value);
        static void Normalize(const Plane& value, Plane& result);

        /// Transforms a normalized plane by a matrix.
        [[nodiscard]] static Plane Transform(Plane plane, Matrix matrix);

        /// Transforms a normalized plane by a matrix.
        static void Transform(const Plane& plane, const Matrix& matrix, Plane& result);

        /// Transforms a normalized plane by a quaternion rotation.
        [[nodiscard]] static Plane Transform(Plane plane, Quaternion rotation);

        /// Transforms a normalized plane by a quaternion rotation.
        static void Transform(const Plane& plane, const Quaternion& rotation, Plane& result);

    private:
        [[nodiscard]] PlaneIntersectionType IntersectsPoint(const Vector3& point) const;

        friend class BoundingFrustum;
    };

    [[nodiscard]] bool operator==(Plane plane1, Plane plane2);
    [[nodiscard]] bool operator!=(Plane plane1, Plane plane2);
}
