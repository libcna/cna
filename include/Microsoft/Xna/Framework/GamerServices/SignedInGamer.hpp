// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AchievementCollection.hpp"
#include "Microsoft/Xna/Framework/GamerServices/FriendCollection.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GameDefaults.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerPresence.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerPrivileges.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInEventArgs.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedOutEventArgs.hpp"
#include "Microsoft/Xna/Framework/PlayerIndex.hpp"
#include "System/AsyncCallback.hpp"
#include "System/EventHandler.hpp"
#include "System/IAsyncResult.hpp"
#include <any>
#include <string>

namespace Microsoft::Xna::Framework::Audio
{
    class Microphone;
}

namespace Microsoft::Xna::Framework::GamerServices
{
    /**
     * @brief Represents a gamer that is signed in on the local system.
     */
    class SignedInGamer final : public Gamer
    {
    public:
        /**
         * @brief Gets the default preferences for this gamer.
         *
         * @return Reference to the GameDefaults.
         */
        [[nodiscard]] const GameDefaults& getGameDefaultsProperty() const;

        /**
         * @brief Gets whether this gamer is a guest.
         *
         * @return true if a guest profile.
         */
        [[nodiscard]] bool getIsGuestProperty() const;

        /**
         * @brief Gets whether this gamer is signed in to Xbox Live.
         *
         * @return true if signed in to Live.
         */
        [[nodiscard]] bool getIsSignedInToLiveProperty() const;

        /**
         * @brief Gets the number of players in this gamer's party.
         *
         * @return The party size.
         */
        [[nodiscard]] int getPartySizeProperty() const;

        /**
         * @brief Sets the number of players in this gamer's party.
         *
         * @param value The party size.
         */
        void setPartySizeProperty(int value);

        /**
         * @brief Gets the controller/player slot this gamer occupies.
         *
         * @return The PlayerIndex.
         */
        [[nodiscard]] Microsoft::Xna::Framework::PlayerIndex getPlayerIndexProperty() const;

        /**
         * @brief Gets the gamer's rich presence information.
         *
         * @return Reference to the GamerPresence.
         */
        [[nodiscard]] const GamerPresence& getPresenceProperty() const;

        /**
         * @brief Gets the gamer's privilege settings.
         *
         * @return Reference to the GamerPrivileges.
         */
        [[nodiscard]] const GamerPrivileges& getPrivilegesProperty() const;

        /**
         * @brief Determines whether the specified gamer is a friend of this gamer.
         *
         * @param gamer The gamer to check.
         * @return Always false in this platform's implementation.
         */
        [[nodiscard]] bool IsFriend(Gamer* gamer) const;

        /**
         * @brief Determines whether the specified microphone is a headset.
         *
         * @param microphone The microphone to check.
         * @return true if the microphone is a headset.
         */
        [[nodiscard]] bool IsHeadset(const Microsoft::Xna::Framework::Audio::Microphone& microphone) const;

        /**
         * @brief Gets the gamer's friends list.
         *
         * @return An empty FriendCollection in this platform's implementation.
         */
        [[nodiscard]] FriendCollection GetFriends() const;

        /**
         * @brief Unlocks the achievement identified by the given key.
         *
         * @param achievementKey The achievement key.
         */
        void AwardAchievement(const std::string& achievementKey);

        /**
         * @brief Begins an asynchronous request to award an achievement.
         *
         * @param achievementKey The achievement key.
         * @param callback       Invoked when the operation completes.
         * @param state          User-defined state passed through to the callback.
         * @return An IAsyncResult already marked complete; caller owns it and must delete it
         *         after EndAwardAchievement.
         */
        [[nodiscard]] System::IAsyncResult* BeginAwardAchievement(
            const std::string& achievementKey,
            System::AsyncCallback callback,
            std::any state
        );

        /**
         * @brief Completes an asynchronous AwardAchievement request.
         *
         * @param result The result returned by BeginAwardAchievement.
         */
        void EndAwardAchievement(System::IAsyncResult* result);

        /**
         * @brief Synchronously retrieves this gamer's achievements.
         *
         * @return An AchievementCollection (empty in this platform's implementation).
         */
        [[nodiscard]] AchievementCollection GetAchievements();

        /**
         * @brief Begins an asynchronous request to retrieve this gamer's achievements.
         *
         * @param callback   Invoked when the operation completes.
         * @param asyncState User-defined state passed through to the callback.
         * @return Heap-allocated IAsyncResult; caller owns it and must delete it after
         *         EndGetAchievements.
         * @throws System::InvalidOperationException if a request is already in progress.
         */
        [[nodiscard]] System::IAsyncResult* BeginGetAchievements(System::AsyncCallback callback, std::any asyncState);

        /**
         * @brief Completes an asynchronous GetAchievements request.
         *
         * @param result The result returned by BeginGetAchievements.
         * @return An empty AchievementCollection in this platform's implementation.
         */
        [[nodiscard]] AchievementCollection EndGetAchievements(System::IAsyncResult* result);

        /**
         * @brief Raised when a gamer signs in.
         *
         * Task 7.6: genuine public XNA 4.0 API (FNA: `public static event
         * EventHandler<SignedInEventArgs> SignedIn;`) - not a CNA extension, so this is not
         * NOXNA (unlike OnSignIn/OnSignOut just below, which really are internal-only).
         */
        static System::EventHandler<SignedInEventArgs> SignedIn;

        /**
         * @brief Raised when a gamer signs out.
         *
         * Task 7.6: genuine public XNA 4.0 API (FNA: `public static event
         * EventHandler<SignedOutEventArgs> SignedOut;`) - not a CNA extension.
         */
        static System::EventHandler<SignedOutEventArgs> SignedOut;

        /**
         * @brief Raises the SignedIn event for the given gamer.
         *
         * @param gamer The gamer that signed in.
         */
        NOXNA static void OnSignIn(SignedInGamer* gamer);

        /**
         * @brief Raises the SignedOut event for the given gamer.
         *
         * @param gamer The gamer that signed out.
         */
        NOXNA static void OnSignOut(SignedInGamer* gamer);

        /** @brief Creates a SignedInGamer for CNA internal use. */
        NOXNA static SignedInGamer CreateInternal(
            const std::string& gamertag,
            bool isSignedInToLive = false,
            bool isGuest = false,
            Microsoft::Xna::Framework::PlayerIndex playerIndex = Microsoft::Xna::Framework::PlayerIndex::One
        );

    private:
        SignedInGamer(
            const std::string& gamertag,
            bool isSignedInToLive,
            bool isGuest,
            Microsoft::Xna::Framework::PlayerIndex playerIndex
        );

        bool isGuest_;
        bool isSignedInToLive_;
        Microsoft::Xna::Framework::PlayerIndex playerIndex_;
        GameDefaults gameDefaults_;
        GamerPresence presence_;
        GamerPrivileges privileges_;
        int partySize_;
        GamerAction* statStoreAction_{nullptr};
        GamerAction* statReceiveAction_{nullptr};
    };
}
