// SPDX-License-Identifier: MS-PL

#pragma once

namespace Microsoft::Xna::Framework
{
    /// Defines the intersection between a Plane and a bounding volume.
    enum class PlaneIntersectionType
    {
        /// There is no intersection; the bounding volume is in the positive half-space of the plane.
        Front,

        /// There is no intersection; the bounding volume is in the negative half-space of the plane.
        Back,

        /// The plane intersects the bounding volume.
        Intersecting
    };
}
