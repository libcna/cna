// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Net
{
    /**
     * @brief Describes the type of a network session.
     */
    enum class NetworkSessionType
    {
        /** @brief A session played entirely on the local machine. */
        Local,
        /** @brief A session played over a local area network. */
        SystemLink,
        /** @brief A session that matches players with similar skill/preferences. */
        PlayerMatch,
        /** @brief A competitive, ranked session. */
        Ranked,
        /** @brief A local session that also reports to leaderboards. */
        LocalWithLeaderboards
    };
}
