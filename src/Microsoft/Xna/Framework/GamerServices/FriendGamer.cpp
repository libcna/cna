// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/GamerServices/FriendGamer.hpp"

namespace Microsoft::Xna::Framework::GamerServices
{
    FriendGamer::FriendGamer(
        const std::string& gamertag,
        const std::string& displayName,
        bool online,
        bool playing,
        bool away,
        bool busy,
        bool requestingFriend,
        bool friendRequesting
    )
        : Gamer(gamertag, displayName)
        , friendRequestReceivedFrom_(friendRequesting)
        , friendRequestSentTo_(requestingFriend)
        , isAway_(away)
        , isBusy_(busy)
        , isOnline_(online)
        , isPlaying_(playing)
    {
    }

    FriendGamer FriendGamer::CreateInternal(
        const std::string& gamertag,
        const std::string& displayName,
        bool online,
        bool playing,
        bool away,
        bool busy,
        bool requestingFriend,
        bool friendRequesting
    )
    {
        return FriendGamer(gamertag, displayName, online, playing, away, busy, requestingFriend, friendRequesting);
    }

    bool FriendGamer::getFriendRequestReceivedFromProperty() const { return friendRequestReceivedFrom_; }
    bool FriendGamer::getFriendRequestSentToProperty() const       { return friendRequestSentTo_; }
    bool FriendGamer::getHasVoiceProperty() const                  { return hasVoice_; }
    bool FriendGamer::getInviteAcceptedProperty() const            { return inviteAccepted_; }
    bool FriendGamer::getInviteReceivedFromProperty() const        { return inviteReceivedFrom_; }
    bool FriendGamer::getInviteRejectedProperty() const            { return inviteRejected_; }
    bool FriendGamer::getInviteSentToProperty() const              { return inviteSentTo_; }
    bool FriendGamer::getIsAwayProperty() const                    { return isAway_; }
    bool FriendGamer::getIsBusyProperty() const                    { return isBusy_; }
    bool FriendGamer::getIsJoinableProperty() const                { return isJoinable_; }
    bool FriendGamer::getIsOnlineProperty() const                  { return isOnline_; }
    bool FriendGamer::getIsPlayingProperty() const                 { return isPlaying_; }
    const std::string& FriendGamer::getPresenceProperty() const    { return presence_; }
}
