// SPDX-License-Identifier: MS-PL

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

    /// Defines a ray, specified by a starting position and a direction.
    struct Ray
    {
        /// The starting position of this ray.
        Vector3 Position;
        /// The direction of this ray.
        Vector3 Direction;

        /// Constructs a zero-initialized ray.
        Ray() = default;
        /// Constructs a ray with the given starting position and direction.
        Ray(Vector3 position, Vector3 direction);

        /// Returns true when both the position and direction match another ray.
        [[nodiscard]] bool Equals(Ray other) const;
        /// Returns a hash code for this ray.
        [[nodiscard]] std::size_t GetHashCode() const;

        /// Returns the distance along the ray to the first intersection with a bounding box, or nullopt if no hit.
        [[nodiscard]] std::optional<float> Intersects(BoundingBox box) const;
        /// Computes the intersection distance with a bounding box into an output parameter.
        void Intersects(const BoundingBox& box, std::optional<float>& result) const;

        /// Returns the distance along the ray to the first intersection with a bounding sphere, or nullopt if no hit.
        [[nodiscard]] std::optional<float> Intersects(BoundingSphere sphere) const;
        /// Computes the intersection distance with a bounding sphere into an output parameter.
        void Intersects(const BoundingSphere& sphere, std::optional<float>& result) const;

        /// Returns the distance along the ray to the intersection with a plane, or nullopt if no hit.
        [[nodiscard]] std::optional<float> Intersects(Plane plane) const;
        /// Computes the intersection distance with a plane into an output parameter.
        void Intersects(const Plane& plane, std::optional<float>& result) const;

        /// Returns the distance along the ray to the first intersection with a bounding frustum, or nullopt if no hit.
        [[nodiscard]] std::optional<float> Intersects(const BoundingFrustum& frustum) const;

        /// Returns a string representation of this ray.
        [[nodiscard]] std::string ToString() const;
    };

    /// Returns true when both rays have equal position and direction.
    [[nodiscard]] bool operator==(Ray a, Ray b);
    /// Returns true when the rays differ in position or direction.
    [[nodiscard]] bool operator!=(Ray a, Ray b);
}
