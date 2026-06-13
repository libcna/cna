// SPDX-License-Identifier: MS-PL

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/ContainmentType.hpp"
#include "Microsoft/Xna/Framework/PlaneIntersectionType.hpp"
#include "System/IEquatable.hpp"

namespace Microsoft::Xna::Framework
{
    struct BoundingBox;
    class BoundingFrustum;
    struct Matrix;
    struct Plane;
    struct Ray;

    /** @brief Describes a sphere used for 3D bounding and intersection tests. */
    struct BoundingSphere : public System::IEquatable<BoundingSphere>
    {
        /** @brief The sphere center. */
        Vector3 Center;

        /** @brief The sphere radius. */
        float Radius{0.0f};

        /** @brief Creates a sphere with zero center and zero radius. */
        BoundingSphere() = default;

        /**
         * @brief Creates a sphere with the given center and radius.
         *
         * @param center The center of the sphere.
         * @param radius The radius of the sphere.
         */
        BoundingSphere(Vector3 center, float radius);

        /**
         * @brief Returns this sphere transformed by translation and scale from the matrix.
         *
         * @param matrix The transformation matrix.
         * @return The transformed bounding sphere.
         */
        [[nodiscard]] BoundingSphere Transform(Matrix matrix) const;

        /**
         * @brief Returns this sphere transformed by translation and scale from the matrix.
         *
         * @param matrix The transformation matrix.
         * @param result Output sphere that receives the transformed result.
         */
        void Transform(const Matrix& matrix, BoundingSphere& result) const;

        /**
         * @brief Output-ref variant; writes the containment result to @p result.
         *
         * @param box The box to test.
         * @param result Output that receives the containment type.
         */
        void Contains(const BoundingBox& box, ContainmentType& result) const;

        /**
         * @brief Output-ref variant; writes the containment result to @p result.
         *
         * @param sphere The sphere to test.
         * @param result Output that receives the containment type.
         */
        void Contains(const BoundingSphere& sphere, ContainmentType& result) const;

        /**
         * @brief Output-ref variant; writes the containment result to @p result.
         *
         * @param point The point to test.
         * @param result Output that receives the containment type.
         */
        void Contains(const Vector3& point, ContainmentType& result) const;

        /**
         * @brief Checks whether a box is outside, inside, or overlapping this sphere.
         *
         * @param box The box to test.
         * @return The containment type describing the relationship.
         */
        [[nodiscard]] ContainmentType Contains(BoundingBox box) const;

        /**
         * @brief Checks whether a frustum is outside, inside, or overlapping this sphere.
         *
         * @param frustum The frustum to test.
         * @return The containment type describing the relationship.
         */
        [[nodiscard]] ContainmentType Contains(const BoundingFrustum& frustum) const;

        /**
         * @brief Checks whether another sphere is outside, inside, or overlapping this sphere.
         *
         * @param sphere The sphere to test.
         * @return The containment type describing the relationship.
         */
        [[nodiscard]] ContainmentType Contains(BoundingSphere sphere) const;

        /**
         * @brief Checks whether a point is outside, inside, or on this sphere.
         *
         * @param point The point to test.
         * @return The containment type describing the relationship.
         */
        [[nodiscard]] ContainmentType Contains(Vector3 point) const;

        /**
         * @brief Compares this sphere with another sphere.
         *
         * @param other The sphere to compare against.
         * @return @c true if the spheres are equal; @c false otherwise.
         */
        [[nodiscard]] bool Equals(const BoundingSphere& other) const override;

        /**
         * @brief Creates the smallest sphere that contains a box.
         *
         * @param box The box to enclose.
         * @return The smallest bounding sphere that contains the box.
         */
        [[nodiscard]] static BoundingSphere CreateFromBoundingBox(BoundingBox box);

        /**
         * @brief Creates the smallest sphere that contains a box.
         *
         * @param box The box to enclose.
         * @param result Output sphere that receives the result.
         */
        static void CreateFromBoundingBox(const BoundingBox& box, BoundingSphere& result);

        /**
         * @brief Creates the smallest sphere that contains a frustum.
         *
         * @param frustum The frustum to enclose.
         * @return The smallest bounding sphere that contains the frustum.
         */
        [[nodiscard]] static BoundingSphere CreateFromFrustum(const BoundingFrustum& frustum);

