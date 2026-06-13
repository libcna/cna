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

    /** @brief Represents a plane in 3D space defined by a normal and a distance from the origin. */
    struct Plane
    {
        /** @brief The normal vector of this plane. */
        Vector3 Normal;
        /** @brief The distance of the plane from the origin along its normal. */
        float D{0.0f};

        /** @brief Constructs a zero-initialized plane. */
        Plane() = default;

        /**
         * @brief Constructs a plane from a Vector4 where XYZ is the normal and W is the distance.
         *
         * @param value A Vector4 whose XYZ components define the normal and W defines the distance.
         */
        explicit Plane(Vector4 value);

        /**
         * @brief Constructs a plane from a normal vector and a distance.
         *
         * @param normal The plane normal vector.
         * @param d The distance from the origin.
         */
        Plane(Vector3 normal, float d);

        /**
         * @brief Constructs a plane that contains the three specified points.
         *
         * @param a The first point on the plane.
         * @param b The second point on the plane.
         * @param c The third point on the plane.
         */
        Plane(Vector3 a, Vector3 b, Vector3 c);

        /**
         * @brief Constructs a plane from individual normal components and a distance.
         *
         * @param a The X component of the normal.
         * @param b The Y component of the normal.
         * @param c The Z component of the normal.
         * @param d The distance from the origin.
         */
        Plane(float a, float b, float c, float d);

        /**
         * @brief Returns the dot product of the plane and a 4D vector.
         *
         * @param value The 4D vector to dot with the plane.
         * @return The dot product result.
         */
        [[nodiscard]] float Dot(Vector4 value) const;

        /**
         * @brief Computes the dot product of the plane and a 4D vector into an output parameter.
         *
         * @param value The 4D vector to dot with the plane.
         * @param result Output scalar that receives the dot product.
         */
        void Dot(const Vector4& value, float& result) const;

        /**
         * @brief Returns the dot product of the plane and the XYZ of a vector, plus the plane distance.
         *
         * @param value The 3D coordinate to test.
         * @return The dot product with coordinate (normal dot value + D).
         */
        [[nodiscard]] float DotCoordinate(Vector3 value) const;

        /**
         * @brief Computes the coordinate dot product into an output parameter.
         *
         * @param value The 3D coordinate to test.
         * @param result Output scalar that receives the dot coordinate result.
         */
        void DotCoordinate(const Vector3& value, float& result) const;

        /**
         * @brief Returns the dot product of the plane normal and a vector.
         *
         * @param value The 3D vector to dot with the plane normal.
         * @return The dot product of the normal and the vector.
         */
        [[nodiscard]] float DotNormal(Vector3 value) const;

        /**
         * @brief Computes the normal dot product into an output parameter.
         *
         * @param value The 3D vector to dot with the plane normal.
         * @param result Output scalar that receives the dot product.
         */
        void DotNormal(const Vector3& value, float& result) const;

        /** @brief Normalizes the plane's normal vector and adjusts D accordingly. */
        void Normalize();

        /**
         * @brief Tests whether a bounding box intersects this plane.
         *
         * @param box The bounding box to test.
         * @return The plane intersection type.
         */
        [[nodiscard]] PlaneIntersectionType Intersects(BoundingBox box) const;

        /**
         * @brief Tests whether a bounding box intersects this plane, storing the result in an output parameter.
         *
         * @param box The bounding box to test.
         * @param result Output that receives the plane intersection type.
         */
        void Intersects(const BoundingBox& box, PlaneIntersectionType& result) const;

        /**
         * @brief Tests whether a bounding sphere intersects this plane.
         *
         * @param sphere The bounding sphere to test.
         * @return The plane intersection type.
         */
        [[nodiscard]] PlaneIntersectionType Intersects(BoundingSphere sphere) const;

        /**
         * @brief Tests whether a bounding sphere intersects this plane, storing the result in an output parameter.
         *
         * @param sphere The bounding sphere to test.
         * @param result Output that receives the plane intersection type.
         */
        void Intersects(const BoundingSphere& sphere, PlaneIntersectionType& result) const;

        /**
         * @brief Tests whether a bounding frustum intersects this plane.
         *
         * @param frustum The bounding frustum to test.
         * @return The plane intersection type.
         */
        [[nodiscard]] PlaneIntersectionType Intersects(const BoundingFrustum& frustum) const;

        /**
         * @brief Returns true if this plane has the same normal and D as another plane.
         *
         * @param other The plane to compare against.
         * @return @c true if the planes are equal; @c false otherwise.
         */
        [[nodiscard]] bool Equals(Plane other) const;

        /**
         * @brief Returns a hash code for this plane.
         *
         * @return Hash code of this plane.
         */
        [[nodiscard]] std::size_t GetHashCode() const;

        /**
         * @brief Returns a string representation of this plane.
         *
         * @return String representation of this plane.
         */
        [[nodiscard]] std::string ToString() const;

        /**
         * @brief Returns a normalized copy of a plane.
         *
         * @param value The plane to normalize.
         * @return The normalized plane.
         */
        [[nodiscard]] static Plane Normalize(Plane value);

        /**
         * @brief Normalizes a plane and stores the result in an output parameter.
         *
         * @param value The plane to normalize.
         * @param result Output plane that receives the normalized result.
         */
        static void Normalize(const Plane& value, Plane& result);

        /**
         * @brief Transforms a normalized plane by a matrix.
         *
         * @param plane The plane to transform.
         * @param matrix The transformation matrix.
         * @return The transformed plane.
         */
        [[nodiscard]] static Plane Transform(Plane plane, Matrix matrix);

        /**
         * @brief Transforms a normalized plane by a matrix, storing the result in an output parameter.
         *
         * @param plane The plane to transform.
         * @param matrix The transformation matrix.
         * @param result Output plane that receives the transformed result.
         */
        static void Transform(const Plane& plane, const Matrix& matrix, Plane& result);

        /**
         * @brief Transforms a normalized plane by a quaternion rotation.
         *
         * @param plane The plane to transform.
         * @param rotation The quaternion rotation to apply.
         * @return The transformed plane.
         */
        [[nodiscard]] static Plane Transform(Plane plane, Quaternion rotation);

        /**
         * @brief Transforms a normalized plane by a quaternion rotation, storing the result in an output parameter.
         *
         * @param plane The plane to transform.
         * @param rotation The quaternion rotation to apply.
         * @param result Output plane that receives the transformed result.
         */
        static void Transform(const Plane& plane, const Quaternion& rotation, Plane& result);

    private:
        [[nodiscard]] PlaneIntersectionType IntersectsPoint(const Vector3& point) const;

        friend class BoundingFrustum;
    };

    /**
     * @brief Returns true if both planes have equal normals and distances.
     *
     * @param plane1 Left-hand plane.
     * @param plane2 Right-hand plane.
     * @return @c true if the planes are equal; @c false otherwise.
     */
    [[nodiscard]] bool operator==(Plane plane1, Plane plane2);

    /**
     * @brief Returns true if the planes differ in any component.
     *
     * @param plane1 Left-hand plane.
     * @param plane2 Right-hand plane.
     * @return @c true if the planes are not equal; @c false otherwise.
     */
    [[nodiscard]] bool operator!=(Plane plane1, Plane plane2);
}
