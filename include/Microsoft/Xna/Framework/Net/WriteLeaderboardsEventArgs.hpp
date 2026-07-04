// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/CNAHelper.hpp"
#include "System/EventArgs.hpp"

namespace Microsoft::Xna::Framework::Net
{
    class NetworkGamer;

    /**
     * @brief Contains data for the WriteLeaderboards event.
     */
    class WriteLeaderboardsEventArgs final : public System::EventArgs
    {
    public:
        /**
         * @brief Gets the gamer whose leaderboard data is being written.
         *
         * @return Pointer to the NetworkGamer.
         */
        [[nodiscard]] NetworkGamer* getGamerProperty() const;

        /**
         * @brief Gets whether the gamer is leaving the session.
         *
         * @return true if the gamer is leaving.
         */
        [[nodiscard]] bool getIsLeavingProperty() const;

        /** @brief Creates a WriteLeaderboardsEventArgs for CNA internal use. */
        NOXNA static WriteLeaderboardsEventArgs CreateInternal(NetworkGamer* gamer, bool isLeaving);

    private:
        WriteLeaderboardsEventArgs(NetworkGamer* gamer, bool isLeaving);

        NetworkGamer* gamer_;
        bool isLeaving_;
    };
}
