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

    /** @brief Defines a ray, specified by a starting position and a direction. */
    struct Ray
    {
        /** @brief The starting position of this ray. */
        Vector3 Position;
        /** @brief The direction of this ray. */
        Vector3 Direction;

        /** @brief Constructs a zero-initialized ray. */
        Ray() = default;

        /**
         * @brief Constructs a ray with the given starting position and direction.
         *
         * @param position The starting position of the ray.
         * @param direction The direction of the ray.
         */
        Ray(Vector3 position, Vector3 direction);

        /**
         * @brief Returns true when both the position and direction match another ray.
         *
         * @param other The ray to compare against.
         * @return @c true if the rays are equal; @c false otherwise.
         */
        [[nodiscard]] bool Equals(Ray other) const;

        /**
         * @brief Returns a hash code for this ray.
         *
         * @return Hash code of this ray.
         */
        [[nodiscard]] std::size_t GetHashCode() const;

        /**
         * @brief Returns the distance along the ray to the first intersection with a bounding box, or nullopt if no hit.
         *
         * @param box The bounding box to test.
         * @return The intersection distance, or empty if no intersection.
         */
        [[nodiscard]] std::optional<float> Intersects(BoundingBox box) const;

        /**
         * @brief Computes the intersection distance with a bounding box into an output parameter.
         *
         * @param box The bounding box to test.
         * @param result Output that receives the intersection distance or empty.
         */
        void Intersects(const BoundingBox& box, std::optional<float>& result) const;

        /**
         * @brief Returns the distance along the ray to the first intersection with a bounding sphere, or nullopt if no hit.
         *
         * @param sphere The bounding sphere to test.
         * @return The intersection distance, or empty if no intersection.
         */
        [[nodiscard]] std::optional<float> Intersects(BoundingSphere sphere) const;

        /**
         * @brief Computes the intersection distance with a bounding sphere into an output parameter.
         *
         * @param sphere The bounding sphere to test.
         * @param result Output that receives the intersection distance or empty.
         */
        void Intersects(const BoundingSphere& sphere, std::optional<float>& result) const;

        /**
         * @brief Returns the distance along the ray to the intersection with a plane, or nullopt if no hit.
         *
         * @param plane The plane to test.
         * @return The intersection distance, or empty if no intersection.
         */
        [[nodiscard]] std::optional<float> Intersects(Plane plane) const;

        /**
         * @brief Computes the intersection distance with a plane into an output parameter.
         *
         * @param plane The plane to test.
         * @param result Output that receives the intersection distance or empty.
         */
        void Intersects(const Plane& plane, std::optional<float>& result) const;

        /**
         * @brief Returns the distance along the ray to the first intersection with a bounding frustum, or nullopt if no hit.
         *
         * @param frustum The bounding frustum to test.
         * @return The intersection distance, or empty if no intersection.
         */
        [[nodiscard]] std::optional<float> Intersects(const BoundingFrustum& frustum) const;

        /**
         * @brief Returns a string representation of this ray.
         *
         * @return String representation of this ray.
         */
        [[nodiscard]] std::string ToString() const;
    };

    /**
     * @brief Returns true when both rays have equal position and direction.
     *
     * @param a Left-hand ray.
     * @param b Right-hand ray.
     * @return @c true if the rays are equal; @c false otherwise.
     */
    [[nodiscard]] bool operator==(Ray a, Ray b);

    /**
     * @brief Returns true when the rays differ in position or direction.
     *
     * @param a Left-hand ray.
     * @param b Right-hand ray.
     * @return @c true if the rays are not equal; @c false otherwise.
     */
    [[nodiscard]] bool operator!=(Ray a, Ray b);
}
