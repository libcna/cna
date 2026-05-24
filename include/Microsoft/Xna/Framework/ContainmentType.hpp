#pragma once

namespace Microsoft::Xna::Framework
{
    /// Defines how one bounding volume relates to another.
    enum class ContainmentType
    {
        /// The two bounding volumes do not overlap.
        Disjoint,

        /// One bounding volume fully contains the other.
        Contains,

        /// The bounding volumes overlap without full containment.
        Intersects
    };
}
