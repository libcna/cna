// SPDX-License-Identifier: MS-PL

#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/ContainmentType.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Plane.hpp"
#include "Microsoft/Xna/Framework/PlaneIntersectionType.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "System/IEquatable.hpp"

namespace Microsoft::Xna::Framework
{
    struct BoundingBox;
    struct BoundingSphere;
    struct Ray;

    /// Defines a viewing frustum for intersection tests.
    class BoundingFrustum : public System::IEquatable<BoundingFrustum>
    {
    public:
        /// Number of corner points in the frustum.
        static constexpr int CornerCount = 8;

        /// Constructs a frustum from the given combined view-projection matrix.
        explicit BoundingFrustum(Matrix value);

        /// Gets or sets the matrix used to build this frustum.
        [[nodiscard]] Matrix getMatrixProperty() const;
        void setMatrixProperty(Matrix value);

        /// Gets the near plane.
        [[nodiscard]] Plane getNearProperty() const;

        /// Gets the far plane.
        [[nodiscard]] Plane getFarProperty() const;

        /// Gets the left plane.
        [[nodiscard]] Plane getLeftProperty() const;

        /// Gets the right plane.
        [[nodiscard]] Plane getRightProperty() const;

        /// Gets the top plane.
        [[nodiscard]] Plane getTopProperty() const;

        /// Gets the bottom plane.
        [[nodiscard]] Plane getBottomProperty() const;

        /// Checks whether another frustum is outside, inside, or overlapping this frustum.
        [[nodiscard]] ContainmentType Contains(const BoundingFrustum& frustum) const;
        /// Checks whether a box is outside, inside, or overlapping this frustum.
        [[nodiscard]] ContainmentType Contains(BoundingBox box) const;
        /// Output-ref variant; writes the containment result to @p result.
        void Contains(const BoundingBox& box, ContainmentType& result) const;
        /// Checks whether a sphere is outside, inside, or overlapping this frustum.
        [[nodiscard]] ContainmentType Contains(BoundingSphere sphere) const;
        /// Output-ref variant; writes the containment result to @p result.
        void Contains(const BoundingSphere& sphere, ContainmentType& result) const;
        /// Checks whether a point is outside, inside, or on this frustum.
        [[nodiscard]] ContainmentType Contains(Vector3 point) const;
        /// Output-ref variant; writes the containment result to @p result.
        void Contains(const Vector3& point, ContainmentType& result) const;

        /// Returns a copy of this frustum's corner points.
        [[nodiscard]] std::vector<Vector3> GetCorners() const;

        /// Copies this frustum's corner points into the supplied vector.
        void GetCorners(std::vector<Vector3>& corners) const;

        /// Checks whether this frustum intersects another frustum.
        [[nodiscard]] bool Intersects(const BoundingFrustum& frustum) const;
        /// Checks whether this frustum intersects a box.
        [[nodiscard]] bool Intersects(BoundingBox box) const;
        /// Output-ref variant; writes the box intersection result to @p result.
        void Intersects(const BoundingBox& box, bool& result) const;
        /// Checks whether this frustum intersects a sphere.
        [[nodiscard]] bool Intersects(BoundingSphere sphere) const;
        /// Output-ref variant; writes the sphere intersection result to @p result.
        void Intersects(const BoundingSphere& sphere, bool& result) const;
        /// Classifies this frustum against a plane.
        [[nodiscard]] PlaneIntersectionType Intersects(Plane plane) const;
        /// Output-ref variant; writes the plane intersection type to @p result.
        void Intersects(const Plane& plane, PlaneIntersectionType& result) const;
        /// Returns the distance along a ray where it hits this frustum, or an empty optional if there is no hit.
        [[nodiscard]] std::optional<float> Intersects(Ray ray) const;
        /// Output-ref variant; writes the intersection distance to @p result, or empty if no hit.
        void Intersects(const Ray& ray, std::optional<float>& result) const;

        /// Compares this frustum with another frustum.
        [[nodiscard]] bool Equals(const BoundingFrustum& other) const override;
        /// Returns a hash code for this frustum.
        [[nodiscard]] std::size_t GetHashCode() const;
        /// Returns a string representation of this frustum.
        [[nodiscard]] std::string ToString() const;

    private:
        static constexpr int PlaneCount = 6;

        Matrix matrix;
        std::array<Vector3, CornerCount> corners{};
        std::array<Plane, PlaneCount> planes{};

        void CreateCorners();
        void CreatePlanes();
        static void NormalizePlane(Plane& p);
        static void IntersectionPoint(const Plane& a, const Plane& b, const Plane& c, Vector3& result);
        [[nodiscard]] std::string getDebugDisplayStringProperty() const;
    };

    /// Returns true when both frustums have the same matrix.
    [[nodiscard]] bool operator==(const BoundingFrustum& a, const BoundingFrustum& b);
    /// Returns true when the frustums have different matrices.
    [[nodiscard]] bool operator!=(const BoundingFrustum& a, const BoundingFrustum& b);
}
