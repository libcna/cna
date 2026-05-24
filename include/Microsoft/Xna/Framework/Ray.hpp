#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include "Microsoft/Xna/Framework/Vector3.hpp"

namespace Microsoft::Xna::Framework
{
    struct BoundingBox;
    class BoundingFrustum;
    struct BoundingSphere;
    struct Plane;

    /// Represents a ray with an origin and direction.
    struct Ray
    {
        Vector3 Position;
        Vector3 Direction;

        Ray() = default;
        Ray(Vector3 position, Vector3 direction);

        [[nodiscard]] bool Equals(Ray other) const;
        [[nodiscard]] std::size_t GetHashCode() const;

        [[nodiscard]] std::optional<float> Intersects(BoundingBox box) const;
        void Intersects(const BoundingBox& box, std::optional<float>& result) const;

        [[nodiscard]] std::optional<float> Intersects(BoundingSphere sphere) const;
        void Intersects(const BoundingSphere& sphere, std::optional<float>& result) const;

        [[nodiscard]] std::optional<float> Intersects(Plane plane) const;
        void Intersects(const Plane& plane, std::optional<float>& result) const;

        [[nodiscard]] std::optional<float> Intersects(const BoundingFrustum& frustum) const;

        [[nodiscard]] std::string ToString() const;
    };

    [[nodiscard]] bool operator==(Ray a, Ray b);
    [[nodiscard]] bool operator!=(Ray a, Ray b);
}
