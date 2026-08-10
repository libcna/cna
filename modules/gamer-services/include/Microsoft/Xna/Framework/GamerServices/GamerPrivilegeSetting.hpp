// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::GamerServices
{
    /**
     * @brief Describes the level of access a gamer has granted to others for a particular privilege.
     */
    enum class GamerPrivilegeSetting
    {
        /** @brief Access is blocked for everyone. */
        Blocked,
        /** @brief Access is allowed only for friends. */
        FriendsOnly,
        /** @brief Access is allowed for everyone. */
        Everyone
    };
}
