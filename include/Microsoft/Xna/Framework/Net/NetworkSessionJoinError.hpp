// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Net
{
    /**
     * @brief Describes why an attempt to join a network session failed.
     */
    enum class NetworkSessionJoinError
    {
        /** @brief The requested session could not be found. */
        SessionNotFound,
        /** @brief The requested session does not allow new gamers to join. */
        SessionNotJoinable,
        /** @brief The requested session has no free player slots. */
        SessionFull
    };
}