        /**
         * @brief Creates a sphere that encloses the supplied point set.
         *
         * @param points The collection of points to enclose.
         * @return The bounding sphere that encloses all points.
         */
        [[nodiscard]] static BoundingSphere CreateFromPoints(const std::vector<Vector3>& points);

        /**
         * @brief Creates the smallest sphere that contains two spheres.
         *
         * @param original The first sphere.
         * @param additional The second sphere.
         * @return The merged bounding sphere.
         */
        [[nodiscard]] static BoundingSphere CreateMerged(BoundingSphere original, BoundingSphere additional);

        /**
         * @brief Creates the smallest sphere that contains two spheres.
         *
         * @param original The first sphere.
         * @param additional The second sphere.
         * @param result Output sphere that receives the merged result.
         */
        static void CreateMerged(const BoundingSphere& original, const BoundingSphere& additional,
                                 BoundingSphere& result);

        /**
         * @brief Checks whether this sphere intersects a box.
         *
         * @param box The box to test.
         * @return @c true if the sphere and box intersect; @c false otherwise.
         */
        [[nodiscard]] bool Intersects(BoundingBox box) const;

        /**
         * @brief Output-ref variant; writes the box intersection result to @p result.
         *
         * @param box The box to test.
         * @param result Output that receives the intersection result.
         */
        void Intersects(const BoundingBox& box, bool& result) const;

        /**
         * @brief Checks whether this sphere intersects a frustum.
         *
         * @param frustum The frustum to test.
         * @return @c true if the sphere and frustum intersect; @c false otherwise.
         */
        [[nodiscard]] bool Intersects(const BoundingFrustum& frustum) const;

        /**
         * @brief Checks whether this sphere intersects another sphere.
         *
         * @param sphere The sphere to test.
         * @return @c true if the spheres intersect; @c false otherwise.
         */
        [[nodiscard]] bool Intersects(BoundingSphere sphere) const;

        /**
         * @brief Output-ref variant; writes the sphere intersection result to @p result.
         *
         * @param sphere The sphere to test.
         * @param result Output that receives the intersection result.
         */
        void Intersects(const BoundingSphere& sphere, bool& result) const;

        /**
         * @brief Returns the distance along a ray where it hits this sphere, or an empty optional if there is no hit.
         *
         * @param ray The ray to test.
         * @return The intersection distance, or empty if no intersection.
         */
        [[nodiscard]] std::optional<float> Intersects(Ray ray) const;

        /**
         * @brief Output-ref variant; writes the intersection distance to @p result, or empty if no hit.
         *
         * @param ray The ray to test.
         * @param result Output that receives the intersection distance or empty.
         */
        void Intersects(const Ray& ray, std::optional<float>& result) const;

        /**
         * @brief Classifies this sphere against a plane.
         *
         * @param plane The plane to test against.
         * @return The plane intersection type.
         */
        [[nodiscard]] PlaneIntersectionType Intersects(Plane plane) const;

        /**
         * @brief Output-ref variant; writes the plane intersection type to @p result.
         *
         * @param plane The plane to test against.
         * @param result Output that receives the plane intersection type.
         */
        void Intersects(const Plane& plane, PlaneIntersectionType& result) const;

        /**
         * @brief Returns a hash code for this sphere.
         *
         * @return Hash code of this bounding sphere.
         */
        [[nodiscard]] std::size_t GetHashCode() const;

        /**
         * @brief Returns a string representation of this sphere.
         *
         * @return String representation of this bounding sphere.
         */
        [[nodiscard]] std::string ToString() const;
    };

    /**
     * @brief Returns true when both spheres have equal center and radius.
     *
     * @param a Left-hand bounding sphere.
     * @param b Right-hand bounding sphere.
     * @return @c true if the spheres are equal; @c false otherwise.
     */
    [[nodiscard]] bool operator==(BoundingSphere a, BoundingSphere b);

    /**
     * @brief Returns true when center or radius differs between the spheres.
     *
     * @param a Left-hand bounding sphere.
     * @param b Right-hand bounding sphere.
     * @return @c true if the spheres are not equal; @c false otherwise.
     */
    [[nodiscard]] bool operator!=(BoundingSphere a, BoundingSphere b);
}
