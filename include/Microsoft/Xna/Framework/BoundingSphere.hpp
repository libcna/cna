#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/ContainmentType.hpp"
#include "Microsoft/Xna/Framework/PlaneIntersectionType.hpp"

namespace Microsoft::Xna::Framework
{
    struct BoundingBox;
    class BoundingFrustum;
    struct Matrix;
    struct Plane;
    struct Ray;

    /// Describes a sphere used for 3D bounding and intersection tests.
    struct BoundingSphere
    {
        /// The sphere center.
        Vector3 Center;

        /// The sphere radius.
        float Radius{0.0f};

        BoundingSphere() = default;
        BoundingSphere(Vector3 center, float radius);

        /// Returns this sphere transformed by translation and scale from the matrix.
        [[nodiscard]] BoundingSphere Transform(Matrix matrix) const;

        /// Returns this sphere transformed by translation and scale from the matrix.
        void Transform(const Matrix& matrix, BoundingSphere& result) const;

        void Contains(const BoundingBox& box, ContainmentType& result) const;
        void Contains(const BoundingSphere& sphere, ContainmentType& result) const;
        void Contains(const Vector3& point, ContainmentType& result) const;

        [[nodiscard]] ContainmentType Contains(BoundingBox box) const;
        [[nodiscard]] ContainmentType Contains(const BoundingFrustum& frustum) const;
        [[nodiscard]] ContainmentType Contains(BoundingSphere sphere) const;
        [[nodiscard]] ContainmentType Contains(Vector3 point) const;

        [[nodiscard]] bool Equals(BoundingSphere other) const;

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

        [[nodiscard]] bool Intersects(BoundingBox box) const;
        void Intersects(const BoundingBox& box, bool& result) const;

        [[nodiscard]] bool Intersects(const BoundingFrustum& frustum) const;

        [[nodiscard]] bool Intersects(BoundingSphere sphere) const;
        void Intersects(const BoundingSphere& sphere, bool& result) const;

        [[nodiscard]] std::optional<float> Intersects(Ray ray) const;
        void Intersects(const Ray& ray, std::optional<float>& result) const;

        [[nodiscard]] PlaneIntersectionType Intersects(Plane plane) const;
        void Intersects(const Plane& plane, PlaneIntersectionType& result) const;

        [[nodiscard]] std::size_t GetHashCode() const;
        [[nodiscard]] std::string ToString() const;
    };

    [[nodiscard]] bool operator==(BoundingSphere a, BoundingSphere b);
    [[nodiscard]] bool operator!=(BoundingSphere a, BoundingSphere b);
}
