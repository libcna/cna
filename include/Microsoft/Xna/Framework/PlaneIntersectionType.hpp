#pragma once

namespace Microsoft::Xna::Framework
{
    /// Describes how a plane relates to a bounding volume.
    enum class PlaneIntersectionType
    {
        /// The volume lies in front of the plane.
        Front,

        /// The volume lies behind the plane.
        Back,

        /// The plane intersects the volume.
        Intersecting
    };
}
