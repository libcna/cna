// SPDX-License-Identifier: MS-PL

#pragma once

namespace Microsoft::Xna::Framework
{
    /// Defines how one bounding volume relates to another.
    enum class ContainmentType
    {
        Disjoint   = 0, ///< The two bounding volumes do not overlap.
        Contains   = 1, ///< One bounding volume fully contains the other.
        Intersects = 2  ///< The bounding volumes partially overlap.
    };
}
