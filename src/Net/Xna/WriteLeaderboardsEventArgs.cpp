// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Net/WriteLeaderboardsEventArgs.hpp"

namespace Microsoft::Xna::Framework::Net
{
    WriteLeaderboardsEventArgs::WriteLeaderboardsEventArgs(NetworkGamer* gamer, bool isLeaving)
        : gamer_(gamer)
        , isLeaving_(isLeaving)
    {
    }

    WriteLeaderboardsEventArgs WriteLeaderboardsEventArgs::CreateInternal(NetworkGamer* gamer, bool isLeaving)
    {
        return WriteLeaderboardsEventArgs(gamer, isLeaving);
    }

    NetworkGamer* WriteLeaderboardsEventArgs::getGamerProperty() const { return gamer_; }
    bool WriteLeaderboardsEventArgs::getIsLeavingProperty() const      { return isLeaving_; }
}
