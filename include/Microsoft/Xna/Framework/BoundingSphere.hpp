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

    /// Describes a sphere used for 3D bounding and intersection tests.
    struct BoundingSphere : public System::IEquatable<BoundingSphere>
    {
        /// The sphere center.
        Vector3 Center;

        /// The sphere radius.
        float Radius{0.0f};

        /// Creates a sphere with zero center and zero radius.
        BoundingSphere() = default;
        /// Creates a sphere with the given center and radius.
        BoundingSphere(Vector3 center, float radius);

        /// Returns this sphere transformed by translation and scale from the matrix.
        [[nodiscard]] BoundingSphere Transform(Matrix matrix) const;

        /// Returns this sphere transformed by translation and scale from the matrix.
        void Transform(const Matrix& matrix, BoundingSphere& result) const;

        /// Output-ref variant; writes the containment result to @p result.
        void Contains(const BoundingBox& box, ContainmentType& result) const;
        /// Output-ref variant; writes the containment result to @p result.
        void Contains(const BoundingSphere& sphere, ContainmentType& result) const;
        /// Output-ref variant; writes the containment result to @p result.
        void Contains(const Vector3& point, ContainmentType& result) const;

        /// Checks whether a box is outside, inside, or overlapping this sphere.
        [[nodiscard]] ContainmentType Contains(BoundingBox box) const;
        /// Checks whether a frustum is outside, inside, or overlapping this sphere.
        [[nodiscard]] ContainmentType Contains(const BoundingFrustum& frustum) const;
        /// Checks whether another sphere is outside, inside, or overlapping this sphere.
        [[nodiscard]] ContainmentType Contains(BoundingSphere sphere) const;
        /// Checks whether a point is outside, inside, or on this sphere.
        [[nodiscard]] ContainmentType Contains(Vector3 point) const;

        /// Compares this sphere with another sphere.
        [[nodiscard]] bool Equals(const BoundingSphere& other) const override;

        /// Creates the smallest sphere that contains a box.
        [[nodiscard]] static BoundingSphere CreateFromBoundingBox(BoundingBox box);

        /// Creates the smallest sphere that contains a box.
        static void CreateFromBoundingBox(const BoundingBox& box, BoundingSphere& result);

        /// Creates the smallest sphere that contains a frustum.
        [[nodiscard]] static BoundingSphere CreateFromFrustum(const BoundingFrustum& frustum);

        /// Creates a sphere that encloses the supplied point set.
        [[nodiscard]] static BoundingSphere CreateFromPoints(const std::vector<Vector3>& points);

        /// Creates the smallest sphere that contains two spheres.
        [[nodiscard]] static BoundingSphere CreateMerged(BoundingSphere original, BoundingSphere additional);

        /// Creates the smallest sphere that contains two spheres.
        static void CreateMerged(const BoundingSphere& original, const BoundingSphere& additional,
                                 BoundingSphere& result);

        /// Checks whether this sphere intersects a box.
        [[nodiscard]] bool Intersects(BoundingBox box) const;
        /// Output-ref variant; writes the box intersection result to @p result.
        void Intersects(const BoundingBox& box, bool& result) const;

        /// Checks whether this sphere intersects a frustum.
        [[nodiscard]] bool Intersects(const BoundingFrustum& frustum) const;

        /// Checks whether this sphere intersects another sphere.
        [[nodiscard]] bool Intersects(BoundingSphere sphere) const;
        /// Output-ref variant; writes the sphere intersection result to @p result.
        void Intersects(const BoundingSphere& sphere, bool& result) const;

        /// Returns the distance along a ray where it hits this sphere, or an empty optional if there is no hit.
        [[nodiscard]] std::optional<float> Intersects(Ray ray) const;
        /// Output-ref variant; writes the intersection distance to @p result, or empty if no hit.
        void Intersects(const Ray& ray, std::optional<float>& result) const;

        /// Classifies this sphere against a plane.
        [[nodiscard]] PlaneIntersectionType Intersects(Plane plane) const;
        /// Output-ref variant; writes the plane intersection type to @p result.
        void Intersects(const Plane& plane, PlaneIntersectionType& result) const;

        /// Returns a hash code for this sphere.
        [[nodiscard]] std::size_t GetHashCode() const;
        /// Returns a string representation of this sphere.
        [[nodiscard]] std::string ToString() const;
    };

    /// Returns true when both spheres have equal center and radius.
    [[nodiscard]] bool operator==(BoundingSphere a, BoundingSphere b);
    /// Returns true when center or radius differs between the spheres.
    [[nodiscard]] bool operator!=(BoundingSphere a, BoundingSphere b);
}
