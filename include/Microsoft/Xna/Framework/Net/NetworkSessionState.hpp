// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Net
{
    /**
     * @brief Describes the current state of a network session.
     */
    enum class NetworkSessionState
    {
        /** @brief The session is waiting in its pre-game lobby. */
        Lobby,
        /** @brief The session is actively playing a game. */
        Playing,
        /** @brief The session has ended. */
        Ended
    };
}
