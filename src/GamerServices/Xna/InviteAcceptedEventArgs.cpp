// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/GamerServices/InviteAcceptedEventArgs.hpp"

namespace Microsoft::Xna::Framework::GamerServices
{
    InviteAcceptedEventArgs::InviteAcceptedEventArgs(SignedInGamer* gamer, bool isCurrentSession)
        : gamer_(gamer)
        , isCurrentSession_(isCurrentSession)
    {
    }

    SignedInGamer* InviteAcceptedEventArgs::getGamerProperty() const
    {
        return gamer_;
    }

    bool InviteAcceptedEventArgs::getIsCurrentSessionProperty() const
    {
        return isCurrentSession_;
    }
}
