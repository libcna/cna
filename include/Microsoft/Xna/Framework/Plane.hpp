// SPDX-License-Identifier: MS-PL

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

    /// Represents a plane in 3D space defined by a normal and a distance from the origin.
    struct Plane
    {
        /// The normal vector of this plane.
        Vector3 Normal;
        /// The distance of the plane from the origin along its normal.
        float D{0.0f};

        /// Constructs a zero-initialized plane.
        Plane() = default;
        /// Constructs a plane from a Vector4 where XYZ is the normal and W is the distance.
        explicit Plane(Vector4 value);
        /// Constructs a plane from a normal vector and a distance.
        Plane(Vector3 normal, float d);
        /// Constructs a plane that contains the three specified points.
        Plane(Vector3 a, Vector3 b, Vector3 c);
        /// Constructs a plane from individual normal components and a distance.
        Plane(float a, float b, float c, float d);

        /// Returns the dot product of the plane and a 4D vector.
        [[nodiscard]] float Dot(Vector4 value) const;
        /// Computes the dot product of the plane and a 4D vector into an output parameter.
        void Dot(const Vector4& value, float& result) const;

        /// Returns the dot product of the plane and the XYZ of a vector, plus the plane distance.
        [[nodiscard]] float DotCoordinate(Vector3 value) const;
        /// Computes the coordinate dot product into an output parameter.
        void DotCoordinate(const Vector3& value, float& result) const;

        /// Returns the dot product of the plane normal and a vector.
        [[nodiscard]] float DotNormal(Vector3 value) const;
        /// Computes the normal dot product into an output parameter.
        void DotNormal(const Vector3& value, float& result) const;

        /// Normalizes the plane's normal vector and adjusts D accordingly.
        void Normalize();

        /// Tests whether a bounding box intersects this plane.
        [[nodiscard]] PlaneIntersectionType Intersects(BoundingBox box) const;
        /// Tests whether a bounding box intersects this plane, storing the result in an output parameter.
        void Intersects(const BoundingBox& box, PlaneIntersectionType& result) const;

        /// Tests whether a bounding sphere intersects this plane.
        [[nodiscard]] PlaneIntersectionType Intersects(BoundingSphere sphere) const;
        /// Tests whether a bounding sphere intersects this plane, storing the result in an output parameter.
        void Intersects(const BoundingSphere& sphere, PlaneIntersectionType& result) const;

        /// Tests whether a bounding frustum intersects this plane.
        [[nodiscard]] PlaneIntersectionType Intersects(const BoundingFrustum& frustum) const;

        /// Returns true if this plane has the same normal and D as another plane.
        [[nodiscard]] bool Equals(Plane other) const;
        /// Returns a hash code for this plane.
        [[nodiscard]] std::size_t GetHashCode() const;
        /// Returns a string representation of this plane.
        [[nodiscard]] std::string ToString() const;

        /// Returns a normalized copy of a plane.
        [[nodiscard]] static Plane Normalize(Plane value);
        /// Normalizes a plane and stores the result in an output parameter.
        static void Normalize(const Plane& value, Plane& result);

        /// Transforms a normalized plane by a matrix.
        [[nodiscard]] static Plane Transform(Plane plane, Matrix matrix);

        /// Transforms a normalized plane by a matrix, storing the result in an output parameter.
        static void Transform(const Plane& plane, const Matrix& matrix, Plane& result);

        /// Transforms a normalized plane by a quaternion rotation.
        [[nodiscard]] static Plane Transform(Plane plane, Quaternion rotation);

        /// Transforms a normalized plane by a quaternion rotation, storing the result in an output parameter.
        static void Transform(const Plane& plane, const Quaternion& rotation, Plane& result);

    private:
        [[nodiscard]] PlaneIntersectionType IntersectsPoint(const Vector3& point) const;

        friend class BoundingFrustum;
    };

    /// Returns true if both planes have equal normals and distances.
    [[nodiscard]] bool operator==(Plane plane1, Plane plane2);
    /// Returns true if the planes differ in any component.
    [[nodiscard]] bool operator!=(Plane plane1, Plane plane2);
}
