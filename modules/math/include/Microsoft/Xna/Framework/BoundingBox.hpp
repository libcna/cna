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

    class BoundingFrustum;
    struct BoundingSphere;
    struct Plane;
    struct Ray;

    /** @brief Axis-aligned box represented by its minimum and maximum 3D corners. */
    struct BoundingBox : public System::IEquatable<BoundingBox>
    {
    public:
        /** @brief The corner with the smallest X, Y and Z coordinates. */
        Vector3 Min;

        /** @brief The corner with the largest X, Y and Z coordinates. */
        Vector3 Max;

        /** @brief Number of points returned by GetCorners(). */
        static constexpr int CornerCount = 8;

    private:
        static const Vector3 MaxVector3;
        static const Vector3 MinVector3;

        [[nodiscard]] std::string getDebugDisplayStringProperty() const;

    public:
        /** @brief Creates a box with zero-initialised Min and Max corners. */
        BoundingBox() = default;

        /**
         * @brief Creates a box from minimum and maximum corners.
         *
         * @param min The corner with the smallest coordinates.
         * @param max The corner with the largest coordinates.
         */
        BoundingBox(const Vector3& min, const Vector3& max);

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
         * @brief Checks whether the point is inside this box.
         *
         * @param point The point to test.
         * @return The containment type describing the relationship.
         */
        [[nodiscard]] ContainmentType Contains(const Vector3& point) const;

        /**
         * @brief Checks whether another box is outside, inside, or overlapping this box.
         *
         * @param box The box to test.
         * @return The containment type describing the relationship.
         */
        [[nodiscard]] ContainmentType Contains(const BoundingBox& box) const;

        /**
         * @brief Checks whether a frustum is outside, inside, or overlapping this box.
         *
         * @param frustum The frustum to test.
         * @return The containment type describing the relationship.
         */
        [[nodiscard]] ContainmentType Contains(const BoundingFrustum& frustum) const;

        /**
         * @brief Checks whether a sphere is outside, inside, or overlapping this box.
         *
         * @param sphere The sphere to test.
         * @return The containment type describing the relationship.
         */
        [[nodiscard]] ContainmentType Contains(const BoundingSphere& sphere) const;

        /**
         * @brief Output-ref variant; writes the containment result to @p result.
         *
         * @param point The point to test.
         * @param result Output that receives the containment type.
         */
        void Contains(const Vector3& point, ContainmentType& result) const;

        /**
         * @brief Returns the eight corners of this box in XNA corner order.
         *
         * @return A vector of eight corner positions.
         */
        [[nodiscard]] std::vector<Vector3> GetCorners() const;

        /**
         * @brief Writes the eight corners into an existing vector. The vector must contain at least CornerCount items.
         *
         * @param corners Output vector that receives the corner positions.
         */
        void GetCorners(std::vector<Vector3>& corners) const;

        /**
         * @brief Returns the distance along a ray where it hits this box, or an empty optional if there is no hit.
         *
         * @param ray The ray to test.
         * @return The intersection distance, or empty if no intersection.
         */
        [[nodiscard]] std::optional<float> Intersects(const Ray& ray) const;

        /**
         * @brief Output-ref variant; writes the intersection distance to @p result, or empty if no hit.
         *
         * @param ray The ray to test.
         * @param result Output that receives the intersection distance or empty.
         */
        void Intersects(const Ray& ray, std::optional<float>& result) const;

        /**
         * @brief Checks whether this box intersects a frustum.
         *
         * @param frustum The frustum to test.
         * @return @c true if the box and frustum intersect; @c false otherwise.
         */
        [[nodiscard]] bool Intersects(const BoundingFrustum& frustum) const;

        /**
         * @brief Output-ref variant; writes the sphere intersection result to @p result.
         *
         * @param sphere The sphere to test.
         * @param result Output that receives the intersection result.
         */
        void Intersects(const BoundingSphere& sphere, bool& result) const;

        /**
         * @brief Checks whether this box intersects another box.
         *
         * @param box The box to test.
         * @return @c true if the boxes intersect; @c false otherwise.
         */
        [[nodiscard]] bool Intersects(const BoundingBox& box) const;

        /**
         * @brief Classifies this box against a plane.
         *
         * @param plane The plane to test against.
         * @return The plane intersection type.
         */
        [[nodiscard]] PlaneIntersectionType Intersects(const Plane& plane) const;

        /**
         * @brief Output-ref variant; writes the box intersection result to @p result.
         *
         * @param box The box to test.
         * @param result Output that receives the intersection result.
         */
        void Intersects(const BoundingBox& box, bool& result) const;

        /**
         * @brief Checks whether this box intersects a sphere.
         *
         * @param sphere The sphere to test.
         * @return @c true if the box and sphere intersect; @c false otherwise.
         */
        [[nodiscard]] bool Intersects(const BoundingSphere& sphere) const;

        /**
         * @brief Output-ref variant; writes the plane intersection type to @p result.
         *
         * @param plane The plane to test against.
         * @param result Output that receives the plane intersection type.
         */
        void Intersects(const Plane& plane, PlaneIntersectionType& result) const;

        /**
         * @brief Compares this box with another box.
         *
         * @param other The box to compare against.
         * @return @c true if the boxes are equal; @c false otherwise.
         */
        [[nodiscard]] bool Equals(const BoundingBox& other) const override;

        /**
         * @brief Creates the smallest box that contains all supplied points.
         *
         * @param points A collection of points to enclose.
         * @return The smallest bounding box that contains all points.
         */
        [[nodiscard]] static BoundingBox CreateFromPoints(const std::vector<Vector3>& points);

        /**
         * @brief Creates a box that tightly encloses a sphere.
         *
         * @param sphere The sphere to enclose.
         * @return The smallest bounding box that contains the sphere.
         */
        [[nodiscard]] static BoundingBox CreateFromSphere(const BoundingSphere& sphere);

        /**
         * @brief Output-ref variant; writes the result box to @p result.
         *
         * @param sphere The sphere to enclose.
         * @param result Output box that receives the result.
         */
        static void CreateFromSphere(const BoundingSphere& sphere, BoundingBox& result);

        /**
         * @brief Creates the smallest box that contains both input boxes.
         *
         * @param original The first box.
         * @param additional The second box.
         * @return The merged bounding box.
         */
        [[nodiscard]] static BoundingBox CreateMerged(const BoundingBox& original, const BoundingBox& additional);

        /**
         * @brief Output-ref variant; writes the merged box to @p result.
         *
         * @param original The first box.
         * @param additional The second box.
         * @param result Output box that receives the merged result.
         */
        static void CreateMerged(const BoundingBox& original, const BoundingBox& additional, BoundingBox& result);

        /**
         * @brief Returns a hash code for this box.
         *
         * @return Hash code of this bounding box.
         */
        [[nodiscard]] std::size_t GetHashCode() const;

        /**
         * @brief Returns a string representation of this box.
         *
         * @return String representation of this bounding box.
         */
        [[nodiscard]] std::string ToString() const;
    };

    /**
     * @brief Returns true when both boxes have equal Min and Max.
     *
     * @param a Left-hand bounding box.
     * @param b Right-hand bounding box.
     * @return @c true if the boxes are equal; @c false otherwise.
     */
    bool operator==(const BoundingBox& a, const BoundingBox& b);

    /**
     * @brief Returns true when either Min or Max differs between the boxes.
     *
     * @param a Left-hand bounding box.
     * @param b Right-hand bounding box.
     * @return @c true if the boxes are not equal; @c false otherwise.
     */
    bool operator!=(const BoundingBox& a, const BoundingBox& b);
}
