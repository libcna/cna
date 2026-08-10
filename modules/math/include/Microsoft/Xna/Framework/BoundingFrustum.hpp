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

    /** @brief Defines a viewing frustum for intersection tests. */
    class BoundingFrustum : public System::IEquatable<BoundingFrustum>
    {
    public:
        /** @brief Number of corner points in the frustum. */
        static constexpr int CornerCount = 8;

        /**
         * @brief Constructs a frustum from the given combined view-projection matrix.
         *
         * @param value The combined view-projection matrix.
         */
        explicit BoundingFrustum(Matrix value);

        /**
         * @brief Gets the matrix used to build this frustum.
         *
         * @return The current combined view-projection matrix.
         */
        [[nodiscard]] Matrix getMatrixProperty() const;

        /**
         * @brief Sets the matrix used to build this frustum.
         *
         * @param value The new combined view-projection matrix.
         */
        void setMatrixProperty(Matrix value);

        /**
         * @brief Gets the near plane.
         *
         * @return The near clipping plane.
         */
        [[nodiscard]] Plane getNearProperty() const;

        /**
         * @brief Gets the far plane.
         *
         * @return The far clipping plane.
         */
        [[nodiscard]] Plane getFarProperty() const;

        /**
         * @brief Gets the left plane.
         *
         * @return The left clipping plane.
         */
        [[nodiscard]] Plane getLeftProperty() const;

        /**
         * @brief Gets the right plane.
         *
         * @return The right clipping plane.
         */
        [[nodiscard]] Plane getRightProperty() const;

        /**
         * @brief Gets the top plane.
         *
         * @return The top clipping plane.
         */
        [[nodiscard]] Plane getTopProperty() const;

        /**
         * @brief Gets the bottom plane.
         *
         * @return The bottom clipping plane.
         */
        [[nodiscard]] Plane getBottomProperty() const;

        /**
         * @brief Checks whether another frustum is outside, inside, or overlapping this frustum.
         *
         * @param frustum The frustum to test.
         * @return The containment type describing the relationship.
         */
        [[nodiscard]] ContainmentType Contains(const BoundingFrustum& frustum) const;

        /**
         * @brief Checks whether a box is outside, inside, or overlapping this frustum.
         *
         * @param box The box to test.
         * @return The containment type describing the relationship.
         */
        [[nodiscard]] ContainmentType Contains(BoundingBox box) const;

        /**
         * @brief Output-ref variant; writes the containment result to @p result.
         *
         * @param box The box to test.
         * @param result Output that receives the containment type.
         */
        void Contains(const BoundingBox& box, ContainmentType& result) const;

        /**
         * @brief Checks whether a sphere is outside, inside, or overlapping this frustum.
         *
         * @param sphere The sphere to test.
         * @return The containment type describing the relationship.
         */
        [[nodiscard]] ContainmentType Contains(BoundingSphere sphere) const;

        /**
         * @brief Output-ref variant; writes the containment result to @p result.
         *
         * @param sphere The sphere to test.
         * @param result Output that receives the containment type.
         */
        void Contains(const BoundingSphere& sphere, ContainmentType& result) const;

        /**
         * @brief Checks whether a point is outside, inside, or on this frustum.
         *
         * @param point The point to test.
         * @return The containment type describing the relationship.
         */
        [[nodiscard]] ContainmentType Contains(Vector3 point) const;

        /**
         * @brief Output-ref variant; writes the containment result to @p result.
         *
         * @param point The point to test.
         * @param result Output that receives the containment type.
         */
        void Contains(const Vector3& point, ContainmentType& result) const;

        /**
         * @brief Returns a copy of this frustum's corner points.
         *
         * @return A vector of eight corner positions.
         */
        [[nodiscard]] std::vector<Vector3> GetCorners() const;

        /**
         * @brief Copies this frustum's corner points into the supplied vector.
         *
         * @param corners Output vector that receives the corner positions.
         */
        void GetCorners(std::vector<Vector3>& corners) const;

        /**
         * @brief Checks whether this frustum intersects another frustum.
         *
         * @param frustum The frustum to test.
         * @return @c true if the frustums intersect; @c false otherwise.
         */
        [[nodiscard]] bool Intersects(const BoundingFrustum& frustum) const;

        /**
         * @brief Checks whether this frustum intersects a box.
         *
         * @param box The box to test.
         * @return @c true if the frustum and box intersect; @c false otherwise.
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
         * @brief Checks whether this frustum intersects a sphere.
         *
         * @param sphere The sphere to test.
         * @return @c true if the frustum and sphere intersect; @c false otherwise.
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
         * @brief Classifies this frustum against a plane.
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
         * @brief Returns the distance along a ray where it hits this frustum, or an empty optional if there is no hit.
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
         * @brief Compares this frustum with another frustum.
         *
         * @param other The frustum to compare against.
         * @return @c true if the frustums are equal; @c false otherwise.
         */
        [[nodiscard]] bool Equals(const BoundingFrustum& other) const override;

        /**
         * @brief Returns a hash code for this frustum.
         *
         * @return Hash code of this bounding frustum.
         */
        [[nodiscard]] std::size_t GetHashCode() const;

        /**
         * @brief Returns a string representation of this frustum.
         *
         * @return String representation of this bounding frustum.
         */
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

    /**
     * @brief Returns true when both frustums have the same matrix.
     *
     * @param a Left-hand bounding frustum.
     * @param b Right-hand bounding frustum.
     * @return @c true if the frustums are equal; @c false otherwise.
     */
    [[nodiscard]] bool operator==(const BoundingFrustum& a, const BoundingFrustum& b);

    /**
     * @brief Returns true when the frustums have different matrices.
     *
     * @param a Left-hand bounding frustum.
     * @param b Right-hand bounding frustum.
     * @return @c true if the frustums are not equal; @c false otherwise.
     */
    [[nodiscard]] bool operator!=(const BoundingFrustum& a, const BoundingFrustum& b);
}
