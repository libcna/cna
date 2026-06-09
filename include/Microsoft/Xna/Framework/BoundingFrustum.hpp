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

        [[nodiscard]] ContainmentType Contains(const BoundingFrustum& frustum) const;
        [[nodiscard]] ContainmentType Contains(BoundingBox box) const;
        void Contains(const BoundingBox& box, ContainmentType& result) const;
        [[nodiscard]] ContainmentType Contains(BoundingSphere sphere) const;
        void Contains(const BoundingSphere& sphere, ContainmentType& result) const;
        [[nodiscard]] ContainmentType Contains(Vector3 point) const;
        void Contains(const Vector3& point, ContainmentType& result) const;

        /// Returns a copy of this frustum's corner points.
        [[nodiscard]] std::vector<Vector3> GetCorners() const;

        /// Copies this frustum's corner points into the supplied vector.
        void GetCorners(std::vector<Vector3>& corners) const;

        [[nodiscard]] bool Intersects(const BoundingFrustum& frustum) const;
        [[nodiscard]] bool Intersects(BoundingBox box) const;
        void Intersects(const BoundingBox& box, bool& result) const;
        [[nodiscard]] bool Intersects(BoundingSphere sphere) const;
        void Intersects(const BoundingSphere& sphere, bool& result) const;
        [[nodiscard]] PlaneIntersectionType Intersects(Plane plane) const;
        void Intersects(const Plane& plane, PlaneIntersectionType& result) const;
        [[nodiscard]] std::optional<float> Intersects(Ray ray) const;
        void Intersects(const Ray& ray, std::optional<float>& result) const;

        [[nodiscard]] bool Equals(const BoundingFrustum& other) const override;
        [[nodiscard]] std::size_t GetHashCode() const;
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

    [[nodiscard]] bool operator==(const BoundingFrustum& a, const BoundingFrustum& b);
    [[nodiscard]] bool operator!=(const BoundingFrustum& a, const BoundingFrustum& b);
}
