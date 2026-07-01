// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Net
{
    /**
     * @brief Describes why a network session ended.
     */
    enum class NetworkSessionEndReason
    {
        /** @brief The local gamer signed out. */
        ClientSignedOut,
        /** @brief The host ended the session. */
        HostEndedSession,
        /** @brief The local gamer was removed by the host. */
        RemovedByHost,
        /** @brief The connection to the session was lost. */
        Disconnected
    };
}
