// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"
#include "Microsoft/Xna/Framework/Audio/Microphone.hpp"
#include "System/InvalidOperationException.hpp"

namespace Microsoft::Xna::Framework::GamerServices
{
    System::EventHandler<SignedInEventArgs> SignedInGamer::SignedIn;
    System::EventHandler<SignedOutEventArgs> SignedInGamer::SignedOut;

    SignedInGamer::SignedInGamer(
        const std::string& gamertag,
        bool isSignedInToLive,
        bool isGuest,
        Microsoft::Xna::Framework::PlayerIndex playerIndex
    )
        : Gamer(gamertag, gamertag)
        , isGuest_(isGuest)
        , isSignedInToLive_(isSignedInToLive)
        , playerIndex_(playerIndex)
        , gameDefaults_(GameDefaults::CreateInternal())
        , presence_(GamerPresence::CreateInternal())
        , privileges_(GamerPrivileges::CreateInternal())
        , partySize_(1)
    {
    }

    SignedInGamer SignedInGamer::CreateInternal(
        const std::string& gamertag,
        bool isSignedInToLive,
        bool isGuest,
        Microsoft::Xna::Framework::PlayerIndex playerIndex
    ) {
        return SignedInGamer(gamertag, isSignedInToLive, isGuest, playerIndex);
    }

    const GameDefaults& SignedInGamer::getGameDefaultsProperty() const     { return gameDefaults_; }
    bool SignedInGamer::getIsGuestProperty() const                        { return isGuest_; }
    bool SignedInGamer::getIsSignedInToLiveProperty() const               { return isSignedInToLive_; }
    int SignedInGamer::getPartySizeProperty() const                       { return partySize_; }
    void SignedInGamer::setPartySizeProperty(int value)                  { partySize_ = value; }

    Microsoft::Xna::Framework::PlayerIndex SignedInGamer::getPlayerIndexProperty() const
    {
        return playerIndex_;
    }

    const GamerPresence& SignedInGamer::getPresenceProperty() const       { return presence_; }
    GamerPresence& SignedInGamer::getPresenceProperty()                   { return presence_; }
    const GamerPrivileges& SignedInGamer::getPrivilegesProperty() const   { return privileges_; }

    bool SignedInGamer::IsFriend(Gamer* /*gamer*/) const
    {
        return false;
    }

    bool SignedInGamer::IsHeadset(const Microsoft::Xna::Framework::Audio::Microphone& microphone) const
    {
        return microphone.getIsHeadsetProperty();
    }

    FriendCollection SignedInGamer::GetFriends() const
    {
        return FriendCollection::CreateInternal({});
    }

    void SignedInGamer::AwardAchievement(const std::string& /*achievementKey*/)
    {
    }

    System::IAsyncResult* SignedInGamer::BeginAwardAchievement(
        const std::string& /*achievementKey*/,
        System::AsyncCallback callback,
        std::any state
    ) {
        // FNA: the overlap check on statStoreAction is intentionally a no-op — the
        // reference source's overlap guard is commented out ("FIXME: Pray...").
        statStoreAction_ = new GamerAction(std::move(state), std::move(callback));
        statStoreAction_->setIsCompletedProperty(true);
        return statStoreAction_;
    }

    void SignedInGamer::EndAwardAchievement(System::IAsyncResult* /*result*/)
    {
        statStoreAction_ = nullptr;
    }

    AchievementCollection SignedInGamer::GetAchievements()
    {
        System::IAsyncResult* result = BeginGetAchievements(System::AsyncCallback{}, std::any{});
        while (!result->getIsCompletedProperty())
        {
            if (!GamerServicesDispatcher::UpdateAsync())
            {
                statReceiveAction_->setIsCompletedProperty(true);
            }
        }
        AchievementCollection achievements = EndGetAchievements(result);
        delete result; // caller-owned; FNA relies on GC, so there is no explicit dispose call
        return achievements;
    }

    System::IAsyncResult* SignedInGamer::BeginGetAchievements(System::AsyncCallback callback, std::any asyncState)
    {
        if (statReceiveAction_ != nullptr)
        {
            throw System::InvalidOperationException();
        }
        statReceiveAction_ = new GamerAction(std::move(asyncState), std::move(callback));
        // Task 7.1: there is no real deferred work to wait on (matching BeginAwardAchievement's
        // own already-correct pattern just above) - GetAchievements()'s polling loop only ever
        // completes the action itself via GamerServicesDispatcher::UpdateAsync() returning false,
        // which never happens once a real GamerServicesComponent exists (isInitialized_ becomes
        // permanently true, matching this project's own established "no reset hook" pattern) -
        // making that loop spin forever at 100% CPU for any real game. Mark it complete
        // immediately instead.
        statReceiveAction_->setIsCompletedProperty(true);
        return statReceiveAction_;
    }

    AchievementCollection SignedInGamer::EndGetAchievements(System::IAsyncResult* /*result*/)
    {
        statReceiveAction_ = nullptr;
        return AchievementCollection::CreateInternal({});
    }

    void SignedInGamer::OnSignIn(SignedInGamer* gamer)
    {
        if (!SignedIn.Empty())
        {
            SignedIn.Raise(nullptr, SignedInEventArgs(gamer));
        }
    }

    void SignedInGamer::OnSignOut(SignedInGamer* gamer)
    {
        if (!SignedOut.Empty())
        {
            SignedOut.Raise(nullptr, SignedOutEventArgs(gamer));
        }
    }
}
