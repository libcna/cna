// SPDX-License-Identifier: MS-PL

#pragma once

namespace Microsoft::Xna::Framework
{
    /** @brief Defines the intersection between a Plane and a bounding volume. */
    enum class PlaneIntersectionType
    {
        /** @brief There is no intersection; the bounding volume is in the negative half-space of the plane. */
        Front,

        /** @brief There is no intersection; the bounding volume is in the positive half-space of the plane. */
        Back,

        /** @brief The plane intersects the bounding volume. */
        Intersecting
    };
}
