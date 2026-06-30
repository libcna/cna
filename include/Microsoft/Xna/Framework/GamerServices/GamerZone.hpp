// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::GamerServices
{
    /**
     * @brief Describes the gaming zone a gamer has selected in their profile.
     */
    enum class GamerZone
    {
        /** @brief Zone is unknown. */
        Unknown,
        /** @brief Recreation zone — casual gaming. */
        Recreation,
        /** @brief Pro zone — competitive gaming. */
        Pro,
        /** @brief Family zone — family-friendly gaming. */
        Family,
        /** @brief Underground zone — unrated content. */
        Underground
    };
}
