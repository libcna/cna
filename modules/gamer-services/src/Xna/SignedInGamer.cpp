// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "CNA/Internal/GamerServices/LocalGamerServicesStore.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"
#include "Microsoft/Xna/Framework/Audio/Microphone.hpp"
#include "System/DateTime.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/TimeZone.hpp"
#include "../Internal/ServiceAsyncResult.hpp"

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

    bool SignedInGamer::IsFriend(Gamer* gamer) const
    {
        if (!serviceUserId_.empty()) {
            if (!gamer) throw System::ArgumentException("Gamer is null.", "gamer");
            for (const auto& entry : CNA::Internal::GamerServices::backend()->friends(serviceUserId_))
                if (entry.accepted && entry.gamertag == gamer->getGamertagProperty()) return true;
        }
        return false;
    }

    bool SignedInGamer::IsHeadset(const Microsoft::Xna::Framework::Audio::Microphone& microphone) const
    {
        return microphone.getIsHeadsetProperty();
    }

    FriendCollection SignedInGamer::GetFriends() const
    {
        if (!serviceUserId_.empty()) {
            std::vector<std::shared_ptr<FriendGamer>> owned;
            std::vector<FriendGamer*> friends;
            for (const auto& entry : CNA::Internal::GamerServices::backend()->friends(serviceUserId_)) {
                auto friendGamer = std::shared_ptr<FriendGamer>(new FriendGamer(entry.gamertag, entry.gamertag, entry.online, entry.online, false, false, entry.requestSent, entry.requestReceived));
                friendGamer->presence_ = entry.presence;
                friends.push_back(friendGamer.get()); owned.push_back(std::move(friendGamer));
            }
            auto collection = FriendCollection::CreateInternal(std::move(friends));
            collection.ownedFriends_ = std::move(owned); return collection;
        }
        return FriendCollection::CreateInternal({});
    }

    void SignedInGamer::AwardAchievement(const std::string& achievementKey)
    {
        if (!serviceUserId_.empty()) {
            CNA::Internal::GamerServices::backend()->award(serviceUserId_, achievementKey);
            return;
        }
        CNA::Internal::GamerServices::SaveEarnedAchievementEXT(
            getGamertagProperty(), achievementKey, System::DateTime::getNowProperty().getTicksProperty()
        );
    }

    System::IAsyncResult* SignedInGamer::BeginAwardAchievement(
        const std::string& achievementKey,
        System::AsyncCallback callback,
        std::any state
    ) {
        if (!serviceUserId_.empty()) {
            auto service = CNA::Internal::GamerServices::backend(); const auto user = serviceUserId_;
            return CNA::Internal::GamerServices::ServiceAsyncResult::begin("award", this,
                [user, achievementKey](auto& executor) -> std::any { executor.award(user, achievementKey); return {}; }, std::move(callback), std::move(state),std::move(service));
        }
        AwardAchievement(achievementKey);
        // FNA: the overlap check on statStoreAction is intentionally a no-op — the
        // reference source's overlap guard is commented out ("FIXME: Pray...").
        statStoreAction_ = new GamerAction(std::move(state), std::move(callback));
        statStoreAction_->setIsCompletedProperty(true);
        // audit_net.md High finding: the callback used to only be stored, never invoked, despite
        // this action already completing synchronously right above. A re-entrant callback that
        // itself calls EndAwardAchievement() (which nulls statStoreAction_) must not make this
        // method return a stale null pointer, so capture the pointer to return before invoking
        // the callback, not after.
        GamerAction* action = statStoreAction_;
        if (action->Callback)
        {
            action->Callback(*action);
        }
        return action;
    }

    void SignedInGamer::EndAwardAchievement(System::IAsyncResult* result)
    {
        if (dynamic_cast<CNA::Internal::GamerServices::ServiceAsyncResult*>(result)) {
            (void)CNA::Internal::GamerServices::ServiceAsyncResult::end(result, "award", this); return;
        }
        statStoreAction_ = nullptr;
    }

    AchievementCollection SignedInGamer::GetAchievements()
    {
        System::IAsyncResult* result = BeginGetAchievements(System::AsyncCallback{}, std::any{});
        if (dynamic_cast<CNA::Internal::GamerServices::ServiceAsyncResult*>(result)) {
            std::unique_ptr<System::IAsyncResult> owned(result); return EndGetAchievements(result);
        }
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
        if (!serviceUserId_.empty()) {
            auto service = CNA::Internal::GamerServices::backend(); const auto user = serviceUserId_;
            return CNA::Internal::GamerServices::ServiceAsyncResult::begin("achievements", this,
                [user](auto& executor) -> std::any { return executor.achievements(user); }, std::move(callback), std::move(asyncState),std::move(service));
        }
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
        // audit_net.md High finding: the callback used to only be stored, never invoked, despite
        // this action already completing synchronously right above. A re-entrant callback that
        // itself calls EndGetAchievements() (which nulls statReceiveAction_) must not make this
        // method return a stale null pointer, so capture the pointer to return before invoking
        // the callback, not after.
        GamerAction* action = statReceiveAction_;
        if (action->Callback)
        {
            action->Callback(*action);
        }
        return action;
    }

    AchievementCollection SignedInGamer::EndGetAchievements(System::IAsyncResult* result)
    {
        if (dynamic_cast<CNA::Internal::GamerServices::ServiceAsyncResult*>(result)) {
            const auto records = std::any_cast<std::vector<CNA::Internal::GamerServices::ServiceAchievement>>(
                CNA::Internal::GamerServices::ServiceAsyncResult::end(result, "achievements", this));
            std::vector<Achievement> values;
            for (const auto& record : records) {
                // The service records UTC. EarnedDateTime is local time, as .NET's FromFileTime makes
                // the Xbox achievement time and as the offline store records it (DateTime.Now).
                const auto earned = record.earnedTicks != 0
                    ? System::DateTime(record.earnedTicks, System::DateTimeKind::Utc).ToLocalTime(System::TimeZone::CurrentTimeZone())
                    : System::DateTime(0);
                auto value = Achievement::CreateInternal(record.key, record.name, record.description,
                    record.displayBeforeEarned, record.earnedTicks != 0, earned);
                value.gamerScore_ = record.score; value.howToEarn_ = record.howToEarn;value.pictureHash_=record.picture;value.serviceBacked_=true;
                values.push_back(std::move(value));
            }
            return AchievementCollection::CreateInternal(std::move(values));
        }
        statReceiveAction_ = nullptr;
        std::vector<Achievement> achievements;
        for (const auto& record : CNA::Internal::GamerServices::LoadEarnedAchievementsEXT(getGamertagProperty()))
        {
            achievements.push_back(Achievement::CreateInternal(
                record.Key, "", "", true, true, System::DateTime(record.EarnedTicks)
            ));
        }
        return AchievementCollection::CreateInternal(std::move(achievements));
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
