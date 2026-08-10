// SPDX-License-Identifier: MS-PL

#pragma once

namespace Microsoft::Xna::Framework
{
    /** @brief Defines how the bounding volumes intersect or contain one another. */
    enum class ContainmentType
    {
        /** @brief Indicates that there is no overlap between two bounding volumes. */
        Disjoint   = 0,
        /** @brief Indicates that one bounding volume completely contains another volume. */
        Contains   = 1,
        /** @brief Indicates that bounding volumes partially overlap one another. */
        Intersects = 2
    };
}
