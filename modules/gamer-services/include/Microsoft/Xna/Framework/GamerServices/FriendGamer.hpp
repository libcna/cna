// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include <string>

namespace Microsoft::Xna::Framework::GamerServices
{
    /**
     * @brief Represents a gamer who is on the local player's friends list.
     */
    class FriendGamer : public Gamer
    {
    public:
        /**
         * @brief Gets whether this friend sent a friend request to the local gamer.
         *
         * @return true if a friend request was received from this gamer.
         */
        [[nodiscard]] bool getFriendRequestReceivedFromProperty() const;

        /**
         * @brief Gets whether the local gamer sent a friend request to this friend.
         *
         * @return true if a friend request was sent to this gamer.
         */
        [[nodiscard]] bool getFriendRequestSentToProperty() const;

        /**
         * @brief Gets whether this friend has voice communication capability.
         *
         * @return Always false: CNA carries no voice.
         */
        [[nodiscard]] bool getHasVoiceProperty() const;

        /**
         * @brief Gets whether this friend accepted an invite from the local gamer, among this title's
         * unexpired invitations.
         *
         * @return true if the invite was accepted.
         */
        [[nodiscard]] bool getInviteAcceptedProperty() const;

        /**
         * @brief Gets whether this friend has a pending, unexpired invite to the local gamer in this
         * title.
         *
         * @return true if an invite was received from this gamer.
         */
        [[nodiscard]] bool getInviteReceivedFromProperty() const;

        /**
         * @brief Gets whether this friend declined an invite from the local gamer, among this title's
         * unexpired invitations.
         *
         * @return true if the invite was rejected.
         */
        [[nodiscard]] bool getInviteRejectedProperty() const;

        /**
         * @brief Gets whether the local gamer has a pending, unexpired invite to this friend in this
         * title.
         *
         * @return true if an invite was sent to this gamer.
         */
        [[nodiscard]] bool getInviteSentToProperty() const;

        /**
         * @brief Gets whether this friend is currently away.
         *
         * @return Always false: CNA accounts have no away status.
         */
        [[nodiscard]] bool getIsAwayProperty() const;

        /**
         * @brief Gets whether this friend is currently busy.
         *
         * @return Always false: CNA accounts have no busy status.
         */
        [[nodiscard]] bool getIsBusyProperty() const;

        /**
         * @brief Gets whether this friend is in a public session that can be joined: online in this
         * title's player-match session that admits joiners now and has a public slot free.
         *
         * @return true if joinable.
         */
        [[nodiscard]] bool getIsJoinableProperty() const;

        /**
         * @brief Gets whether this friend is currently online.
         *
         * @return true if online.
         */
        [[nodiscard]] bool getIsOnlineProperty() const;

        /**
         * @brief Gets whether this friend is currently playing a game. A CNA account is online only
         * from a game, so this is true exactly when the friend is online.
         *
         * @return true if playing.
         */
        [[nodiscard]] bool getIsPlayingProperty() const;

        /**
         * @brief Gets this friend's current presence string.
         *
         * @return The presence string.
         */
        [[nodiscard]] const std::string& getPresenceProperty() const;

        /** @brief Creates a FriendGamer for CNA internal use. */
        CNAEXT static FriendGamer CreateInternal(
            const std::string& gamertag,
            const std::string& displayName,
            bool online,
            bool playing,
            bool away,
            bool busy,
            bool requestingFriend,
            bool friendRequesting
        );

    private:
        friend class SignedInGamer;
        FriendGamer(
            const std::string& gamertag,
            const std::string& displayName,
            bool online,
            bool playing,
            bool away,
            bool busy,
            bool requestingFriend,
            bool friendRequesting
        );

        bool friendRequestReceivedFrom_;
        bool friendRequestSentTo_;
        bool hasVoice_{false};
        bool inviteAccepted_{false};
        bool inviteReceivedFrom_{false};
        bool inviteRejected_{false};
        bool inviteSentTo_{false};
        bool isAway_;
        bool isBusy_;
        bool isJoinable_{false};
        bool isOnline_;
        bool isPlaying_;
        std::string presence_;
    };
}
