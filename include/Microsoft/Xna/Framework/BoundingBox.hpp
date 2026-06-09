// SPDX-License-Identifier: MS-PL

#pragma once

#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "System/IEquatable.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace Microsoft::Xna::Framework
{
    enum class ContainmentType;
    enum class PlaneIntersectionType;

    struct BoundingFrustum;
    struct BoundingSphere;
    struct Plane;
    struct Ray;

    /// Axis-aligned box represented by its minimum and maximum 3D corners.
    struct BoundingBox : public System::IEquatable<BoundingBox>
    {
    public:
        /// The corner with the smallest X, Y and Z coordinates.
        Vector3 Min;

        /// The corner with the largest X, Y and Z coordinates.
        Vector3 Max;

        /// Number of points returned by GetCorners().
        static constexpr int CornerCount = 8;

    private:
        static const Vector3 MaxVector3;
        static const Vector3 MinVector3;

        [[nodiscard]] std::string getDebugDisplayStringProperty() const;

    public:
        BoundingBox() = default;

        /// Creates a box from minimum and maximum corners.
        BoundingBox(const Vector3& min, const Vector3& max);

        void Contains(const BoundingBox& box, ContainmentType& result) const;
        void Contains(const BoundingSphere& sphere, ContainmentType& result) const;

        /// Checks whether the point is inside this box.
        [[nodiscard]] ContainmentType Contains(const Vector3& point) const;

        /// Checks whether another box is outside, inside, or overlapping this box.
        [[nodiscard]] ContainmentType Contains(const BoundingBox& box) const;

        /// Checks whether a frustum is outside, inside, or overlapping this box.
        [[nodiscard]] ContainmentType Contains(const BoundingFrustum& frustum) const;

        /// Checks whether a sphere is outside, inside, or overlapping this box.
        [[nodiscard]] ContainmentType Contains(const BoundingSphere& sphere) const;

        void Contains(const Vector3& point, ContainmentType& result) const;

        /// Returns the eight corners of this box in XNA corner order.
        [[nodiscard]] std::vector<Vector3> GetCorners() const;

        /// Writes the eight corners into an existing vector. The vector must contain at least CornerCount items.
        void GetCorners(std::vector<Vector3>& corners) const;

        /// Returns the distance along a ray where it hits this box, or an empty optional if there is no hit.
        [[nodiscard]] std::optional<float> Intersects(const Ray& ray) const;

        void Intersects(const Ray& ray, std::optional<float>& result) const;

        /// Checks whether this box intersects a frustum.
        [[nodiscard]] bool Intersects(const BoundingFrustum& frustum) const;

        void Intersects(const BoundingSphere& sphere, bool& result) const;

        /// Checks whether this box intersects another box.
        [[nodiscard]] bool Intersects(const BoundingBox& box) const;

        /// Classifies this box against a plane.
        [[nodiscard]] PlaneIntersectionType Intersects(const Plane& plane) const;

        void Intersects(const BoundingBox& box, bool& result) const;

        /// Checks whether this box intersects a sphere.
        [[nodiscard]] bool Intersects(const BoundingSphere& sphere) const;

        void Intersects(const Plane& plane, PlaneIntersectionType& result) const;

        /// Compares this box with another box.
        [[nodiscard]] bool Equals(const BoundingBox& other) const override;

        /// Creates the smallest box that contains all supplied points.
        [[nodiscard]] static BoundingBox CreateFromPoints(const std::vector<Vector3>& points);

        /// Creates a box that tightly encloses a sphere.
        [[nodiscard]] static BoundingBox CreateFromSphere(const BoundingSphere& sphere);

        static void CreateFromSphere(const BoundingSphere& sphere, BoundingBox& result);

        /// Creates the smallest box that contains both input boxes.
        [[nodiscard]] static BoundingBox CreateMerged(const BoundingBox& original, const BoundingBox& additional);

        static void CreateMerged(const BoundingBox& original, const BoundingBox& additional, BoundingBox& result);

        [[nodiscard]] std::size_t GetHashCode() const;

        [[nodiscard]] std::string ToString() const;
    };

    bool operator==(const BoundingBox& a, const BoundingBox& b);
    bool operator!=(const BoundingBox& a, const BoundingBox& b);
}
